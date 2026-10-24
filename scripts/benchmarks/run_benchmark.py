"""Benchmark runner CLI with host tuning, Google Benchmark execution, and Bayesian analysis.

Executes Google Benchmark in either fast-screen mode or full-statistical mode, prepares
the host environment (taskpolicy, caffeinate, pmset on Darwin; taskset, cpupower, isolcpus on Linux),
runs Bayesian BEST comparison between baseline and candidate benchmarks, and generates reports.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import UTC
from datetime import datetime
from pathlib import Path
from typing import Any

from scripts.benchmarks.bayesian_compare import BestResult
from scripts.benchmarks.bayesian_compare import analyze_benchmark
from scripts.benchmarks.bayesian_compare import generate_markdown_report
from scripts.benchmarks.bayesian_compare import generate_plotly_html_report
from scripts.benchmarks.bayesian_compare import parse_google_benchmark_json
from scripts.benchmarks.bayesian_compare import promote_to_baseline
from scripts.benchmarks.bayesian_compare import serialize_results
from scripts.benchmarks.host_tuner import DarwinHostTuner
from scripts.benchmarks.host_tuner import HostTuner
from scripts.benchmarks.host_tuner import LinuxHostTuner
from scripts.benchmarks.host_tuner import get_host_tuner


# Re-export tuners for interface inspection
__all__ = [
    'DarwinHostTuner',
    'HostTuner',
    'LinuxHostTuner',
    'get_host_tuner',
    'run_benchmark_suite',
]


def find_or_build_binary(custom_path: Path | None = None) -> Path:
    """Finds the benchmark binary or builds it via cmake benchmark preset.

    Args:
        custom_path: Optional explicit binary path provided by the user.

    Returns:
        Absolute Path to the executable.

    Raises:
        FileNotFoundError: If binary cannot be found or built.
        PermissionError: If specified custom binary is not executable.
    """
    repo_root = Path(__file__).resolve().parent.parent.parent

    if custom_path is not None:
        resolved_custom = custom_path.resolve()
        if not resolved_custom.exists():
            msg = f'Specified benchmark binary not found: {custom_path}'
            raise FileNotFoundError(msg)
        if not os.access(resolved_custom, os.X_OK):
            msg = f'Specified benchmark binary is not executable: {custom_path}'
            raise PermissionError(msg)
        return resolved_custom

    # Candidate paths for benchmark executable
    candidates = [
        repo_root / 'build' / 'benchmark' / 'bin' / 'bitmap_flip_benchmark',
        repo_root / 'build' / 'benchmark' / 'benchmarks' / 'cpp' / 'systems' / 'bitmap_flip_benchmark',
    ]

    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return candidate.resolve()

    # Attempt to build via cmake
    cmake_bin = shutil.which('cmake')
    if cmake_bin:
        build_proc = subprocess.run(
            [cmake_bin, '--build', '--preset', 'benchmark'],
            cwd=repo_root,
            capture_output=True,
            text=True,
            check=False,
        )
        if build_proc.returncode == 0:
            for candidate in candidates:
                if candidate.exists() and os.access(candidate, os.X_OK):
                    return candidate.resolve()

    msg = f'Benchmark executable bitmap_flip_benchmark not found in {candidates}'
    raise FileNotFoundError(msg)


def extract_paired_series(
    raw_series: dict[str, list[float]],
    rope_fraction: float = 0.02,
) -> tuple[dict[str, dict[str, list[float]]], dict[str, BestResult]]:
    """Pairs baseline and candidate benchmark series by geometry and runs BEST analysis.

    Args:
        raw_series: Mapping from registered benchmark name to list of sample latencies.
        rope_fraction: Half-width of ROPE region (default 0.02 = 2%).

    Returns:
        Tuple of (paired_samples_dict, results_dict).

    Raises:
        ValueError: If rope_fraction is negative or >= 1.0.
    """
    if rope_fraction < 0.0:
        msg = f'rope_fraction must be non-negative (got {rope_fraction})'
        raise ValueError(msg)
    if rope_fraction >= 1.0:
        msg = f'rope_fraction must be less than 1.0 (got {rope_fraction})'
        raise ValueError(msg)

    paired_samples: dict[str, dict[str, list[float]]] = {}

    for name, samples in raw_series.items():
        if '_Baseline/' in name:
            geometry = name.split('_Baseline/', 1)[1]
            paired_samples.setdefault(geometry, {})['baseline'] = samples
        elif '_Candidate/' in name:
            geometry = name.split('_Candidate/', 1)[1]
            paired_samples.setdefault(geometry, {})['candidate'] = samples

    results: dict[str, BestResult] = {}
    for geometry, pair in paired_samples.items():
        base = pair.get('baseline', [])
        cand = pair.get('candidate', [])
        if len(base) >= 2 and len(cand) >= 2:
            results[geometry] = analyze_benchmark(base, cand, rope_fraction=rope_fraction)
        elif len(base) >= 1 and len(cand) >= 1:
            # Fallback for fast-screen with N=1 repetition: duplicate sample to compute delta
            pseudo_base = base if len(base) >= 2 else base + base
            pseudo_cand = cand if len(cand) >= 2 else cand + cand
            results[geometry] = analyze_benchmark(pseudo_base, pseudo_cand, rope_fraction=rope_fraction)

    return paired_samples, results


_BENIGN_DARWIN_GBENCH_PATTERNS: tuple[str, ...] = (
    'Unable to determine clock rate from sysctl: hw.cpufrequency:',
    'This does not affect benchmark measurements, only the metadata output.',
    '***WARNING*** Failed to set thread affinity. Estimated CPU frequency may be incorrect.',
)


def filter_apple_silicon_gbench_output(
    stderr_text: str,
    host_meta: dict[str, Any],
) -> str:
    """Filters expected Apple Silicon sysctl/affinity warnings and annotates the console header.

    On Apple Silicon, `hw.cpufrequency` does not exist in `sysctl` and Mach
    `THREAD_AFFINITY_POLICY` returns `KERN_NOT_SUPPORTED` (XNU binds threads to
    P-cores via `QOS_CLASS_USER_INTERACTIVE` instead), causing Google Benchmark's
    fallback estimator to read the 24 MHz ARM `cntvct_el0` reference crystal.

    Args:
        stderr_text: Raw stderr string from the Google Benchmark process.
        host_meta: Host telemetry dictionary from `HostTuner.prepare()`.

    Returns:
        Cleaned stderr string with accurate Apple Silicon clock annotation.
    """
    if host_meta.get('system') != 'Darwin':
        return stderr_text

    p_mhz = host_meta.get('pcore_max_mhz')
    filtered_lines: list[str] = []
    for line in stderr_text.splitlines():
        if any(pat in line for pat in _BENIGN_DARWIN_GBENCH_PATTERNS):
            continue
        if p_mhz and '24 MHz CPU s' in line:
            line = line.replace(
                '24 MHz CPU s',
                f'{p_mhz:.0f} MHz P-core max / 24 MHz ARM cntfrq_el0 timer CPU s',
            )
        filtered_lines.append(line)

    return '\n'.join(filtered_lines) + ('\n' if filtered_lines else '')


def patch_google_benchmark_json_context(
    json_path: Path,
    host_meta: dict[str, Any],
) -> None:
    """Enriches `benchmark_raw.json` context with true Apple Silicon clock and cache metadata.

    Args:
        json_path: Path to `benchmark_raw.json` written by Google Benchmark.
        host_meta: Host telemetry dictionary from `HostTuner.prepare()`.
    """
    if host_meta.get('system') != 'Darwin' or not json_path.exists():
        return
    try:
        payload = json.loads(json_path.read_text(encoding='utf-8'))
    except json.JSONDecodeError, OSError:
        return

    ctx = payload.get('context')
    if not isinstance(ctx, dict):
        return

    p_mhz = host_meta.get('pcore_max_mhz')
    if p_mhz is not None:
        if ctx.get('mhz_per_cpu') == 24:
            ctx['apple_cntfrq_el0_mhz'] = 24
        ctx['mhz_per_cpu'] = p_mhz
        ctx['apple_pcore_max_mhz'] = p_mhz
    e_mhz = host_meta.get('ecore_max_mhz')
    if e_mhz is not None:
        ctx['apple_ecore_max_mhz'] = e_mhz

    json_path.write_text(json.dumps(payload, indent=2), encoding='utf-8')


def run_benchmark_suite(
    experiment: str = 'flip_bitmap_unaligned',
    mode: str = 'fast-screen',
    repetitions: int | None = None,
    benchmark_filter: str = 'BM_FlipBitmap_.*',
    rope_fraction: float = 0.02,
    min_time: str | None = None,
    min_warmup_time: str | None = None,
    random_interleaving: bool = False,
    promote: bool = False,
    tune_host: bool = True,
    binary_path: Path | None = None,
) -> int:
    """Executes the Google Benchmark suite and analyzes results.

    Args:
        experiment: Name of the experiment directory.
        mode: 'fast-screen' or 'full-statistical'.
        repetitions: Number of repetitions (defaults to 3 for fast-screen, 20 for full).
        benchmark_filter: Regular expression filter for Google Benchmark.
        rope_fraction: Half-width of ROPE region (default 0.02 = 2%).
        min_time: Minimum benchmark duration per iteration (e.g. '0.1s' or '0.5s').
        min_warmup_time: Warmup duration before timing begins (e.g. '0.02s' or '0.05s').
        random_interleaving: Randomly interleave benchmark repetitions to cancel drift.
        promote: Whether to promote latest data to baseline if criteria met.
        tune_host: Whether to apply host hardware/scheduler tuning.
        binary_path: Optional custom path to benchmark binary.

    Returns:
        Exit code: 0 on success.

    Raises:
        ValueError: If rope_fraction is negative or >= 1.0.
    """
    repo_root = Path(__file__).resolve().parent.parent.parent
    data_dir = repo_root / 'data'
    latest_dir = data_dir / 'latest' / experiment
    baseline_dir = data_dir / 'baseline' / experiment
    latest_dir.mkdir(parents=True, exist_ok=True)

    if rope_fraction < 0.0:
        msg = f'rope_fraction must be non-negative (got {rope_fraction})'
        raise ValueError(msg)
    if rope_fraction >= 1.0:
        msg = f'rope_fraction must be less than 1.0 (got {rope_fraction})'
        raise ValueError(msg)

    if repetitions is None:
        repetitions = 3 if mode == 'fast-screen' else 20
    if min_time is None:
        min_time = '0.1s' if mode == 'fast-screen' else '0.5s'
    if min_warmup_time is None:
        min_warmup_time = '0.02' if mode == 'fast-screen' else '0.05'
    warmup_seconds = min_warmup_time.removesuffix('s')

    binary = find_or_build_binary(binary_path)

    tuner = get_host_tuner() if tune_host else None
    host_meta: dict[str, Any] = tuner.prepare() if tuner else {'system': sys.platform}

    if host_meta.get('system') == 'Darwin':
        brand = host_meta.get('cpu_brand', 'Apple Silicon')
        p_cores = host_meta.get('pcore_count', '?')
        e_cores = host_meta.get('ecore_count', '?')
        p_mhz = host_meta.get('pcore_max_mhz')
        e_mhz = host_meta.get('ecore_max_mhz')
        freq_summary = f'{p_cores} P-cores' + (f' @ {p_mhz:.1f} MHz' if p_mhz else '')
        freq_summary += f' + {e_cores} E-cores' + (f' @ {e_mhz:.1f} MHz' if e_mhz else '')
        print(f'==> Host Hardware: {brand} ({host_meta.get("machine", "arm64")}) | {freq_summary}')
        if 'pcore_l1d_kib' in host_meta:
            print(
                f'==> P-Core Cache:  {host_meta["pcore_l1d_kib"]} KiB L1D / '
                f'{host_meta.get("pcore_l1i_kib", "?")} KiB L1I / '
                f'{host_meta.get("pcore_l2_kib", "?")} KiB L2 '
                f'({host_meta.get("cacheline_bytes", 128)} B line)'
            )
        print(f'==> Scheduler QoS: {host_meta.get("qos", "QOS_CLASS_USER_INTERACTIVE")}')

    # Setup Google Benchmark CLI command
    raw_json_path = latest_dir / 'benchmark_raw.json'
    bm_cmd = [
        str(binary),
        f'--benchmark_filter={benchmark_filter}',
        f'--benchmark_repetitions={repetitions}',
        f'--benchmark_min_time={min_time}',
        f'--benchmark_min_warmup_time={warmup_seconds}',
        '--benchmark_format=console',
        f'--benchmark_out={raw_json_path}',
        '--benchmark_out_format=json',
    ]
    if random_interleaving and repetitions > 1:
        bm_cmd.append('--benchmark_enable_random_interleaving=true')

    if tuner:
        bm_cmd = tuner.wrap_command(bm_cmd)

    print(
        f'==> Running Google Benchmark [{mode} mode, {repetitions} reps, '
        f'min_time={min_time}, warmup={min_warmup_time}]...'
    )
    proc = subprocess.run(bm_cmd, cwd=repo_root, capture_output=True, text=True, check=False)
    cleaned_stderr = filter_apple_silicon_gbench_output(proc.stderr or '', host_meta)
    if cleaned_stderr:
        sys.stderr.write(cleaned_stderr)
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        print(f'Benchmark execution failed with code {proc.returncode}', file=sys.stderr)
        return proc.returncode

    patch_google_benchmark_json_context(raw_json_path, host_meta)

    # Check post-run thermal throttling
    if tuner:
        post_thermal = tuner.check_thermal_throttling()
        host_meta['post_thermal'] = post_thermal
        if post_thermal.get('throttled'):
            print('⚠️ WARNING: Host experienced thermal throttling during execution!', file=sys.stderr)
        tuner.restore()

    # Parse JSON output and perform Bayesian comparison
    raw_series = parse_google_benchmark_json(raw_json_path)
    if not raw_series:
        print(f'⚠️ No benchmarks matched filter: {benchmark_filter}', file=sys.stderr)
        return 1

    paired_samples, results = extract_paired_series(raw_series, rope_fraction=rope_fraction)
    if not results:
        print(f'⚠️ No benchmark pairs found for analysis (filter: {benchmark_filter})', file=sys.stderr)
        return 1

    # Git commit hash
    git_commit = 'unknown'
    try:
        git_proc = subprocess.run(
            ['git', 'rev-parse', 'HEAD'],
            capture_output=True,
            text=True,
            check=False,
        )
        if git_proc.returncode == 0:
            git_commit = git_proc.stdout.strip()
    except subprocess.SubprocessError, OSError:
        pass

    telemetry_meta: dict[str, Any] = {
        'experiment': experiment,
        'timestamp': datetime.now(UTC).isoformat(),
        'git_commit': git_commit,
        'host': host_meta,
        'config': {
            'mode': mode,
            'repetitions': repetitions,
            'min_time': min_time,
            'min_warmup_time': min_warmup_time,
            'random_interleaving': random_interleaving and repetitions > 1,
            'rope_percent': rope_fraction * 100.0,
            'benchmark_filter': benchmark_filter,
        },
        'benchmarks': paired_samples,
    }

    # Save structured telemetry and analysis
    (latest_dir / 'telemetry.json').write_text(json.dumps(telemetry_meta, indent=2), encoding='utf-8')
    (latest_dir / 'analysis.json').write_text(
        json.dumps({'meta': telemetry_meta['config'], 'benchmarks': serialize_results(results)}, indent=2),
        encoding='utf-8',
    )

    # Generate Markdown and interactive Plotly HTML reports
    report_md_path = latest_dir / 'report.md'
    report_html_path = latest_dir / 'report.html'
    md_content = generate_markdown_report(results, telemetry_meta, report_md_path)
    generate_plotly_html_report(results, paired_samples, report_html_path, meta=telemetry_meta)

    print('\n' + md_content)
    print(f'HTML Report saved to: file://{report_html_path.resolve()}')

    # Automatic initial baseline or explicit promotion
    is_initial_run = not baseline_dir.exists()
    if is_initial_run or promote:
        promoted = promote_to_baseline(latest_dir, baseline_dir, force=is_initial_run)
        if promoted:
            status_label = 'Initial baseline established' if is_initial_run else 'Promoted candidate to baseline'
            print(f'==> {status_label} at {baseline_dir}')
        else:
            print('==> Promotion rejected: candidate did not meet statistical criteria.')

    return 0


def main() -> int:
    """CLI entrypoint for run_benchmark.

    Returns:
        Integer exit code (0 for success).
    """
    parser = argparse.ArgumentParser(description='Run Google Benchmark and Bayesian BEST analysis.')
    parser.add_argument('--experiment', default='flip_bitmap_unaligned', help='Experiment name')
    parser.add_argument(
        '--mode',
        choices=['fast-screen', 'full-statistical'],
        default='fast-screen',
        help='Execution mode',
    )
    parser.add_argument('--fast-screen', action='store_const', dest='mode', const='fast-screen')
    parser.add_argument('--full', action='store_const', dest='mode', const='full-statistical')
    parser.add_argument('--repetitions', type=int, default=None, help='Repetitions count')
    parser.add_argument(
        '--benchmark-filter',
        '--filter',
        dest='benchmark_filter',
        default='BM_FlipBitmap_.*',
        help='Filter regex (e.g. 1366x768 or BM_FlipBitmap_.*)',
    )
    parser.add_argument('--rope', type=float, default=0.02, help='ROPE fraction (default: 0.02)')
    parser.add_argument('--min-time', default=None, help='Google Benchmark min_time (e.g. 0.1s)')
    parser.add_argument('--min-warmup-time', default=None, help='Google Benchmark warmup time (e.g. 0.02s)')
    parser.add_argument('--interleave', action='store_true', help='Enable random benchmark interleaving')
    parser.add_argument('--promote', action='store_true', help='Promote to baseline if statistical criteria met')
    parser.add_argument('--no-tune', action='store_true', help='Disable host tuning')
    parser.add_argument('--binary', type=Path, default=None, help='Path to benchmark binary')

    args = parser.parse_args()

    return run_benchmark_suite(
        experiment=args.experiment,
        mode=args.mode,
        repetitions=args.repetitions,
        benchmark_filter=args.benchmark_filter,
        rope_fraction=args.rope,
        min_time=args.min_time,
        min_warmup_time=args.min_warmup_time,
        random_interleaving=args.interleave,
        promote=args.promote,
        tune_host=not args.no_tune,
        binary_path=args.binary,
    )


if __name__ == '__main__':
    sys.exit(main())
