#!/usr/bin/env python3
# /// script
# requires-python = ">=3.15"
# dependencies = [
#     "arviz-base>=1.3.1",
#     "arviz-stats>=1.3.3",
#     "cyclopts>=5.5.0",
#     "numpy>=2.5.4",
#     "plotly>=7.1.0",
#     "polars>=2.1.0",
#     "scipy>=1.18.1",
#     "structlog>=26.1.0",
# ]
# ///
"""Benchmark runner CLI with host tuning, Google Benchmark, and BEST analysis.

Executes Google Benchmark in either ``fast-screen`` or ``full-statistical`` mode,
prepares the host environment (``taskpolicy``, ``caffeinate``, ``pmset`` telemetry on
Darwin; ``taskset``/``cpupower`` on Linux), runs Bayesian BEST comparison between
baseline and candidate series, and generates Markdown + interactive Plotly HTML reports.
"""

import json
import os
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import UTC, datetime
from itertools import starmap
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Any, Final, Literal, cast

import structlog
from cyclopts import App, Parameter

from scripts.benchmarks.host_tuner import (
    DarwinHostTuner,
    HostTuner,
    LinuxHostTuner,
    get_host_tuner,
)
lazy from scripts.benchmarks.bayesian_compare import (
    BestResult,
    analyze_benchmark,
    generate_markdown_report,
    generate_plotly_html_report,
    parse_google_benchmark_json,
    promote_to_baseline,
    serialize_results,
)


if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence


# Re-export tuners for interface inspection
__all__ = [
    "DarwinHostTuner",
    "HostTuner",
    "LinuxHostTuner",
    "get_host_tuner",
    "run_benchmark_suite",
]

type RunMode = Literal["fast-screen", "full-statistical"]

_DEFAULT_ROPE_FRACTION: Final[float] = 0.02
_PERCENT_SCALE: Final[float] = 100.0
_MIN_PAIR_SAMPLES: Final[int] = 2
_APPLE_CRYSTAL_MHZ: Final[int] = 24
_DEFAULT_CACHELINE_BYTES: Final[int] = 128


@dataclass(slots=True, frozen=True)
class _ModeDefaults:
    """Default repetition and timing parameters per execution mode."""

    repetitions: int
    min_time: str
    min_warmup_time: str


@dataclass(slots=True, frozen=True)
class _ExecutionSpec:
    """Resolved execution parameters for a Google Benchmark invocation."""

    repo_root: Path
    binary: Path
    benchmark_filter: str
    repetitions: int
    min_time: str
    warmup_seconds: str
    raw_json_path: Path
    random_interleaving: bool
    tuner: HostTuner | None


_MODE_DEFAULTS: Final[frozendict[RunMode, _ModeDefaults]] = frozendict[
    RunMode, _ModeDefaults
]() | cast(
    "dict[RunMode, _ModeDefaults]",
    {
        "fast-screen": _ModeDefaults(
            repetitions=3,
            min_time="0.1s",
            min_warmup_time="0.02",
        ),
        "full-statistical": _ModeDefaults(
            repetitions=20,
            min_time="0.5s",
            min_warmup_time="0.05",
        ),
    },
)

_BINARY_CANDIDATES: Final[tuple[tuple[str, ...], ...]] = (
    ("build", "benchmark", "bin", "bitmap_flip_benchmark"),
    (
        "build",
        "benchmark",
        "benchmarks",
        "cpp",
        "systems",
        "bitmap_flip_benchmark",
    ),
)

_BENIGN_DARWIN_GBENCH_PATTERNS: Final[tuple[str, ...]] = (
    "Unable to determine clock rate from sysctl: hw.cpufrequency:",
    "This does not affect benchmark measurements, only the metadata output.",
    (
        "***WARNING*** Failed to set thread affinity. "
        "Estimated CPU frequency may be incorrect."
    ),
)


def _get_logger(name: str = __name__) -> structlog.stdlib.BoundLogger:
    """Build a ``structlog`` logger writing structured JSON records to ``sys.stderr``.

    Args:
        name: Logical logger identifier.

    Returns:
        Configured ``structlog`` logger bound to stderr.
    """
    logger = structlog.wrap_logger(
        structlog.PrintLoggerFactory(file=sys.stderr)(name),
        processors=[
            structlog.processors.TimeStamper(fmt="iso", utc=True),
            structlog.processors.add_log_level,
            structlog.processors.JSONRenderer(),
        ],
        wrapper_class=structlog.BoundLogger,
    )
    return cast("structlog.stdlib.BoundLogger", logger)


def _validate_rope_fraction(rope_fraction: float) -> None:
    """Ensure ``rope_fraction`` lies in ``[0.0, 1.0)``.

    Args:
        rope_fraction: Half-width of the Region of Practical Equivalence.

    Raises:
        ValueError: If ``rope_fraction`` is negative or ``>= 1.0``.
    """
    if rope_fraction < 0.0:
        msg = f"rope_fraction must be non-negative (got {rope_fraction})"
        raise ValueError(msg)
    if rope_fraction >= 1.0:
        msg = f"rope_fraction must be less than 1.0 (got {rope_fraction})"
        raise ValueError(msg)


def find_or_build_binary(custom_path: Path | None = None) -> Path:
    """Finds the benchmark binary or builds it via the CMake ``benchmark`` preset.

    Args:
        custom_path: Optional explicit binary path provided by the user.

    Returns:
        Absolute ``Path`` to the executable.

    Raises:
        FileNotFoundError: If binary cannot be found or built.
        PermissionError: If specified custom binary is not executable.
    """
    repo_root = Path(__file__).resolve().parent.parent.parent

    if custom_path is not None:
        resolved_custom = Path(custom_path).resolve()
        if not resolved_custom.exists():
            msg = f"Specified benchmark binary not found: {custom_path}"
            raise FileNotFoundError(msg)
        if not os.access(resolved_custom, os.X_OK):
            msg = f"Specified benchmark binary is not executable: {custom_path}"
            raise PermissionError(msg)
        return resolved_custom

    candidates = list(starmap(repo_root.joinpath, _BINARY_CANDIDATES))
    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate.resolve()

    cmake_bin = shutil.which("cmake")
    if cmake_bin:
        build_proc = subprocess.run(
            [cmake_bin, "--build", "--preset", "benchmark"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=False,
        )
        if build_proc.returncode == 0:
            for candidate in candidates:
                if candidate.exists() and os.access(candidate, os.X_OK):
                    return candidate.resolve()

    msg = f"Benchmark executable bitmap_flip_benchmark not found in {candidates}"
    raise FileNotFoundError(msg)


def extract_paired_series(
    raw_series: dict[str, list[float]],
    rope_fraction: float = _DEFAULT_ROPE_FRACTION,
) -> tuple[dict[str, dict[str, list[float]]], dict[str, BestResult]]:
    """Pairs baseline and candidate benchmark series by geometry and runs BEST analysis.

    Args:
        raw_series: Mapping from registered benchmark name to sample latency list.
        rope_fraction: Half-width of ROPE region (default 0.02 = 2%).

    Returns:
        Tuple of ``(paired_samples_dict, results_dict)``.
    """
    _validate_rope_fraction(rope_fraction)

    paired_samples: dict[str, dict[str, list[float]]] = {}
    for name, samples in raw_series.items():
        if "_Baseline/" in name:
            geometry = name.split("_Baseline/", 1)[1]
            paired_samples.setdefault(geometry, {})["baseline"] = samples
        elif "_Candidate/" in name:
            geometry = name.split("_Candidate/", 1)[1]
            paired_samples.setdefault(geometry, {})["candidate"] = samples

    results: dict[str, BestResult] = {}
    for geometry, pair in paired_samples.items():
        base = pair.get("baseline", [])
        cand = pair.get("candidate", [])
        if len(base) >= _MIN_PAIR_SAMPLES and len(cand) >= _MIN_PAIR_SAMPLES:
            results[geometry] = analyze_benchmark(
                base,
                cand,
                rope_fraction=rope_fraction,
            )
        elif len(base) >= 1 and len(cand) >= 1:
            # Fallback for fast-screen N=1 repetition: duplicate single draw.
            pseudo_base = base if len(base) >= _MIN_PAIR_SAMPLES else base + base
            pseudo_cand = cand if len(cand) >= _MIN_PAIR_SAMPLES else cand + cand
            results[geometry] = analyze_benchmark(
                pseudo_base,
                pseudo_cand,
                rope_fraction=rope_fraction,
            )

    return paired_samples, results


def filter_apple_silicon_gbench_output(
    stderr_text: str,
    host_meta: dict[str, Any],
) -> str:
    """Filters expected Apple Silicon sysctl/affinity warnings and annotates clocks.

    On Apple Silicon, ``hw.cpufrequency`` does not exist in ``sysctl`` and Mach
    ``THREAD_AFFINITY_POLICY`` returns ``KERN_NOT_SUPPORTED`` (XNU binds threads to
    P-cores via ``QOS_CLASS_USER_INTERACTIVE`` instead), causing Google Benchmark's
    fallback estimator to report the 24 MHz ARM ``cntvct_el0`` reference crystal.

    Args:
        stderr_text: Raw stderr string from the Google Benchmark process.
        host_meta: Host telemetry dictionary from ``HostTuner.prepare()``.

    Returns:
        Cleaned stderr string with accurate Apple Silicon clock annotation.
    """
    if host_meta.get("system") != "Darwin":
        return stderr_text

    p_mhz = host_meta.get("pcore_max_mhz")
    filtered_lines: list[str] = []
    for raw_line in stderr_text.splitlines():
        if any(pat in raw_line for pat in _BENIGN_DARWIN_GBENCH_PATTERNS):
            continue
        rendered = raw_line
        if p_mhz and "24 MHz CPU s" in rendered:
            rendered = rendered.replace(
                "24 MHz CPU s",
                (
                    f"{float(p_mhz):.0f} MHz P-core max / "
                    "24 MHz ARM cntfrq_el0 timer CPU s"
                ),
            )
        filtered_lines.append(rendered)

    return "\n".join(filtered_lines) + ("\n" if filtered_lines else "")


def patch_google_benchmark_json_context(
    json_path: Path,
    host_meta: dict[str, Any],
) -> None:
    """Enriches ``benchmark_raw.json`` context with Apple Silicon clock metadata.

    Args:
        json_path: Path to ``benchmark_raw.json`` written by Google Benchmark.
        host_meta: Host telemetry dictionary from ``HostTuner.prepare()``.
    """
    if host_meta.get("system") != "Darwin" or not json_path.exists():
        return
    try:
        raw_obj: object = json.loads(json_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError, OSError:
        return

    if not isinstance(raw_obj, dict):
        return
    payload = cast("dict[str, Any]", raw_obj)
    raw_ctx: object = payload.get("context")
    if not isinstance(raw_ctx, dict):
        return
    ctx = cast("dict[str, Any]", raw_ctx)

    p_mhz = host_meta.get("pcore_max_mhz")
    if p_mhz is not None:
        if ctx.get("mhz_per_cpu") == _APPLE_CRYSTAL_MHZ:
            ctx["apple_cntfrq_el0_mhz"] = _APPLE_CRYSTAL_MHZ
        ctx["mhz_per_cpu"] = p_mhz
        ctx["apple_pcore_max_mhz"] = p_mhz

    e_mhz = host_meta.get("ecore_max_mhz")
    if e_mhz is not None:
        ctx["apple_ecore_max_mhz"] = e_mhz

    json_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def _resolve_mode_settings(
    mode: str,
    repetitions: int | None,
    min_time: str | None,
    min_warmup_time: str | None,
) -> tuple[int, str, str]:
    """Resolve repetitions, min_time, and min_warmup_time from execution mode.

    Args:
        mode: Benchmark execution mode name.
        repetitions: Explicit repetition count override, if supplied.
        min_time: Explicit ``--benchmark_min_time`` override.
        min_warmup_time: Explicit ``--benchmark_min_warmup_time`` override.

    Returns:
        Tuple of ``(repetitions, min_time, min_warmup_time)``.
    """
    defaults = (
        _MODE_DEFAULTS["fast-screen"]
        if mode == "fast-screen"
        else _MODE_DEFAULTS["full-statistical"]
    )
    return (
        repetitions if repetitions is not None else defaults.repetitions,
        min_time if min_time is not None else defaults.min_time,
        min_warmup_time if min_warmup_time is not None else defaults.min_warmup_time,
    )


def _log_darwin_hardware(
    host_meta: Mapping[str, Any],
    logger: structlog.stdlib.BoundLogger,
) -> None:
    """Emit structured host hardware metadata when running on Apple Silicon."""
    if host_meta.get("system") != "Darwin":
        return

    logger.info(
        "host_hardware_ready",
        cpu_brand=host_meta.get("cpu_brand", "Apple Silicon"),
        machine=host_meta.get("machine", "arm64"),
        pcore_count=host_meta.get("pcore_count"),
        ecore_count=host_meta.get("ecore_count"),
        pcore_max_mhz=host_meta.get("pcore_max_mhz"),
        ecore_max_mhz=host_meta.get("ecore_max_mhz"),
        pcore_l1d_kib=host_meta.get("pcore_l1d_kib"),
        pcore_l1i_kib=host_meta.get("pcore_l1i_kib"),
        pcore_l2_kib=host_meta.get("pcore_l2_kib"),
        cacheline_bytes=host_meta.get("cacheline_bytes", _DEFAULT_CACHELINE_BYTES),
        qos=host_meta.get("qos", "QOS_CLASS_USER_INTERACTIVE"),
    )


def _read_git_head_sha() -> str:
    """Resolve current Git HEAD SHA or return ``'unknown'`` when unavailable.

    Returns:
        Full Git HEAD commit hash or ``'unknown'``.
    """
    git_bin = shutil.which("git")
    if git_bin is None:
        return "unknown"
    try:
        proc = subprocess.run(
            [git_bin, "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
        )
    except subprocess.SubprocessError, OSError:
        return "unknown"
    return proc.stdout.strip() or "unknown" if proc.returncode == 0 else "unknown"


def _build_gbench_command(spec: _ExecutionSpec) -> list[str]:
    """Construct the Google Benchmark command line wrapped by ``HostTuner``.

    Args:
        spec: Resolved execution configuration.

    Returns:
        Command argv sequence ready for ``subprocess.run``.
    """
    cmd = [
        str(spec.binary),
        f"--benchmark_filter={spec.benchmark_filter}",
        f"--benchmark_repetitions={spec.repetitions}",
        f"--benchmark_min_time={spec.min_time}",
        f"--benchmark_min_warmup_time={spec.warmup_seconds}",
        "--benchmark_format=console",
        f"--benchmark_out={spec.raw_json_path}",
        "--benchmark_out_format=json",
    ]
    if spec.random_interleaving and spec.repetitions > 1:
        cmd.append("--benchmark_enable_random_interleaving=true")
    return spec.tuner.wrap_command(cmd) if spec.tuner is not None else cmd


def _execute_gbench(
    spec: _ExecutionSpec,
    host_meta: dict[str, Any],
    logger: structlog.stdlib.BoundLogger,
) -> int:
    """Run the benchmark subprocess, patch Apple Silicon JSON metadata, restore.

    Args:
        spec: Resolved execution configuration.
        host_meta: Mutable host telemetry mapping enriched with post-thermal check.
        logger: Structured stderr logger.

    Returns:
        Subprocess exit code (``0`` on success).
    """
    proc = subprocess.run(
        _build_gbench_command(spec),
        cwd=spec.repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    cleaned_stderr = filter_apple_silicon_gbench_output(proc.stderr or "", host_meta)
    if cleaned_stderr:
        sys.stderr.write(cleaned_stderr)
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        logger.error("benchmark_execution_failed", returncode=proc.returncode)
        return proc.returncode

    patch_google_benchmark_json_context(spec.raw_json_path, host_meta)
    if spec.tuner is not None:
        post_thermal = spec.tuner.check_thermal_throttling()
        host_meta["post_thermal"] = post_thermal
        if post_thermal.get("throttled"):
            logger.warning("thermal_throttling_detected", post_thermal=post_thermal)
        spec.tuner.restore()
    return 0


def _write_reports(
    latest_dir: Path,
    telemetry_meta: dict[str, Any],
    results: dict[str, BestResult],
) -> None:
    """Persist telemetry/analysis JSON and render Markdown + Plotly HTML reports."""
    (latest_dir / "telemetry.json").write_text(
        json.dumps(telemetry_meta, indent=2),
        encoding="utf-8",
    )
    (latest_dir / "analysis.json").write_text(
        json.dumps(
            {
                "meta": telemetry_meta["config"],
                "benchmarks": serialize_results(results),
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    paired_samples = cast(
        "dict[str, dict[str, list[float]]]",
        telemetry_meta["benchmarks"],
    )
    report_md_path = latest_dir / "report.md"
    report_html_path = latest_dir / "report.html"
    md_content = generate_markdown_report(results, telemetry_meta, report_md_path)
    generate_plotly_html_report(
        results,
        paired_samples,
        report_html_path,
        meta=telemetry_meta,
    )

    sys.stdout.write(f"\n{md_content}")
    sys.stdout.write(f"HTML Report saved to: file://{report_html_path.resolve()}\n")


def _maybe_promote_baseline(
    latest_dir: Path,
    baseline_dir: Path,
    logger: structlog.stdlib.BoundLogger,
    *,
    promote: bool,
) -> None:
    """Establish an initial baseline or promote candidate when requested."""
    is_initial_run = not baseline_dir.exists()
    if is_initial_run or promote:
        promoted = promote_to_baseline(
            latest_dir,
            baseline_dir,
            force=is_initial_run,
        )
        logger.info(
            "baseline_promotion_status",
            promoted=promoted,
            initial_run=is_initial_run,
            baseline_dir=str(baseline_dir),
        )


def run_benchmark_suite(  # ruff: ignore[too-many-arguments, too-many-positional-arguments]
    experiment: str = "flip_bitmap_unaligned",
    mode: str = "fast-screen",
    repetitions: int | None = None,
    benchmark_filter: str = "BM_FlipBitmap_.*",
    rope_fraction: float = _DEFAULT_ROPE_FRACTION,
    min_time: str | None = None,
    min_warmup_time: str | None = None,
    random_interleaving: bool = False,
    promote: bool = False,
    tune_host: bool = True,
    binary_path: Path | None = None,
) -> int:
    """Executes the Google Benchmark suite and performs Bayesian BEST analysis.

    Args:
        experiment: Name of the experiment directory under ``data/``.
        mode: ``'fast-screen'`` or ``'full-statistical'``.
        repetitions: Number of repetitions (defaults to 3 or 20 per mode).
        benchmark_filter: Regular expression filter for Google Benchmark.
        rope_fraction: Half-width of ROPE region (default 0.02 = 2%).
        min_time: Minimum benchmark duration per iteration (e.g. ``'0.1s'``).
        min_warmup_time: Warmup duration before timing begins (e.g. ``'0.02s'``).
        random_interleaving: Randomly interleave repetitions to cancel drift.
        promote: Whether to promote latest data to baseline if criteria are met.
        tune_host: Whether to apply host hardware/scheduler tuning.
        binary_path: Optional custom path to the benchmark binary.

    Returns:
        Exit code: ``0`` on success, non-zero otherwise.
    """
    _validate_rope_fraction(rope_fraction)
    logger = _get_logger()

    repo_root = Path(__file__).resolve().parent.parent.parent
    latest_dir = repo_root / "data" / "latest" / experiment
    latest_dir.mkdir(parents=True, exist_ok=True)

    reps, eff_min_time, eff_warmup = _resolve_mode_settings(
        mode,
        repetitions,
        min_time,
        min_warmup_time,
    )
    tuner = get_host_tuner() if tune_host else None
    host_meta: dict[str, Any] = (
        tuner.prepare() if tuner is not None else {"system": sys.platform}
    )
    _log_darwin_hardware(host_meta, logger)

    spec = _ExecutionSpec(
        repo_root=repo_root,
        binary=find_or_build_binary(binary_path),
        benchmark_filter=benchmark_filter,
        repetitions=reps,
        min_time=eff_min_time,
        warmup_seconds=eff_warmup.removesuffix("s"),
        raw_json_path=latest_dir / "benchmark_raw.json",
        random_interleaving=random_interleaving,
        tuner=tuner,
    )
    logger.info(
        "benchmark_started",
        mode=mode,
        repetitions=reps,
        min_time=eff_min_time,
        warmup=eff_warmup,
    )
    exec_code = _execute_gbench(spec, host_meta, logger)
    if exec_code != 0:
        return exec_code

    raw_series = parse_google_benchmark_json(spec.raw_json_path)
    if not raw_series:
        logger.warning("no_benchmarks_matched", benchmark_filter=benchmark_filter)
        return 1

    paired_samples, results = extract_paired_series(
        raw_series,
        rope_fraction=rope_fraction,
    )
    if not results:
        logger.warning("no_benchmark_pairs_found", benchmark_filter=benchmark_filter)
        return 1

    telemetry_meta: dict[str, Any] = {
        "experiment": experiment,
        "timestamp": datetime.now(UTC).isoformat(),
        "git_commit": _read_git_head_sha(),
        "host": host_meta,
        "config": {
            "mode": mode,
            "repetitions": reps,
            "min_time": eff_min_time,
            "min_warmup_time": eff_warmup,
            "random_interleaving": random_interleaving and reps > 1,
            "rope_percent": rope_fraction * _PERCENT_SCALE,
            "benchmark_filter": benchmark_filter,
        },
        "benchmarks": paired_samples,
    }
    _write_reports(latest_dir, telemetry_meta, results)
    _maybe_promote_baseline(
        latest_dir,
        repo_root / "data" / "baseline" / experiment,
        logger,
        promote=promote,
    )
    return 0


@dataclass(slots=True, frozen=True, kw_only=True)
class RunOptions:
    """CLI options for running the Google Benchmark + BEST suite."""

    experiment: Annotated[
        str,
        Parameter(name="--experiment", help="Experiment name."),
    ] = "flip_bitmap_unaligned"
    mode: Annotated[
        RunMode,
        Parameter(name="--mode", help="Execution mode."),
    ] = "fast-screen"
    fast_screen: Annotated[
        bool,
        Parameter(
            name="--fast-screen",
            negative=False,
            help="Shortcut for --mode fast-screen.",
        ),
    ] = False
    full: Annotated[
        bool,
        Parameter(
            name="--full",
            negative=False,
            help="Shortcut for --mode full-statistical.",
        ),
    ] = False
    repetitions: Annotated[
        int | None,
        Parameter(name="--repetitions", help="Repetitions count."),
    ] = None
    benchmark_filter: Annotated[
        str,
        Parameter(
            name="--benchmark-filter",
            alias="--filter",
            help="Filter regex (e.g. 1366x768 or BM_FlipBitmap_.*).",
        ),
    ] = "BM_FlipBitmap_.*"
    rope: Annotated[
        float,
        Parameter(name="--rope", help="ROPE fraction (default: 0.02)."),
    ] = _DEFAULT_ROPE_FRACTION
    min_time: Annotated[
        str | None,
        Parameter(name="--min-time", help="Google Benchmark min_time (e.g. 0.1s)."),
    ] = None
    min_warmup_time: Annotated[
        str | None,
        Parameter(
            name="--min-warmup-time",
            help="Google Benchmark warmup time (e.g. 0.02s).",
        ),
    ] = None
    interleave: Annotated[
        bool,
        Parameter(
            name="--interleave",
            negative=False,
            help="Enable random benchmark interleaving.",
        ),
    ] = False
    promote: Annotated[
        bool,
        Parameter(
            name="--promote",
            negative=False,
            help="Promote to baseline if statistical criteria met.",
        ),
    ] = False
    no_tune: Annotated[
        bool,
        Parameter(
            name="--no-tune",
            negative=False,
            help="Disable host tuning.",
        ),
    ] = False
    binary: Annotated[
        Path | None,
        Parameter(name="--binary", help="Path to benchmark binary."),
    ] = None

    def resolved_mode(self) -> RunMode:
        """Resolve effective benchmark mode from ``--mode`` / ``--full`` flags.

        Returns:
            Effective execution mode literal.

        Raises:
            ValueError: If both ``--fast-screen`` and ``--full`` are specified.
        """
        if self.fast_screen and self.full:
            msg = "Specify at most one of --fast-screen or --full"
            raise ValueError(msg)
        if self.full:
            return "full-statistical"
        if self.fast_screen:
            return "fast-screen"
        return self.mode


app = App(
    name="run-benchmark",
    help="Run Google Benchmark and Bayesian BEST analysis.",
    result_action="return_value",
)


@app.default
def _cli_run(options: Annotated[RunOptions | None, Parameter(name="*")] = None) -> int:
    """Invoke ``run_benchmark_suite`` with parsed ``RunOptions``.

    Args:
        options: Parsed CLI options dataclass.

    Returns:
        Exit code returned by ``run_benchmark_suite``.
    """
    resolved = options if options is not None else RunOptions()
    return run_benchmark_suite(
        experiment=resolved.experiment,
        mode=resolved.resolved_mode(),
        repetitions=resolved.repetitions,
        benchmark_filter=resolved.benchmark_filter,
        rope_fraction=resolved.rope,
        min_time=resolved.min_time,
        min_warmup_time=resolved.min_warmup_time,
        random_interleaving=resolved.interleave,
        promote=resolved.promote,
        tune_host=not resolved.no_tune,
        binary_path=resolved.binary,
    )


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entrypoint for ``run_benchmark``.

    Args:
        argv: Optional CLI argument sequence overriding ``sys.argv[1:]``.

    Returns:
        Integer process exit code (``0`` for success).
    """
    try:
        result = app(
            list(argv) if argv is not None else None,
            result_action="return_value",
        )
    except SystemExit as exc:
        code = exc.code
        return code if isinstance(code, int) else (0 if code is None else 1)
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    raise SystemExit(main())
