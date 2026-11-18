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
"""Bayesian performance comparison engine using Kruschke's BEST model and ROPE.

Performs robust Student-t Bayesian posterior estimation comparing microbenchmark
latency samples between baseline and candidate implementations, computes P(Better),
P(Same), P(Worse), effect sizes, ArviZ 95% HDIs/ETIs/ESS/MCSE diagnostics, Polars
summary tables, and interactive Plotly HTML and Markdown reports.
"""

import contextlib
import json
import math
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING, Annotated, Any, Final, Literal, Protocol, cast

import structlog
from cyclopts import App, Parameter
lazy import arviz_base as azb
lazy import numpy as np
lazy import plotly.graph_objects as _go  # pyright: ignore[reportMissingTypeStubs]
lazy import plotly.subplots as _subplots  # pyright: ignore[reportMissingTypeStubs]
lazy import polars as pl
lazy from arviz_stats.base.array import array_stats
lazy from scipy import stats


if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence

    from numpy.typing import NDArray


_MIN_SAMPLE_COUNT: Final[int] = 2
_MONTE_CARLO_SAMPLES: Final[int] = 50_000
_MONTE_CARLO_SEED: Final[int] = 20261016
_ZERO_DIV_EPS: Final[float] = 1e-12
_MAD_SCALE_FACTOR: Final[float] = 1.4826
_MIN_DF_PRIOR: Final[float] = 2.0
_MIN_SCALE_FLOOR: Final[float] = 1e-9
_PCT_SCALE: Final[float] = 100.0
_NS_PER_US: Final[float] = 1000.0
_DEFAULT_CRED_MASS: Final[float] = 0.95
_DEFAULT_ROPE_PCT: Final[float] = 2.0
_DEFAULT_REPS: Final[int] = 20

_PROMOTE_P_BETTER: Final[float] = 0.95
_PROMOTE_MAX_P_WORSE: Final[float] = 0.01
_EQUIV_P_SAME: Final[float] = 0.90
_REVERT_P_WORSE: Final[float] = 0.50
_POSSIBLE_REGRESSION_P_WORSE: Final[float] = 0.05

type PlotlyTemplate = Literal["plotly_white", "plotly_dark"]


def _get_logger(name: str = __name__) -> structlog.stdlib.BoundLogger:
    """Create a structlog logger strictly bound to ``sys.stderr``.

    Args:
        name: Logical logger identifier.

    Returns:
        Configured structlog logger emitting JSON lines to stderr.
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


_VALID_PLOTLY_TEMPLATES: Final[frozenset[str]] = frozenset({
    "plotly_white",
    "plotly_dark",
})

_VERDICT_ICONS: Final[frozendict[str, str]] = frozendict[str, str]() | {
    "PROCEED_PROMOTE": "🚀 Superior",
    "EQUIVALENT": "⚖️ Equivalent",
    "REVERT_REGRESSION": "❌ Regressed",
    "POSSIBLE_REGRESSION": "⚠️ Slower",
    "INCONCLUSIVE": "❓ Inconclusive",
}

_PLOTLY_COLORS: Final[frozendict[str, str]] = frozendict[str, str]() | {
    "baseline": "#4285F4",
    "candidate": "#34A853",
    "neutral": "#FBBC05",
    "regression": "#EA4335",
}


class _HdiCallable(Protocol):
    def __call__(
        self,
        ary: NDArray[np.float64],
        prob: float,
    ) -> NDArray[np.float64]: ...


class _ScalarStatCallable(Protocol):
    def __call__(self, ary: NDArray[np.float64]) -> float | NDArray[np.float64]: ...


class _PlotlyTrace(Protocol):
    pass


class _PlotlyFigure(Protocol):
    def add_trace(
        self,
        trace: _PlotlyTrace,
        row: int | None = None,
        col: int | None = None,
    ) -> _PlotlyFigure: ...

    def update_layout(
        self,
        *,
        title_text: str | None = None,
        barmode: str | None = None,
        height: int | None = None,
        showlegend: bool | None = None,
        template: str | None = None,
    ) -> _PlotlyFigure: ...

    def write_html(self, file: str, *, include_plotlyjs: bool = True) -> None: ...


class _PlotlyGoModule(Protocol):
    def Bar(  # ruff: ignore[invalid-function-name]
        self,
        *,
        name: str,
        x: Sequence[str],
        y: Sequence[float],
        marker_color: str | Sequence[str] | None = None,
        error_y: dict[str, object] | None = None,
    ) -> _PlotlyTrace: ...

    def Scatter(  # ruff: ignore[invalid-function-name]
        self,
        *,
        name: str,
        x: Sequence[int],
        y: Sequence[float],
        mode: str,
        line: dict[str, str] | None = None,
    ) -> _PlotlyTrace: ...


class _PlotlySubplotsModule(Protocol):
    def make_subplots(
        self,
        *,
        rows: int,
        cols: int,
        subplot_titles: tuple[str, ...],
        vertical_spacing: float,
        horizontal_spacing: float,
    ) -> _PlotlyFigure: ...


@dataclass(frozen=True, slots=True)
class BestResult:
    """Statistical outcome of Bayesian BEST + ROPE analysis.

    Attributes:
        p_better: Posterior probability candidate is faster than baseline by > ROPE.
        p_same: Posterior probability candidate is practically equivalent within ROPE.
        p_worse: Posterior probability candidate is slower than baseline by > ROPE.
        effect_size: Standardized effect size (Bayesian Cohen's d equivalent).
        hdi_95: 95% Highest Density Interval of relative latency difference.
        baseline_mean: Location parameter (mean latency) of baseline in ns.
        candidate_mean: Location parameter (mean latency) of candidate in ns.
        relative_diff_percent: Relative mean difference ((candidate - baseline) / baseline) * 100.
        verdict: Categorical decision ('PROCEED_PROMOTE', 'EQUIVALENT', 'REVERT_REGRESSION', 'INCONCLUSIVE').
    """

    p_better: float
    p_same: float
    p_worse: float
    effect_size: float
    hdi_95: tuple[float, float]
    baseline_mean: float
    candidate_mean: float
    relative_diff_percent: float
    verdict: str


# Backwards-compatible alias for survey report references
BayesianComparisonResult = BestResult


@dataclass(frozen=True, slots=True)
class PosteriorDiagnostics:
    """ArviZ posterior diagnostics for relative percentage latency difference.

    Attributes:
        hdi_95: 95% Highest Density Interval in percentage points.
        eti_95: 95% Equal-Tailed Interval in percentage points.
        ess: Bulk effective sample size estimated via ArviZ.
        mcse: Monte Carlo standard error of posterior mean estimated via ArviZ.
    """

    hdi_95: tuple[float, float]
    eti_95: tuple[float, float]
    ess: float
    mcse: float


def compute_hdi(
    samples: NDArray[np.float64] | Sequence[float],
    cred_mass: float = _DEFAULT_CRED_MASS,
) -> tuple[float, float]:
    """Compute the Highest Density Interval (HDI) containing ``cred_mass`` of mass.

    Uses ``arviz_stats.base.array.array_stats.hdi`` after validating bounds.

    Args:
        samples: 1D sequence or array of posterior samples.
        cred_mass: Credible mass fraction in ``(0.0, 1.0]`` (default 0.95).

    Returns:
        Tuple of ``(hdi_low, hdi_high)``.

    Raises:
        ValueError: If ``samples`` is empty or ``cred_mass`` is outside ``(0.0, 1.0]``.
    """
    if not 0.0 < cred_mass <= 1.0:
        msg = f"cred_mass must be in (0.0, 1.0] (got {cred_mass})"
        raise ValueError(msg)

    arr = np.asarray(samples, dtype=np.float64).ravel()
    if arr.size == 0:
        msg = "samples must not be empty"
        raise ValueError(msg)
    if arr.size == 1 or cred_mass >= 1.0:
        return (float(np.min(arr)), float(np.max(arr)))

    hdi_fn = cast("_HdiCallable", array_stats.hdi)
    bounds = np.asarray(hdi_fn(arr, cred_mass), dtype=np.float64).ravel()
    low = float(bounds[0])
    high = float(bounds[1])
    return (min(low, high), max(low, high))


def compute_arviz_diagnostics(
    delta_samples: NDArray[np.float64] | Sequence[float],
    cred_mass: float = _DEFAULT_CRED_MASS,
) -> PosteriorDiagnostics:
    """Compute ArviZ ``DataTree`` posterior diagnostics for relative deltas.

    Constructs an ArviZ ``DataTree`` via ``arviz_base.from_dict`` on the percentage
    difference chain and computes HDI, ETI, bulk ESS, and MCSE via ``arviz_stats``.

    Args:
        delta_samples: Fractional difference draws ``(mu_cand - mu_base) / mu_base``.
        cred_mass: Credible mass fraction in ``(0.0, 1.0]`` (default 0.95).

    Returns:
        ``PosteriorDiagnostics`` with percentage HDI, ETI, ESS, and MCSE.

    Raises:
        ValueError: If ``delta_samples`` is empty or ``cred_mass`` is invalid.
    """
    if not 0.0 < cred_mass <= 1.0:
        msg = f"cred_mass must be in (0.0, 1.0] (got {cred_mass})"
        raise ValueError(msg)

    pct_draws = np.asarray(delta_samples, dtype=np.float64).ravel() * _PCT_SCALE
    if pct_draws.size == 0:
        msg = "delta_samples must not be empty"
        raise ValueError(msg)

    dt = azb.from_dict({"posterior": {"delta_pct": pct_draws[None, :]}})
    chain_arr = np.asarray(dt["posterior"]["delta_pct"].to_numpy(), dtype=np.float64)

    hdi_bounds = compute_hdi(chain_arr[0], cred_mass=cred_mass)
    if chain_arr.shape[1] == 1 or cred_mass >= 1.0:
        eti_bounds = hdi_bounds
    else:
        eti_fn = cast("_HdiCallable", array_stats.eti)
        eti_raw = np.asarray(eti_fn(chain_arr[0], cred_mass), dtype=np.float64).ravel()
        eti_bounds = (
            min(float(eti_raw[0]), float(eti_raw[1])),
            max(float(eti_raw[0]), float(eti_raw[1])),
        )

    ess_fn = cast("_ScalarStatCallable", array_stats.ess)
    mcse_fn = cast("_ScalarStatCallable", array_stats.mcse)
    ess_val = float(np.asarray(ess_fn(chain_arr)).item())
    mcse_val = float(np.asarray(mcse_fn(chain_arr)).item())
    return PosteriorDiagnostics(
        hdi_95=hdi_bounds,
        eti_95=eti_bounds,
        ess=ess_val if math.isfinite(ess_val) else float(chain_arr.shape[1]),
        mcse=mcse_val if math.isfinite(mcse_val) else 0.0,
    )


def _fit_student_t(y: NDArray[np.float64]) -> tuple[float, float, float]:
    """Robustly fits Student-t distribution (df, loc, scale) using standardized data.

    Standardizes by median and median absolute deviation (MAD) to guarantee numerical
    stability across all magnitude scales (e.g. nanoseconds to milliseconds).

    Args:
        y: 1D array of latency measurements.

    Returns:
        Tuple of (df, loc, scale).
    """
    n = len(y)
    shift = float(np.median(y))
    mad = float(np.median(np.abs(y - shift)))
    spread = _MAD_SCALE_FACTOR * mad if mad > 0.0 else (float(np.std(y, ddof=1)) or 1.0)
    min_scale = _MIN_SCALE_FLOOR * max(abs(shift), 1.0)

    z = (y - shift) / spread
    try:
        df, loc_z, scale_z = stats.t.fit(z)
        loc = float(loc_z * spread + shift)
        scale = max(float(scale_z * spread), min_scale)
        df = max(float(df), 1.0)
    except RuntimeError, ValueError:
        loc = shift
        scale = max(spread, min_scale)
        df = max(float(n - 1), 1.0)

    return df, loc, scale


def _sample_relative_deltas(
    y1: NDArray[np.float64],
    y2: NDArray[np.float64],
) -> tuple[NDArray[np.float64], float, float, float, float]:
    """Fit baseline/candidate Student-t models and draw relative posterior deltas.

    Args:
        y1: Baseline latency samples.
        y2: Candidate latency samples.

    Returns:
        Tuple of ``(delta_draws, loc1, loc2, scale1, scale2)``.
    """
    min_scale = _MIN_SCALE_FLOOR * max(
        float(np.abs(np.mean(y1))),
        float(np.abs(np.mean(y2))),
        1.0,
    )

    df1, loc1, scale1 = _fit_student_t(y1)
    df2, loc2, scale2 = _fit_student_t(y2)

    se1 = max(
        math.sqrt((scale1**2 / len(y1)) * ((df1 + 3.0) / (df1 + 1.0))),
        min_scale,
    )
    se2 = max(
        math.sqrt((scale2**2 / len(y2)) * ((df2 + 3.0) / (df2 + 1.0))),
        min_scale,
    )

    rng = np.random.default_rng(_MONTE_CARLO_SEED)
    post_mu1 = np.asarray(
        stats.t.rvs(
            df=max(df1, _MIN_DF_PRIOR),
            loc=loc1,
            scale=se1,
            size=_MONTE_CARLO_SAMPLES,
            random_state=rng,
        ),
        dtype=np.float64,
    )
    post_mu2 = np.asarray(
        stats.t.rvs(
            df=max(df2, _MIN_DF_PRIOR),
            loc=loc2,
            scale=se2,
            size=_MONTE_CARLO_SAMPLES,
            random_state=rng,
        ),
        dtype=np.float64,
    )

    post_mu1_safe = np.where(
        np.abs(post_mu1) < _ZERO_DIV_EPS,
        _ZERO_DIV_EPS,
        post_mu1,
    )
    delta = (post_mu2 - post_mu1_safe) / post_mu1_safe
    return delta, loc1, loc2, scale1, scale2


def _compute_rope_probabilities(
    delta: NDArray[np.float64],
    rope_fraction: float,
) -> tuple[float, float, float]:
    """Compute normalized ``(p_better, p_same, p_worse)`` over ROPE bounds.

    Args:
        delta: Posterior draws of relative latency change.
        rope_fraction: Half-width of practical equivalence interval.

    Returns:
        Normalized tuple ``(p_better, p_same, p_worse)`` summing to 1.0.
    """
    raw_p_better = float(np.mean(delta < -rope_fraction))
    raw_p_same = float(np.mean((delta >= -rope_fraction) & (delta <= rope_fraction)))
    raw_p_worse = float(np.mean(delta > rope_fraction))

    prob_sum = raw_p_better + raw_p_same + raw_p_worse
    if prob_sum > 0.0:
        return (
            raw_p_better / prob_sum,
            raw_p_same / prob_sum,
            raw_p_worse / prob_sum,
        )
    return 0.0, 1.0, 0.0


def _determine_verdict(p_better: float, p_same: float, p_worse: float) -> str:
    """Map posterior probabilities to the categorical decision verdict.

    Args:
        p_better: Posterior probability candidate beats ROPE.
        p_same: Posterior probability candidate lies within ROPE.
        p_worse: Posterior probability candidate regresses beyond ROPE.

    Returns:
        Verdict string identifier.
    """
    if p_better >= _PROMOTE_P_BETTER and p_worse <= _PROMOTE_MAX_P_WORSE:
        return "PROCEED_PROMOTE"
    if p_same >= _EQUIV_P_SAME and p_worse <= _PROMOTE_MAX_P_WORSE:
        return "EQUIVALENT"
    if p_worse > _REVERT_P_WORSE:
        return "REVERT_REGRESSION"
    if p_worse > _POSSIBLE_REGRESSION_P_WORSE:
        return "POSSIBLE_REGRESSION"
    return "INCONCLUSIVE"


def analyze_benchmark(
    baseline_samples: NDArray[np.float64] | Sequence[float],
    candidate_samples: NDArray[np.float64] | Sequence[float],
    rope_fraction: float = 0.02,
) -> BestResult:
    """Analyzes benchmark latency samples using Bayesian BEST (Student-t) with ROPE.

    Computes posterior distributions for baseline and candidate execution times,
    evaluating relative difference ``Delta = (candidate - baseline) / baseline``.
    Because lower latency is faster (better):

    - ``P(Better)``: ``Delta < -rope_fraction``
    - ``P(Same)``: ``-rope_fraction <= Delta <= rope_fraction``
    - ``P(Worse)``: ``Delta > rope_fraction``

    Probabilities are normalized to guarantee ``sum(P) == 1.0``.

    Args:
        baseline_samples: Baseline latency samples (e.g. in nanoseconds).
        candidate_samples: Candidate latency samples (e.g. in nanoseconds).
        rope_fraction: Region of Practical Equivalence half-width (e.g. 0.02 for 2%).

    Returns:
        BestResult with posterior probabilities, effect size, HDI, and verdict.

    Raises:
        ValueError: If sample sizes are < 2 or rope_fraction is negative.
    """
    if rope_fraction < 0.0:
        msg = f"rope_fraction must be non-negative (got {rope_fraction})"
        raise ValueError(msg)

    y1 = np.asarray(baseline_samples, dtype=np.float64)
    y2 = np.asarray(candidate_samples, dtype=np.float64)
    if len(y1) < _MIN_SAMPLE_COUNT or len(y2) < _MIN_SAMPLE_COUNT:
        msg = (
            "Sample sizes must be >= 2 "
            f"(got n_baseline={len(y1)}, n_candidate={len(y2)})"
        )
        raise ValueError(msg)

    delta, loc1, loc2, scale1, scale2 = _sample_relative_deltas(y1, y2)
    p_better, p_same, p_worse = _compute_rope_probabilities(delta, rope_fraction)
    pooled_scale = math.sqrt((scale1**2 + scale2**2) / 2.0)

    return BestResult(
        p_better=p_better,
        p_same=p_same,
        p_worse=p_worse,
        effect_size=(loc2 - loc1) / pooled_scale if pooled_scale > 0.0 else 0.0,
        hdi_95=compute_arviz_diagnostics(delta, cred_mass=_DEFAULT_CRED_MASS).hdi_95,
        baseline_mean=loc1,
        candidate_mean=loc2,
        relative_diff_percent=float(np.mean(delta) * _PCT_SCALE),
        verdict=_determine_verdict(p_better, p_same, p_worse),
    )


# Backwards-compatible alias for survey report references
compute_best_comparison = analyze_benchmark


def parse_google_benchmark_json(json_path: Path) -> dict[str, list[float]]:
    """Parses Google Benchmark JSON v2 output into series of repetition times.

    Filters out aggregate summary rows (mean, median, stddev) to retain only raw runs.

    Args:
        json_path: Path to ``benchmark_raw.json``.

    Returns:
        Mapping of benchmark registration name to list of execution CPU times (ns).

    Raises:
        FileNotFoundError: If the json file does not exist.
        ValueError: If JSON format is invalid or lacks ``benchmarks``.
    """
    if not json_path.exists():
        msg = f"Benchmark output file not found: {json_path}"
        raise FileNotFoundError(msg)

    content = json_path.read_text(encoding="utf-8")
    if not content.strip():
        return {}

    try:
        raw_obj: object = json.loads(content)
    except json.JSONDecodeError as err:
        msg = f"Corrupt JSON in {json_path}: {err}"
        raise ValueError(msg) from err

    if not isinstance(raw_obj, dict):
        msg = f'Missing "benchmarks" array in {json_path}'
        raise ValueError(msg)

    data = cast("dict[str, Any]", raw_obj)
    raw_benchmarks: object = data.get("benchmarks")
    if not isinstance(raw_benchmarks, list):
        msg = f'Missing "benchmarks" array in {json_path}'
        raise ValueError(msg)

    series: dict[str, list[float]] = {}
    for raw_entry in cast("list[object]", raw_benchmarks):
        if not isinstance(raw_entry, dict):
            continue
        entry = cast("dict[str, Any]", raw_entry)
        if entry.get("run_type", "iteration") == "aggregate":
            continue

        name = str(entry.get("name", ""))
        clean_name = (
            name.split("/repeats:", maxsplit=1)[0] if "/repeats:" in name else name
        )
        time_ns = float(entry.get("cpu_time", entry.get("real_time", 0.0)))
        series.setdefault(clean_name, []).append(time_ns)

    return series


def summarize_samples_dataframe(
    raw_series: Mapping[str, Sequence[float]],
) -> pl.DataFrame:
    """Aggregate raw benchmark sample series into a typed Polars summary DataFrame.

    Args:
        raw_series: Mapping from benchmark registration name to sample latencies (ns).

    Returns:
        Sorted ``polars.DataFrame`` with per-benchmark count, mean, median, std, min,
        max, and IQR statistics.
    """
    empty_schema: dict[str, pl.DataType] = {
        "benchmark": pl.String(),
        "n_samples": pl.UInt32(),
        "mean_ns": pl.Float64(),
        "median_ns": pl.Float64(),
        "std_ns": pl.Float64(),
        "min_ns": pl.Float64(),
        "max_ns": pl.Float64(),
        "iqr_ns": pl.Float64(),
    }
    rows: list[dict[str, str | float]] = [
        {"benchmark": name, "time_ns": float(sample)}
        for name, samples in raw_series.items()
        for sample in samples
    ]
    if not rows:
        return pl.DataFrame(schema=empty_schema)

    raw_df = pl.DataFrame(
        rows,
        schema={"benchmark": pl.String(), "time_ns": pl.Float64()},
    )
    return (
        raw_df
        .group_by("benchmark")
        .agg(
            pl.len().cast(pl.UInt32()).alias("n_samples"),
            pl.col("time_ns").mean().alias("mean_ns"),
            pl.col("time_ns").median().alias("median_ns"),
            pl.col("time_ns").std().fill_null(0.0).alias("std_ns"),
            pl.col("time_ns").min().alias("min_ns"),
            pl.col("time_ns").max().alias("max_ns"),
            (pl.col("time_ns").quantile(0.75) - pl.col("time_ns").quantile(0.25)).alias(
                "iqr_ns"
            ),
        )
        .sort("benchmark")
    )


def results_to_dataframe(benchmark_results: Mapping[str, BestResult]) -> pl.DataFrame:
    """Convert a mapping of ``BestResult`` outcomes into a Polars DataFrame.

    Args:
        benchmark_results: Mapping from benchmark geometry name to ``BestResult``.

    Returns:
        Sorted ``polars.DataFrame`` containing all Bayesian comparison columns.
    """
    schema: dict[str, pl.DataType] = {
        "geometry": pl.String(),
        "baseline_mean": pl.Float64(),
        "candidate_mean": pl.Float64(),
        "relative_diff_percent": pl.Float64(),
        "hdi_95_low": pl.Float64(),
        "hdi_95_high": pl.Float64(),
        "p_better": pl.Float64(),
        "p_same": pl.Float64(),
        "p_worse": pl.Float64(),
        "effect_size": pl.Float64(),
        "verdict": pl.String(),
    }
    if not benchmark_results:
        return pl.DataFrame(schema=schema)

    records = [
        {
            "geometry": name,
            "baseline_mean": res.baseline_mean,
            "candidate_mean": res.candidate_mean,
            "relative_diff_percent": res.relative_diff_percent,
            "hdi_95_low": res.hdi_95[0],
            "hdi_95_high": res.hdi_95[1],
            "p_better": res.p_better,
            "p_same": res.p_same,
            "p_worse": res.p_worse,
            "effect_size": res.effect_size,
            "verdict": res.verdict,
        }
        for name, res in benchmark_results.items()
    ]
    return pl.DataFrame(records, schema=schema).sort("geometry")


def _extract_report_metadata(meta: Mapping[str, Any]) -> tuple[float, int]:
    """Resolves ``rope_percent`` and ``repetitions`` from top-level or nested config.

    Args:
        meta: Telemetry metadata dictionary.

    Returns:
        Tuple of ``(rope_percent, repetitions)``.
    """
    raw_cfg: object = meta.get("config")
    cfg: dict[str, Any] = (
        cast("dict[str, Any]", raw_cfg) if isinstance(raw_cfg, dict) else {}
    )

    rope_pct = meta.get("rope_percent") or cfg.get("rope_percent")
    if rope_pct is None:
        raw_frac = meta.get("rope_fraction")
        if raw_frac is None:
            raw_frac = cfg.get("rope_fraction")
        rope_pct = (
            float(raw_frac) * _PCT_SCALE if raw_frac is not None else _DEFAULT_ROPE_PCT
        )

    reps = meta.get("repetitions")
    if reps is None:
        reps = cfg.get("repetitions")
    if reps is None:
        reps = _DEFAULT_REPS

    return float(rope_pct), int(reps)


def _format_host_label(meta: Mapping[str, Any]) -> str:
    """Format human-readable host label from telemetry metadata.

    Args:
        meta: Telemetry metadata dictionary.

    Returns:
        Formatted host description string.
    """
    raw_host: object = meta.get("host", {})
    host_info: dict[str, Any] = (
        cast("dict[str, Any]", raw_host) if isinstance(raw_host, dict) else {}
    )
    sys_name = str(host_info.get("system", "Darwin"))
    machine = str(host_info.get("machine", "arm64"))
    cpu_brand = host_info.get("cpu_brand")
    p_mhz = host_info.get("pcore_max_mhz")
    host_label = f"{sys_name} {machine}"
    if cpu_brand:
        freq_note = f" @ {float(p_mhz):.0f} MHz P-core" if p_mhz else ""
        host_label += f" ({cpu_brand}{freq_note})"
    return host_label


def _resolve_overall_verdict(
    benchmark_results: Mapping[str, BestResult],
) -> tuple[str, str]:
    """Resolve overall status headline and recommendation for Markdown summary.

    Args:
        benchmark_results: Mapping from benchmark geometry name to ``BestResult``.

    Returns:
        Tuple of ``(overall_status, recommendation)``.
    """
    has_promotion = any(
        res.verdict == "PROCEED_PROMOTE" for res in benchmark_results.values()
    )
    has_regression = any(
        res.verdict in {"REVERT_REGRESSION", "POSSIBLE_REGRESSION"}
        for res in benchmark_results.values()
    )
    if has_regression:
        return (
            "⚠️ **REGRESSION DETECTED: Candidate Slower than Baseline**",
            "**REVERT**: Discard changes; performance regressed.",
        )
    if has_promotion:
        return (
            "🚀 **PROCEED: Candidate is Statistically Superior**",
            "**PROMOTE**: Candidate improved performance without regressions.",
        )
    return (
        "⚖️ **EQUIVALENT: Candidate Matches Baseline within ROPE**",
        "**PRESERVE**: Code is functionally equivalent with no speedup.",
    )


def _format_latency_ns(latency_ns: float) -> str:
    """Format latency in nanoseconds or microseconds for Markdown tables.

    Args:
        latency_ns: Latency value in nanoseconds.

    Returns:
        Formatted string with ``ns`` or ``us`` unit.
    """
    if latency_ns < _NS_PER_US:
        return f"{latency_ns:.1f} ns"
    return f"{latency_ns / _NS_PER_US:.2f} us"


def _format_markdown_table_row(row: Mapping[str, Any]) -> str:
    """Format a single Polars summary row as a Markdown table line.

    Args:
        row: Named row mapping produced by ``results_to_dataframe``.

    Returns:
        Formatted Markdown table row string.
    """
    verdict = str(row["verdict"])
    verdict_icon = _VERDICT_ICONS.get(verdict, verdict)
    return (
        f"| `{row['geometry']}` "
        f"| {_format_latency_ns(float(row['baseline_mean']))} "
        f"| {_format_latency_ns(float(row['candidate_mean']))} "
        f"| **{float(row['relative_diff_percent']):+.2f}%** "
        f"| `[{float(row['hdi_95_low']):.1f}%, {float(row['hdi_95_high']):.1f}%]` "
        f"| **{float(row['p_better']):.4f}** "
        f"| {float(row['p_same']):.4f} "
        f"| {float(row['p_worse']):.4f} "
        f"| {verdict_icon} |"
    )


def generate_markdown_report(
    benchmark_results: dict[str, BestResult],
    meta: dict[str, Any],
    output_path: Path,
) -> str:
    """Generates GitHub-flavored Markdown report in ``data/latest/<experiment>/report.md``.

    Args:
        benchmark_results: Mapping from benchmark geometry name to ``BestResult``.
        meta: Metadata dictionary including host, commit, ROPE, and repetitions.
        output_path: Path where ``report.md`` will be saved.

    Returns:
        Generated Markdown string.
    """
    exp_name = str(meta.get("experiment", "bitmap_flip"))
    commit = str(meta.get("git_commit", "unknown"))[:7]
    date_str = str(meta.get("timestamp", datetime.now(UTC).isoformat()))
    rope_pct, reps = _extract_report_metadata(meta)
    overall_status, recommendation = _resolve_overall_verdict(benchmark_results)

    lines: list[str] = [
        f"# Benchmark Analysis: {exp_name}",
        "",
        f"**Status:** {overall_status}  ",
        (
            f"**Date:** {date_str} | **Commit:** `{commit}` | "
            f"**ROPE:** $\\pm {rope_pct:.1f}\\%$ | **Reps:** {reps}  "
        ),
        f"**Host:** {_format_host_label(meta)} | **QoS:** User-Interactive  ",
        "",
        (
            "| Geometry | Baseline Mean | Candidate Mean | Rel Diff | 95% HDI | "
            "P(Better) | P(Same) | P(Worse) | Verdict |"
        ),
        "| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |",
        *(
            _format_markdown_table_row(row)
            for row in results_to_dataframe(benchmark_results).iter_rows(named=True)
        ),
        "",
        "### Decision Summary",
        f"- **Recommendation:** {recommendation}",
        (
            f"- **Evaluated Geometries:** {len(benchmark_results)} "
            "registered benchmark pairs."
        ),
        "",
    ]

    report_content = "\n".join(lines)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(report_content, encoding="utf-8")
    return report_content


def _populate_plotly_figure(
    fig: _PlotlyFigure,
    go: _PlotlyGoModule,
    benchmark_results: Mapping[str, BestResult],
    raw_series: Mapping[str, Mapping[str, list[float]]],
) -> None:
    """Populate all four Plotly report subplots with BEST and trace series.

    Args:
        fig: Target Plotly figure.
        go: Typed Plotly ``graph_objects`` module proxy.
        benchmark_results: Mapping from geometry name to ``BestResult``.
        raw_series: Raw latency sample traces keyed by geometry and variant.
    """
    geometries = list(benchmark_results.keys())
    names = [g.replace("BM_FlipBitmap_", "") for g in geometries]

    fig.add_trace(
        go.Bar(
            name="Baseline",
            x=names,
            y=[benchmark_results[g].baseline_mean for g in geometries],
            marker_color=_PLOTLY_COLORS["baseline"],
        ),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Bar(
            name="Candidate",
            x=names,
            y=[benchmark_results[g].candidate_mean for g in geometries],
            marker_color=_PLOTLY_COLORS["candidate"],
        ),
        row=1,
        col=1,
    )

    diffs = [benchmark_results[g].relative_diff_percent for g in geometries]
    err_low = [
        diffs[idx] - benchmark_results[geo].hdi_95[0]
        for idx, geo in enumerate(geometries)
    ]
    err_high = [
        benchmark_results[geo].hdi_95[1] - diffs[idx]
        for idx, geo in enumerate(geometries)
    ]
    diff_colors = [
        _PLOTLY_COLORS["regression"] if diff > 0.0 else _PLOTLY_COLORS["candidate"]
        for diff in diffs
    ]
    fig.add_trace(
        go.Bar(
            name="Rel Diff (%)",
            x=names,
            y=diffs,
            error_y={
                "type": "data",
                "symmetric": False,
                "array": err_high,
                "arrayminus": err_low,
            },
            marker_color=diff_colors,
        ),
        row=1,
        col=2,
    )

    fig.add_trace(
        go.Bar(
            name="P(Better)",
            x=names,
            y=[benchmark_results[g].p_better for g in geometries],
            marker_color=_PLOTLY_COLORS["candidate"],
        ),
        row=2,
        col=1,
    )
    fig.add_trace(
        go.Bar(
            name="P(Same)",
            x=names,
            y=[benchmark_results[g].p_same for g in geometries],
            marker_color=_PLOTLY_COLORS["neutral"],
        ),
        row=2,
        col=1,
    )
    fig.add_trace(
        go.Bar(
            name="P(Worse)",
            x=names,
            y=[benchmark_results[g].p_worse for g in geometries],
            marker_color=_PLOTLY_COLORS["regression"],
        ),
        row=2,
        col=1,
    )

    target_geo = next((g for g in geometries if "1366x768" in g), geometries[0])
    target_data = raw_series.get(target_geo, {})
    base_trace = target_data.get("baseline", [])
    cand_trace = target_data.get("candidate", [])
    if base_trace:
        fig.add_trace(
            go.Scatter(
                name="Baseline Trace",
                x=list(range(1, len(base_trace) + 1)),
                y=base_trace,
                mode="lines+markers",
                line={"color": _PLOTLY_COLORS["baseline"]},
            ),
            row=2,
            col=2,
        )
    if cand_trace:
        fig.add_trace(
            go.Scatter(
                name="Candidate Trace",
                x=list(range(1, len(cand_trace) + 1)),
                y=cand_trace,
                mode="lines+markers",
                line={"color": _PLOTLY_COLORS["candidate"]},
            ),
            row=2,
            col=2,
        )


def generate_plotly_html_report(
    benchmark_results: dict[str, BestResult],
    raw_series: dict[str, dict[str, list[float]]],
    output_path: Path,
    meta: dict[str, Any] | None = None,
    template: PlotlyTemplate = "plotly_white",
) -> None:
    """Generates standalone interactive Plotly HTML report with explicit theme template.

    Args:
        benchmark_results: Mapping from benchmark geometry name to ``BestResult``.
        raw_series: Mapping from geometry name to ``{'baseline': [...], 'candidate': [...]}``.
        output_path: Path where ``report.html`` will be saved.
        meta: Optional metadata dictionary with host, repetitions, and ROPE configuration.
        template: Plotly figure theme template (``plotly_white`` or ``plotly_dark``).

    Raises:
        ValueError: If ``template`` is not one of ``plotly_white`` or ``plotly_dark``.
    """
    if template not in _VALID_PLOTLY_TEMPLATES:
        msg = f"Unsupported Plotly template {template!r}; expected one of {sorted(_VALID_PLOTLY_TEMPLATES)}"
        raise ValueError(msg)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    rope_str = ""
    if meta is not None:
        rope_pct, reps = _extract_report_metadata(meta)
        rope_str = f" (ROPE: ±{rope_pct:.1f}%, Reps: {reps})"

    if not benchmark_results:
        empty_html = (
            f"<html><body><h1>Benchmark Report{rope_str}</h1>"
            "<p>No benchmarks analyzed.</p></body></html>"
        )
        output_path.write_text(empty_html, encoding="utf-8")
        return

    go = cast("_PlotlyGoModule", _go)
    subplots = cast("_PlotlySubplotsModule", _subplots)
    fig = subplots.make_subplots(
        rows=2,
        cols=2,
        subplot_titles=(
            "Mean Latency Comparison by Geometry (ns)",
            "Relative Latency Difference (%) & 95% HDI",
            "Posterior Probabilities: P(Better), P(Same), P(Worse)",
            "Repetition Latency Trace (Canonical Geometry)",
        ),
        vertical_spacing=0.15,
        horizontal_spacing=0.12,
    )
    _populate_plotly_figure(fig, go, benchmark_results, raw_series)
    fig.update_layout(
        title_text=f"Bayesian BEST Performance Analysis Report{rope_str}",
        barmode="group",
        height=900,
        showlegend=True,
        template=template,
    )
    fig.write_html(str(output_path), include_plotlyjs=True)


def _read_git_head_sha() -> str:
    """Resolve current git HEAD commit SHA, returning ``'unknown'`` on failure.

    Returns:
        Git commit SHA string or ``'unknown'``.
    """
    with contextlib.suppress(subprocess.SubprocessError, OSError):
        proc = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        if proc.returncode == 0 and proc.stdout.strip():
            return proc.stdout.strip()
    return "unknown"


def promote_to_baseline(
    latest_dir: Path,
    baseline_dir: Path,
    force: bool = False,
) -> bool:
    """Promotes latest experiment data to baseline if Bayesian criteria are satisfied.

    Promotion requires:
    1. ``latest_dir`` exists.
    2. If ``baseline_dir`` already exists:
       - No regressions across any geometry (``P(Worse) <= 0.05``).
       - No ``REVERT_REGRESSION`` verdict, unless ``force=True``.

    Args:
        latest_dir: Path to ``data/latest/<experiment>``.
        baseline_dir: Path to ``data/baseline/<experiment>``.
        force: If True, bypass statistical checks.

    Returns:
        True if promoted successfully, False otherwise.

    Raises:
        FileNotFoundError: If ``latest_dir`` does not exist.
    """
    if not latest_dir.exists():
        msg = f"Source latest directory {latest_dir} does not exist"
        raise FileNotFoundError(msg)

    logger = _get_logger()
    analysis_file = latest_dir / "analysis.json"
    if not force and analysis_file.exists() and baseline_dir.exists():
        with contextlib.suppress(json.JSONDecodeError, KeyError, TypeError):
            raw_obj: object = json.loads(analysis_file.read_text(encoding="utf-8"))
            raw_benchmarks: object = (
                cast("dict[str, Any]", raw_obj).get("benchmarks", {})
                if isinstance(raw_obj, dict)
                else {}
            )
            benchmarks: dict[str, Any] = (
                cast("dict[str, Any]", raw_benchmarks)
                if isinstance(raw_benchmarks, dict)
                else {}
            )
            for item in benchmarks.values():
                if not isinstance(item, dict):
                    continue
                item_dict = cast("dict[str, Any]", item)
                if (
                    item_dict.get("verdict") == "REVERT_REGRESSION"
                    or float(item_dict.get("p_worse", 0.0))
                    > _POSSIBLE_REGRESSION_P_WORSE
                ):
                    logger.warning(
                        "baseline_promotion_rejected",
                        latest_dir=str(latest_dir),
                        baseline_dir=str(baseline_dir),
                    )
                    return False

    baseline_dir.parent.mkdir(parents=True, exist_ok=True)
    if baseline_dir.exists():
        shutil.rmtree(baseline_dir)
    shutil.copytree(latest_dir, baseline_dir)

    meta_payload = {
        "promoted_at": datetime.now(UTC).isoformat(),
        "promoted_git_commit": _read_git_head_sha(),
        "promoted_by": "bayesian_compare",
        "criteria_met": True,
    }
    (baseline_dir / "baseline_meta.json").write_text(
        json.dumps(meta_payload, indent=2),
        encoding="utf-8",
    )
    logger.info(
        "baseline_promoted",
        latest_dir=str(latest_dir),
        baseline_dir=str(baseline_dir),
    )
    return True


def serialize_results(
    results: dict[str, BestResult],
) -> dict[str, Any]:
    """Serializes ``BestResult`` mapping into a JSON-compatible dictionary.

    Args:
        results: Mapping from benchmark name to ``BestResult``.

    Returns:
        Dictionary mapping benchmark names to dictionary representations.
    """
    return {name: asdict(res) for name, res in results.items()}


def _pair_raw_series(
    raw_series: Mapping[str, list[float]],
    rope_fraction: float,
) -> tuple[dict[str, dict[str, list[float]]], dict[str, BestResult]]:
    """Pair ``_Baseline/`` and ``_Candidate/`` series and compute BEST results.

    Args:
        raw_series: Parsed Google Benchmark timing series.
        rope_fraction: ROPE half-width fraction.

    Returns:
        Tuple of ``(paired_samples, results)``.
    """
    paired: dict[str, dict[str, list[float]]] = {}
    for name, samples in raw_series.items():
        if "_Baseline/" in name:
            geometry = name.split("_Baseline/", 1)[1]
            paired.setdefault(geometry, {})["baseline"] = samples
        elif "_Candidate/" in name:
            geometry = name.split("_Candidate/", 1)[1]
            paired.setdefault(geometry, {})["candidate"] = samples

    results: dict[str, BestResult] = {}
    for geometry, pair in paired.items():
        base = pair.get("baseline", [])
        cand = pair.get("candidate", [])
        if len(base) >= _MIN_SAMPLE_COUNT and len(cand) >= _MIN_SAMPLE_COUNT:
            results[geometry] = analyze_benchmark(
                base,
                cand,
                rope_fraction=rope_fraction,
            )
    return paired, results


app = App(
    name="bayesian-compare",
    help="Analyze Google Benchmark JSON results with Bayesian BEST + ROPE.",
    result_action="return_value",
)


@app.default
def _cli_analyze(
    *,
    input_json: Annotated[
        Path,
        Parameter(name="--input", help="Path to Google Benchmark JSON output."),
    ],
    output_dir: Annotated[
        Path | None,
        Parameter(
            name="--output-dir",
            help="Directory for analysis.json, report.md, and report.html.",
        ),
    ] = None,
    rope: Annotated[
        float,
        Parameter(name="--rope", help="ROPE half-width fraction (default: 0.02)."),
    ] = 0.02,
    template: Annotated[
        PlotlyTemplate,
        Parameter(name="--template", help="Plotly HTML report theme template."),
    ] = "plotly_white",
    experiment: Annotated[
        str,
        Parameter(name="--experiment", help="Experiment title for report header."),
    ] = "benchmark",
) -> int:
    """Run Bayesian BEST comparison on a Google Benchmark JSON file.

    Args:
        input_json: Path to ``benchmark_raw.json``.
        output_dir: Output directory (defaults to ``input_json.parent``).
        rope: ROPE half-width fraction.
        template: Plotly HTML report template name.
        experiment: Experiment name shown in Markdown report.

    Returns:
        Exit code 0 on success or 1 if no paired benchmark series were found.
    """
    logger = _get_logger()
    raw_series = parse_google_benchmark_json(input_json)
    paired_samples, results = _pair_raw_series(raw_series, rope_fraction=rope)
    if not results:
        logger.warning("no_paired_benchmarks", input_json=str(input_json))
        return 1

    target_dir = Path(output_dir) if output_dir is not None else Path(input_json).parent
    target_dir.mkdir(parents=True, exist_ok=True)
    meta: dict[str, Any] = {
        "experiment": experiment,
        "timestamp": datetime.now(UTC).isoformat(),
        "git_commit": _read_git_head_sha(),
        "rope_fraction": rope,
        "repetitions": max(len(v) for v in raw_series.values()),
    }
    (target_dir / "analysis.json").write_text(
        json.dumps({"meta": meta, "benchmarks": serialize_results(results)}, indent=2),
        encoding="utf-8",
    )
    md_text = generate_markdown_report(results, meta, target_dir / "report.md")
    generate_plotly_html_report(
        results,
        paired_samples,
        target_dir / "report.html",
        meta=meta,
        template=template,
    )
    logger.info(
        "bayesian_analysis_complete",
        geometries=len(results),
        output_dir=str(target_dir),
        template=template,
    )
    sys.stdout.write(md_text)
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entrypoint for ``bayesian_compare``.

    Args:
        argv: Optional CLI argument sequence overriding ``sys.argv[1:]``.

    Returns:
        Integer process exit code.
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
