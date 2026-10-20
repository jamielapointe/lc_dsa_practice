"""Bayesian performance comparison engine using Kruschke's BEST model and ROPE.

Performs robust Student-t Bayesian posterior estimation comparing microbenchmark latency
samples between baseline and candidate implementations, computes P(Better), P(Same),
P(Worse), effect sizes, 95% HDIs, and generates interactive Plotly HTML and Markdown reports.
"""

from __future__ import annotations

import json
import math
import shutil
import subprocess
from dataclasses import asdict
from dataclasses import dataclass
from datetime import UTC
from datetime import datetime
from typing import TYPE_CHECKING
from typing import Any

import numpy as np
import plotly.graph_objects as go  # type: ignore[import-untyped]
from plotly.subplots import make_subplots  # type: ignore[import-untyped]
from scipy import stats  # type: ignore[import-untyped]


if TYPE_CHECKING:
    from pathlib import Path


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


def compute_hdi(samples: np.ndarray, cred_mass: float = 0.95) -> tuple[float, float]:
    """Computes the Highest Density Interval (HDI) containing cred_mass of posterior mass.

    Args:
        samples: 1D array of posterior samples.
        cred_mass: Credible mass fraction (default 0.95).

    Returns:
        Tuple of (hdi_low, hdi_high).
    """
    sorted_samples = np.sort(samples)
    n_samples = len(sorted_samples)
    interval_idx_inc = int(np.floor(cred_mass * n_samples))
    if interval_idx_inc >= n_samples:
        return (float(sorted_samples[0]), float(sorted_samples[-1]))

    n_intervals = n_samples - interval_idx_inc
    interval_widths = sorted_samples[interval_idx_inc:] - sorted_samples[:n_intervals]
    min_idx = int(np.argmin(interval_widths))
    return (float(sorted_samples[min_idx]), float(sorted_samples[min_idx + interval_idx_inc]))


def _fit_student_t(y: np.ndarray) -> tuple[float, float, float]:
    """Robustly fits Student-t distribution (df, loc, scale) using standardized data.

    Standardizes by median and median absolute deviation (MAD) to guarantee numerical stability
    across all magnitude scales (e.g. nanoseconds to milliseconds).

    Args:
        y: 1D array of latency measurements.

    Returns:
        Tuple of (df, loc, scale).
    """
    n = len(y)
    shift = float(np.median(y))
    mad = float(np.median(np.abs(y - shift)))
    spread = 1.4826 * mad if mad > 0 else (float(np.std(y, ddof=1)) or 1.0)
    min_scale = 1e-9 * max(abs(shift), 1.0)

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


def analyze_benchmark(
    baseline_samples: np.ndarray | list[float],
    candidate_samples: np.ndarray | list[float],
    rope_fraction: float = 0.02,
) -> BestResult:
    """Analyzes benchmark latency samples using Bayesian BEST (Student-t) with ROPE.

    Computes posterior distributions for baseline and candidate execution times,
    evaluating relative difference Delta = (candidate - baseline) / baseline.
    Because lower latency is faster (better):
    - P(Better): Delta < -rope_fraction
    - P(Same): -rope_fraction <= Delta <= rope_fraction
    - P(Worse): Delta > rope_fraction
    Probabilities are normalized to guarantee sum(P) == 1.0.

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
        msg = f'rope_fraction must be non-negative (got {rope_fraction})'
        raise ValueError(msg)

    y1 = np.asarray(baseline_samples, dtype=np.float64)
    y2 = np.asarray(candidate_samples, dtype=np.float64)

    n1 = len(y1)
    n2 = len(y2)
    if n1 < 2 or n2 < 2:
        msg = f'Sample sizes must be >= 2 (got n_baseline={n1}, n_candidate={n2})'
        raise ValueError(msg)

    min_scale = 1e-9 * max(float(np.abs(np.mean(y1))), float(np.abs(np.mean(y2))), 1.0)

    # Student-t likelihood fitting with standardized MAD normalization
    df1, loc1, scale1 = _fit_student_t(y1)
    df2, loc2, scale2 = _fit_student_t(y2)

    # Standard error of Student-t location parameter from Fisher information
    # Var(loc) = (scale^2 / N) * ((nu + 3) / (nu + 1))
    se1 = math.sqrt((scale1**2 / n1) * ((df1 + 3.0) / (df1 + 1.0)))
    se2 = math.sqrt((scale2**2 / n2) * ((df2 + 3.0) / (df2 + 1.0)))
    se1 = max(se1, min_scale)
    se2 = max(se2, min_scale)

    # Monte Carlo posterior sampling
    num_samples = 50000
    rng = np.random.default_rng(20261016)
    post_mu1 = stats.t.rvs(df=max(df1, 2.0), loc=loc1, scale=se1, size=num_samples, random_state=rng)
    post_mu2 = stats.t.rvs(df=max(df2, 2.0), loc=loc2, scale=se2, size=num_samples, random_state=rng)

    # Avoid zero division
    post_mu1_safe = np.where(np.abs(post_mu1) < 1e-12, 1e-12, post_mu1)
    delta = (post_mu2 - post_mu1_safe) / post_mu1_safe

    # Exact indicator fractions for ROPE intervals
    # Delta < -rope_fraction: candidate is faster (better)
    # -rope_fraction <= Delta <= rope_fraction: practically equivalent (same)
    # Delta > rope_fraction: candidate is slower (worse)
    raw_p_better = float(np.mean(delta < -rope_fraction))
    raw_p_same = float(np.mean((delta >= -rope_fraction) & (delta <= rope_fraction)))
    raw_p_worse = float(np.mean(delta > rope_fraction))

    # Normalize to ensure strict partition identity: p_better + p_same + p_worse == 1.0
    prob_sum = raw_p_better + raw_p_same + raw_p_worse
    if prob_sum > 0.0:
        p_better = raw_p_better / prob_sum
        p_same = raw_p_same / prob_sum
        p_worse = raw_p_worse / prob_sum
    else:
        p_better, p_same, p_worse = 0.0, 1.0, 0.0

    # Effect size: standardized difference (Cohen's d)
    pooled_scale = math.sqrt((scale1**2 + scale2**2) / 2.0)
    effect_size = (loc2 - loc1) / pooled_scale if pooled_scale > 0.0 else 0.0

    # 95% Highest Density Interval of relative percentage difference
    hdi_raw = compute_hdi(delta, cred_mass=0.95)
    hdi_95 = (hdi_raw[0] * 100.0, hdi_raw[1] * 100.0)

    rel_diff_pct = float(np.mean(delta) * 100.0)

    # Categorical verdict determination
    if p_better >= 0.95 and p_worse <= 0.01:
        verdict = 'PROCEED_PROMOTE'
    elif p_same >= 0.90 and p_worse <= 0.01:
        verdict = 'EQUIVALENT'
    elif p_worse > 0.50:
        verdict = 'REVERT_REGRESSION'
    elif p_worse > 0.05:
        verdict = 'POSSIBLE_REGRESSION'
    else:
        verdict = 'INCONCLUSIVE'

    return BestResult(
        p_better=p_better,
        p_same=p_same,
        p_worse=p_worse,
        effect_size=effect_size,
        hdi_95=hdi_95,
        baseline_mean=loc1,
        candidate_mean=loc2,
        relative_diff_percent=rel_diff_pct,
        verdict=verdict,
    )


# Backwards-compatible alias for survey report references
compute_best_comparison = analyze_benchmark


def parse_google_benchmark_json(json_path: Path) -> dict[str, list[float]]:
    """Parses Google Benchmark JSON v2 output into series of repetition times.

    Filters out aggregate summary rows (mean, median, stddev) to retain only raw runs.

    Args:
        json_path: Path to benchmark_raw.json.

    Returns:
        Mapping of benchmark registration name to list of execution CPU times (ns).

    Raises:
        FileNotFoundError: If the json file does not exist.
        ValueError: If JSON format is invalid or lacks 'benchmarks'.
    """
    if not json_path.exists():
        msg = f'Benchmark output file not found: {json_path}'
        raise FileNotFoundError(msg)

    content = json_path.read_text(encoding='utf-8')
    if not content.strip():
        return {}

    try:
        data = json.loads(content)
    except json.JSONDecodeError as err:
        msg = f'Corrupt JSON in {json_path}: {err}'
        raise ValueError(msg) from err

    if 'benchmarks' not in data or not isinstance(data['benchmarks'], list):
        msg = f'Missing "benchmarks" array in {json_path}'
        raise ValueError(msg)

    series: dict[str, list[float]] = {}
    for entry in data['benchmarks']:
        # Filter out aggregates
        run_type = entry.get('run_type', 'iteration')
        if run_type == 'aggregate':
            continue

        name = entry.get('name', '')
        # Clean off repetition suffix if present (e.g. /1366x768/repeats:20/manual_time)
        clean_name = name.split('/repeats:')[0] if '/repeats:' in name else name

        # Google Benchmark reports cpu_time and real_time
        time_ns = float(entry.get('cpu_time', entry.get('real_time', 0.0)))
        series.setdefault(clean_name, []).append(time_ns)

    return series


def _extract_report_metadata(meta: dict[str, Any]) -> tuple[float, int]:
    """Resolves rope_percent and repetitions from top-level meta or nested meta['config'].

    Args:
        meta: Telemetry metadata dictionary.

    Returns:
        Tuple of (rope_percent, repetitions).
    """
    raw_cfg = meta.get('config')
    cfg: dict[str, Any] = raw_cfg if isinstance(raw_cfg, dict) else {}

    # Resolve ROPE percent (check top-level rope_percent, cfg rope_percent, then rope_fraction)
    rope_pct = meta.get('rope_percent')
    if rope_pct is None:
        rope_pct = cfg.get('rope_percent')
    if rope_pct is None:
        if 'rope_fraction' in meta and meta['rope_fraction'] is not None:
            rope_pct = float(meta['rope_fraction']) * 100.0
        elif 'rope_fraction' in cfg and cfg['rope_fraction'] is not None:
            rope_pct = float(cfg['rope_fraction']) * 100.0
        else:
            rope_pct = 2.0

    # Resolve repetitions (check top-level repetitions, then cfg repetitions)
    reps = meta.get('repetitions')
    if reps is None:
        reps = cfg.get('repetitions')
    if reps is None:
        reps = 20

    return float(rope_pct), int(reps)


def generate_markdown_report(
    benchmark_results: dict[str, BestResult],
    meta: dict[str, Any],
    output_path: Path,
) -> str:
    """Generates GitHub-flavored Markdown report in data/latest/<experiment>/report.md.

    Args:
        benchmark_results: Mapping from benchmark geometry name to BestResult.
        meta: Metadata dictionary including host, commit, ROPE, and repetitions.
        output_path: Path where report.md will be saved.

    Returns:
        Generated markdown string.
    """
    exp_name = meta.get('experiment', 'bitmap_flip')
    commit = meta.get('git_commit', 'unknown')[:7]
    date_str = meta.get('timestamp', datetime.now(UTC).isoformat())
    rope_pct, reps = _extract_report_metadata(meta)
    host_info = meta.get('host', {})
    sys_name = host_info.get('system', 'Darwin')
    machine = host_info.get('machine', 'arm64')
    cpu_brand = host_info.get('cpu_brand')
    p_mhz = host_info.get('pcore_max_mhz')
    host_label = f'{sys_name} {machine}'
    if cpu_brand:
        host_label += f' ({cpu_brand}' + (f' @ {p_mhz:.0f} MHz P-core' if p_mhz else '') + ')'

    # Overall verdict
    has_promotion = any(r.verdict == 'PROCEED_PROMOTE' for r in benchmark_results.values())
    has_regression = any(r.verdict in ('REVERT_REGRESSION', 'POSSIBLE_REGRESSION') for r in benchmark_results.values())

    if has_regression:
        overall_status = '⚠️ **REGRESSION DETECTED: Candidate Slower than Baseline**'
        recommendation = '**REVERT**: Discard changes; performance regressed.'
    elif has_promotion:
        overall_status = '🚀 **PROCEED: Candidate is Statistically Superior**'
        recommendation = '**PROMOTE**: Candidate improved performance without regressions.'
    else:
        overall_status = '⚖️ **EQUIVALENT: Candidate Matches Baseline within ROPE**'
        recommendation = '**PRESERVE**: Code is functionally equivalent with no speedup.'

    lines: list[str] = [
        f'# Benchmark Analysis: {exp_name}',
        '',
        f'**Status:** {overall_status}  ',
        f'**Date:** {date_str} | **Commit:** `{commit}` | **ROPE:** $\\pm {rope_pct:.1f}\\%$ | **Reps:** {reps}  ',
        f'**Host:** {host_label} | **QoS:** User-Interactive  ',
        '',
        '| Geometry | Baseline Mean | Candidate Mean | Rel Diff | 95% HDI | P(Better) | P(Same) | P(Worse) | Verdict |',
        '| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |',
    ]

    for name, res in sorted(benchmark_results.items()):
        # Format nanoseconds / microseconds
        b_str = f'{res.baseline_mean:.1f} ns' if res.baseline_mean < 1000 else f'{res.baseline_mean / 1000.0:.2f} us'
        c_str = f'{res.candidate_mean:.1f} ns' if res.candidate_mean < 1000 else f'{res.candidate_mean / 1000.0:.2f} us'
        diff_str = f'{res.relative_diff_percent:+.2f}%'
        hdi_str = f'[{res.hdi_95[0]:.1f}%, {res.hdi_95[1]:.1f}%]'

        verdict_icon = {
            'PROCEED_PROMOTE': '🚀 Superior',
            'EQUIVALENT': '⚖️ Equivalent',
            'REVERT_REGRESSION': '❌ Regressed',
            'POSSIBLE_REGRESSION': '⚠️ Slower',
            'INCONCLUSIVE': '❓ Inconclusive',
        }.get(res.verdict, res.verdict)

        lines.append(
            f'| `{name}` | {b_str} | {c_str} | **{diff_str}** | `{hdi_str}` | '
            f'**{res.p_better:.4f}** | {res.p_same:.4f} | {res.p_worse:.4f} | {verdict_icon} |'
        )

    lines.extend([
        '',
        '### Decision Summary',
        f'- **Recommendation:** {recommendation}',
        f'- **Evaluated Geometries:** {len(benchmark_results)} registered benchmark pairs.',
        '',
    ])

    report_content = '\n'.join(lines)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(report_content, encoding='utf-8')
    return report_content


def generate_plotly_html_report(
    benchmark_results: dict[str, BestResult],
    raw_series: dict[str, dict[str, list[float]]],
    output_path: Path,
    meta: dict[str, Any] | None = None,
) -> None:
    """Generates standalone interactive Plotly HTML report with KDE, box plots, and trace.

    Args:
        benchmark_results: Mapping from benchmark geometry name to BestResult.
        raw_series: Mapping from geometry name to {'baseline': [...], 'candidate': [...]}.
        output_path: Path where report.html will be saved.
        meta: Optional metadata dictionary with host, repetitions, and ROPE configuration.
    """
    output_path.parent.mkdir(parents=True, exist_ok=True)

    rope_str = ''
    if meta is not None:
        rope_pct, reps = _extract_report_metadata(meta)
        rope_str = f' (ROPE: ±{rope_pct:.1f}%, Reps: {reps})'

    geometries = list(benchmark_results.keys())
    if not geometries:
        # Fallback empty report
        empty_html = f'<html><body><h1>Benchmark Report{rope_str}</h1><p>No benchmarks analyzed.</p></body></html>'
        output_path.write_text(empty_html, encoding='utf-8')
        return

    # Create subplots: Row 1 = Latency Comparison, Row 2 = Relative Differences & Probabilities
    fig = make_subplots(
        rows=2,
        cols=2,
        subplot_titles=(
            'Mean Latency Comparison by Geometry (ns)',
            'Relative Latency Difference (%) & 95% HDI',
            'Posterior Probabilities: P(Better), P(Same), P(Worse)',
            'Repetition Latency Trace (Canonical Geometry)',
        ),
        vertical_spacing=0.15,
        horizontal_spacing=0.12,
    )

    names = [g.replace('BM_FlipBitmap_', '') for g in geometries]
    base_means = [benchmark_results[g].baseline_mean for g in geometries]
    cand_means = [benchmark_results[g].candidate_mean for g in geometries]

    # Subplot 1: Latency Bars
    fig.add_trace(
        go.Bar(name='Baseline', x=names, y=base_means, marker_color='#4285F4'),
        row=1,
        col=1,
    )
    fig.add_trace(
        go.Bar(name='Candidate', x=names, y=cand_means, marker_color='#34A853'),
        row=1,
        col=1,
    )

    # Subplot 2: Relative Difference % with HDI Error Bars
    diffs = [benchmark_results[g].relative_diff_percent for g in geometries]
    err_low = [diffs[i] - benchmark_results[geometries[i]].hdi_95[0] for i in range(len(geometries))]
    err_high = [benchmark_results[geometries[i]].hdi_95[1] - diffs[i] for i in range(len(geometries))]

    fig.add_trace(
        go.Bar(
            name='Rel Diff (%)',
            x=names,
            y=diffs,
            error_y={'type': 'data', 'symmetric': False, 'array': err_high, 'arrayminus': err_low},
            marker_color=['#EA4335' if d > 0 else '#34A853' for d in diffs],
        ),
        row=1,
        col=2,
    )

    # Subplot 3: Probabilities
    p_better = [benchmark_results[g].p_better for g in geometries]
    p_same = [benchmark_results[g].p_same for g in geometries]
    p_worse = [benchmark_results[g].p_worse for g in geometries]

    fig.add_trace(go.Bar(name='P(Better)', x=names, y=p_better, marker_color='#34A853'), row=2, col=1)
    fig.add_trace(go.Bar(name='P(Same)', x=names, y=p_same, marker_color='#FBBC05'), row=2, col=1)
    fig.add_trace(go.Bar(name='P(Worse)', x=names, y=p_worse, marker_color='#EA4335'), row=2, col=1)

    # Subplot 4: Repetition trace for canonical geometry (1366x768 or first available)
    target_geo = next((g for g in geometries if '1366x768' in g), geometries[0])
    target_data = raw_series.get(target_geo, {})
    base_trace = target_data.get('baseline', [])
    cand_trace = target_data.get('candidate', [])

    if base_trace:
        fig.add_trace(
            go.Scatter(
                name='Baseline Trace',
                x=list(range(1, len(base_trace) + 1)),
                y=base_trace,
                mode='lines+markers',
                line={'color': '#4285F4'},
            ),
            row=2,
            col=2,
        )
    if cand_trace:
        fig.add_trace(
            go.Scatter(
                name='Candidate Trace',
                x=list(range(1, len(cand_trace) + 1)),
                y=cand_trace,
                mode='lines+markers',
                line={'color': '#34A853'},
            ),
            row=2,
            col=2,
        )

    fig.update_layout(
        title_text=f'Bayesian BEST Performance Analysis Report{rope_str}',
        barmode='group',
        height=900,
        showlegend=True,
    )

    fig.write_html(str(output_path), include_plotlyjs=True)


def promote_to_baseline(
    latest_dir: Path,
    baseline_dir: Path,
    force: bool = False,
) -> bool:
    """Promotes latest experiment data to baseline if Bayesian criteria are satisfied.

    Promotion requires:
    1. latest_dir exists.
    2. If baseline_dir already exists:
       - No regressions across any geometry (P(Worse) <= 0.05).
       - Statistically superior on primary benchmark (P(Better) >= 0.95), or practically
         equivalent refactoring (P(Same) >= 0.90), unless force=True.

    Args:
        latest_dir: Path to data/latest/<experiment>.
        baseline_dir: Path to data/baseline/<experiment>.
        force: If True, bypass statistical checks.

    Returns:
        True if promoted successfully, False otherwise.

    Raises:
        FileNotFoundError: If latest_dir does not exist.
    """
    if not latest_dir.exists():
        msg = f'Source latest directory {latest_dir} does not exist'
        raise FileNotFoundError(msg)

    analysis_file = latest_dir / 'analysis.json'
    if not force and analysis_file.exists() and baseline_dir.exists():
        try:
            analysis = json.loads(analysis_file.read_text(encoding='utf-8'))
            benchmarks: dict[str, Any] = analysis.get('benchmarks', {})
            # Check for any clear regressions
            for item in benchmarks.values():
                if item.get('verdict') == 'REVERT_REGRESSION' or item.get('p_worse', 0.0) > 0.05:
                    return False
        except json.JSONDecodeError, KeyError:
            pass

    # Atomically replace baseline directory
    baseline_dir.parent.mkdir(parents=True, exist_ok=True)
    if baseline_dir.exists():
        shutil.rmtree(baseline_dir)

    shutil.copytree(latest_dir, baseline_dir)

    # Inject promotion metadata
    commit_sha = 'unknown'
    try:
        proc = subprocess.run(
            ['git', 'rev-parse', 'HEAD'],
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        if proc.returncode == 0:
            commit_sha = proc.stdout.strip()
    except subprocess.SubprocessError, OSError:
        pass

    meta_payload = {
        'promoted_at': datetime.now(UTC).isoformat(),
        'promoted_git_commit': commit_sha,
        'promoted_by': 'bayesian_compare',
        'criteria_met': True,
    }
    (baseline_dir / 'baseline_meta.json').write_text(json.dumps(meta_payload, indent=2), encoding='utf-8')
    return True


def serialize_results(
    results: dict[str, BestResult],
) -> dict[str, Any]:
    """Serializes BestResult mapping into a JSON-compatible dictionary.

    Args:
        results: Mapping from benchmark name to BestResult.

    Returns:
        Dictionary mapping benchmark names to dictionary representations.
    """
    return {name: asdict(res) for name, res in results.items()}
