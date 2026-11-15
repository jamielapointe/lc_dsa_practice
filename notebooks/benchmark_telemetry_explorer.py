# /// script
# requires-python = ">=3.15"
# dependencies = [
#     "marimo>=0.25.2",
#     "polars>=2.1.0",
#     "numpy>=2.5.4",
#     "scipy>=1.18.1",
#     "arviz-base>=1.3.1",
#     "arviz-stats>=1.3.3",
#     "plotly>=7.1.0",
#     "pydantic>=2.14.3",
#     "pytest>=9.2.0",
# ]
# ///
"""Reactive Marimo explorer for C++23 benchmark telemetry and Bayesian BEST analysis."""

import marimo


__generated_with = "0.25.2"
app = marimo.App(width="medium", app_title="Benchmark Telemetry Explorer")


with app.setup:
    import json
    import math
    import sys
    import tempfile
    from pathlib import Path
    from typing import Literal, cast
    lazy from collections.abc import Mapping

    import arviz_base as azb
    import marimo as mo
    import numpy as np
    import plotly.graph_objects as go  # pyright: ignore[reportMissingTypeStubs]
    import polars as pl
    from arviz_stats.base.array import array_stats
    from pydantic import BaseModel, ConfigDict, Field
    from scipy import stats
    lazy from numpy.typing import NDArray


@app.class_definition
class ExplorerParams(BaseModel):
    """Validated configuration parameters for interactive and script modes."""

    model_config = ConfigDict(frozen=True)

    data_root: Path = Path("data")
    experiment: str = "flip_bitmap_unaligned"
    geometry: str = "1366x768"
    rope_fraction: float = Field(default=0.02, ge=0.0, le=1.0)
    n_reps: int = Field(default=30, ge=2, le=500)
    theme: Literal["auto", "plotly_dark", "plotly_white"] = "auto"


@app.function
def format_cli_help() -> str:
    """Returns CLI usage instructions for headless script execution.

    Returns:
        Formatted multi-line help text documenting supported flags.
    """
    return (
        "Usage: pixi run python notebooks/benchmark_telemetry_explorer.py -- [OPTIONS]\n"
        "\n"
        "Options:\n"
        "  --data-root PATH       Telemetry root directory (default: data)\n"
        "  --experiment NAME      Experiment directory (default: flip_bitmap_unaligned)\n"
        "  --geometry GEOM        Active geometry label (default: 1366x768)\n"
        "  --rope FLOAT           ROPE half-width fraction in [0.0, 1.0] (default: 0.02)\n"
        "  --n-reps INT           Repetition cap in [2, 500] (default: 30)\n"
        "  --theme NAME           auto | plotly_dark | plotly_white (default: auto)\n"
    )


@app.function
def parse_cli_explorer_params(raw_args: Mapping[str, object]) -> ExplorerParams:
    """Parses Marimo CLI arguments into a validated ``ExplorerParams`` instance.

    Filters out empty ``""`` separator keys emitted when callers pass ``--`` and
    ignores unrelated host/runner flags (such as ``pytest`` arguments during tests).

    Args:
        raw_args: Mapping produced by ``mo.cli_args()`` or test inputs.

    Returns:
        Validated ``ExplorerParams`` model instance.
    """
    payload: dict[str, object] = {}
    for alias_group, field_name in (
        (("data-root", "data_root"), "data_root"),
        (("experiment",), "experiment"),
        (("geometry",), "geometry"),
        (("rope", "rope-fraction", "rope_fraction"), "rope_fraction"),
        (("n-reps", "n_reps"), "n_reps"),
        (("theme",), "theme"),
    ):
        for alias in alias_group:
            if alias not in raw_args:
                continue
            raw_value = raw_args[alias]
            if isinstance(raw_value, list):
                typed_list = cast("list[object]", raw_value)
                if not typed_list:
                    continue
                scalar = typed_list[-1]
            else:
                scalar = raw_value
            if scalar is not None and not (isinstance(scalar, str) and not scalar):
                payload[field_name] = scalar
                break
    return ExplorerParams.model_validate(payload)


@app.function
def discover_available_experiments(
    data_root: Path,
    preferred: str = "flip_bitmap_unaligned",
) -> list[str]:
    """Discovers experiment directories under ``<data_root>/{latest,baseline}``.

    Args:
        data_root: Root directory containing ``baseline/`` and ``latest/``.
        preferred: Default or CLI-specified experiment guaranteed to appear.

    Returns:
        Sorted list of unique experiment names, including ``preferred``.
    """
    discovered: set[str] = {preferred}
    for sub_name in ("latest", "baseline"):
        sub_dir = data_root / sub_name
        if not sub_dir.is_dir():
            continue
        for child in sub_dir.iterdir():
            if child.is_dir() and (
                (child / "telemetry.json").is_file()
                or (child / "benchmark_raw.json").is_file()
            ):
                discovered.add(child.name)
    return sorted(discovered)


@app.function
def discover_available_geometries(telemetry_df: pl.DataFrame) -> list[str]:
    """Returns unique benchmark geometries ordered deterministically.

    Orders ``<width>x<height>`` geometries numerically by area and width when
    possible, placing non-grid labels afterward in lexicographic order.

    Args:
        telemetry_df: Telemetry DataFrame containing a ``geometry`` column.

    Returns:
        Ordered list of unique non-empty geometry strings.
    """
    if telemetry_df.is_empty():
        return []
    raw_geometries = [
        str(val) for val in telemetry_df["geometry"].unique().to_list() if str(val)
    ]

    def _sort_key(label: str) -> tuple[int, int, int, str]:
        parts = label.split("x")
        if len(parts) == 2 and parts[0].isdigit() and parts[1].isdigit():
            width = int(parts[0])
            height = int(parts[1])
            return (0, width * height, width, label)
        return (1, 0, 0, label)

    return sorted(set(raw_geometries), key=_sort_key)


@app.function
def select_active_geometry(available_geometries: list[str], requested: str) -> str:
    """Selects ``requested`` geometry if present, else canonical or first available.

    Args:
        available_geometries: Ordered list of geometries present in loaded telemetry.
        requested: Preferred or CLI-specified geometry label.

    Returns:
        Geometry label guaranteed to belong to ``available_geometries``.

    Raises:
        ValueError: If ``available_geometries`` is empty.
    """
    if not available_geometries:
        msg = "Cannot select geometry from an empty telemetry set"
        raise ValueError(msg)
    if requested in available_geometries:
        return requested
    if "1366x768" in available_geometries:
        return "1366x768"
    return available_geometries[0]


@app.function
def resolve_plotly_template(
    theme_choice: str,
    app_theme: str | None = None,
) -> str:
    """Resolves the explicit Plotly layout template from switch and Marimo theme.

    Args:
        theme_choice: Switch value (``"auto"``, ``"plotly_dark"``, ``"plotly_white"``).
        app_theme: Active Marimo runtime theme from ``mo.app_meta().theme``.

    Returns:
        Explicit Plotly template string (``"plotly_dark"`` or ``"plotly_white"``).
    """
    if theme_choice in {"plotly_dark", "plotly_white"}:
        return theme_choice
    return "plotly_dark" if app_theme == "dark" else "plotly_white"


@app.function
def generate_synthetic_telemetry(seed: int = 42, n_reps: int = 30) -> pl.DataFrame:
    """Generates deterministic synthetic C++23 ``bitmap_flip`` benchmark telemetry.

    Produces paired baseline vs. candidate latency repetitions across three unaligned
    bitmap geometries so clean clones without gitignored ``data/`` remain runnable.

    Args:
        seed: Deterministic NumPy PRNG seed.
        n_reps: Number of benchmark repetitions per variant and geometry (>= 2).

    Returns:
        Polars DataFrame with columns ``experiment``, ``geometry``, ``variant``,
        ``repetition``, and ``latency_ns``.

    Raises:
        ValueError: If ``n_reps`` is smaller than 2.
    """
    if n_reps < 2:
        msg = f"n_reps must be >= 2 (got {n_reps})"
        raise ValueError(msg)

    rng = np.random.default_rng(seed)
    geometries: tuple[tuple[str, float, float], ...] = (
        ("64x64", 48.0, 34.5),
        ("1366x768", 1840.0, 1295.0),
        ("4096x2160", 14200.0, 9980.0),
    )

    records: list[dict[str, str | int | float]] = []
    for geometry, base_loc, cand_loc in geometries:
        base_noise = rng.normal(loc=base_loc, scale=base_loc * 0.018, size=n_reps)
        cand_noise = rng.normal(loc=cand_loc, scale=cand_loc * 0.016, size=n_reps)
        # Inject mild heavy-tailed OS jitter so Student-t BEST modeling is exercised.
        if n_reps >= 10:
            base_noise[n_reps // 3] *= 1.07
            cand_noise[n_reps // 2] *= 1.05

        for rep_idx in range(n_reps):
            records.extend([
                {
                    "experiment": "bitmap_flip",
                    "geometry": geometry,
                    "variant": "baseline",
                    "repetition": rep_idx + 1,
                    "latency_ns": float(max(base_noise[rep_idx], 1.0)),
                },
                {
                    "experiment": "bitmap_flip",
                    "geometry": geometry,
                    "variant": "candidate",
                    "repetition": rep_idx + 1,
                    "latency_ns": float(max(cand_noise[rep_idx], 1.0)),
                },
            ])

    return pl.DataFrame(records)


@app.function
def read_json_dict(json_path: Path) -> dict[object, object] | None:
    """Safely reads a JSON file whose top-level value is a JSON object.

    Args:
        json_path: Path to the JSON file to read.

    Returns:
        Parsed dictionary when readable and object-shaped, otherwise ``None``.
    """
    if not json_path.is_file():
        return None
    try:
        raw_payload: object = json.loads(json_path.read_text(encoding="utf-8"))
    except OSError, json.JSONDecodeError:
        return None
    if not isinstance(raw_payload, dict):
        return None
    return cast("dict[object, object]", raw_payload)


@app.function
def parse_gbench_json_records(
    json_path: Path,
    experiment: str,
    forced_variant: str | None = None,
) -> list[dict[str, str | int | float]]:
    """Extracts iteration-level latency records from Google Benchmark JSON output.

    Args:
        json_path: Path to a ``benchmark_raw.json`` file.
        experiment: Experiment identifier recorded on each row.
        forced_variant: Optional variant override (``"baseline"`` or ``"candidate"``)
            when reading separated baseline/latest directories.

    Returns:
        List of normalized telemetry row dictionaries.
    """
    payload = read_json_dict(json_path)
    if payload is None:
        return []

    raw_benchmarks = payload.get("benchmarks")
    if not isinstance(raw_benchmarks, list):
        return []

    counters: dict[tuple[str, str], int] = {}
    rows: list[dict[str, str | int | float]] = []
    for entry_obj in cast("list[object]", raw_benchmarks):
        if not isinstance(entry_obj, dict):
            continue
        entry = cast("dict[object, object]", entry_obj)
        if entry.get("run_type", "iteration") == "aggregate":
            continue

        raw_name = str(entry.get("name", ""))
        clean_name = raw_name.split("/repeats:", maxsplit=1)[0]
        if "_Baseline/" in clean_name:
            geometry = clean_name.split("_Baseline/", 1)[1]
            variant = forced_variant or "baseline"
        elif "_Candidate/" in clean_name:
            geometry = clean_name.split("_Candidate/", 1)[1]
            variant = forced_variant or "candidate"
        else:
            geometry = clean_name or "default"
            variant = forced_variant or "candidate"

        latency_raw = entry.get("cpu_time")
        if not isinstance(latency_raw, (int, float)):
            latency_raw = entry.get("real_time")
        if not isinstance(latency_raw, (int, float)):
            continue

        key = (geometry, variant)
        counters[key] = counters.get(key, 0) + 1
        rows.append({
            "experiment": experiment,
            "geometry": geometry,
            "variant": variant,
            "repetition": counters[key],
            "latency_ns": float(latency_raw),
        })
    return rows


@app.function
def parse_telemetry_json_records(
    telemetry_json: Path, experiment: str
) -> list[dict[str, str | int | float]]:
    """Parses structured ``telemetry.json`` mappings into flat latency records.

    Args:
        telemetry_json: Path to ``telemetry.json``.
        experiment: Experiment identifier recorded on each row.

    Returns:
        List of normalized telemetry row dictionaries, or empty list if missing/invalid.
    """
    payload = read_json_dict(telemetry_json)
    if payload is None:
        return []

    benchmarks_obj = payload.get("benchmarks")
    if not isinstance(benchmarks_obj, dict):
        return []

    benchmarks = cast("dict[object, object]", benchmarks_obj)
    rows: list[dict[str, str | int | float]] = []
    for geometry, variants_obj in benchmarks.items():
        if not isinstance(variants_obj, dict):
            continue
        variants = cast("dict[object, object]", variants_obj)
        for variant in ("baseline", "candidate"):
            series_obj = variants.get(variant)
            if not isinstance(series_obj, list):
                continue
            for idx, val in enumerate(cast("list[object]", series_obj), start=1):
                if isinstance(val, (int, float)):
                    rows.append({
                        "experiment": experiment,
                        "geometry": str(geometry),
                        "variant": variant,
                        "repetition": idx,
                        "latency_ns": float(val),
                    })
    return rows


@app.function
def load_experiment_telemetry(
    data_root: Path, experiment: str
) -> tuple[pl.DataFrame, str]:
    """Loads benchmark telemetry from ``data/`` or falls back to synthetic data.

    Checks ``<data_root>/latest/<experiment>/telemetry.json`` first, then pairs
    ``<data_root>/{baseline,latest}/<experiment>/benchmark_raw.json``. When no valid
    files exist, returns deterministic synthetic telemetry from
    ``generate_synthetic_telemetry()``.

    Args:
        data_root: Root directory containing ``baseline/`` and ``latest/``.
        experiment: Experiment directory name (for example ``"flip_bitmap_unaligned"``).

    Returns:
        Tuple of ``(telemetry_df, source_label)``.
    """
    latest_dir = data_root / "latest" / experiment
    baseline_dir = data_root / "baseline" / experiment

    telemetry_json = latest_dir / "telemetry.json"
    structured_rows = parse_telemetry_json_records(telemetry_json, experiment)
    if structured_rows:
        return pl.DataFrame(structured_rows), f"live ({telemetry_json})"

    latest_raw = latest_dir / "benchmark_raw.json"
    baseline_raw = baseline_dir / "benchmark_raw.json"
    paired_rows = parse_gbench_json_records(latest_raw, experiment)
    variants_seen = {str(row["variant"]) for row in paired_rows}
    if paired_rows and {"baseline", "candidate"}.issubset(variants_seen):
        return pl.DataFrame(paired_rows), f"live ({latest_raw})"

    combined_rows = parse_gbench_json_records(
        baseline_raw, experiment, forced_variant="baseline"
    ) + parse_gbench_json_records(latest_raw, experiment, forced_variant="candidate")
    combined_variants = {str(row["variant"]) for row in combined_rows}
    if combined_rows and {"baseline", "candidate"}.issubset(combined_variants):
        return (
            pl.DataFrame(combined_rows),
            f"live ({baseline_raw} vs {latest_raw})",
        )

    fallback_df = generate_synthetic_telemetry(seed=42, n_reps=30)
    return (
        fallback_df,
        f"synthetic fallback (no live JSON under {latest_dir})",
    )


@app.function
def slice_telemetry_by_geometry_and_reps(
    raw_df: pl.DataFrame,
    raw_source: str,
    geometry: str,
    n_reps: int,
) -> tuple[pl.DataFrame, pl.DataFrame, str, str]:
    """Applies repetition depth cap and filters telemetry to a single active geometry.

    Args:
        raw_df: Full telemetry DataFrame returned by ``load_experiment_telemetry``.
        raw_source: Source label returned by ``load_experiment_telemetry``.
        geometry: Requested geometry label.
        n_reps: Maximum repetition index (or sample count when synthetic).

    Returns:
        Tuple of ``(full_df, selected_geometry_df, active_geometry, source_label)``.
    """
    if raw_source.startswith("synthetic"):
        telemetry_df = generate_synthetic_telemetry(seed=42, n_reps=n_reps)
        telemetry_source = f"synthetic (seed=42, n_reps={n_reps})"
    else:
        telemetry_df = raw_df.filter(pl.col("repetition") <= n_reps)
        telemetry_source = raw_source

    available = discover_available_geometries(telemetry_df)
    if not available:
        return telemetry_df, telemetry_df, geometry, telemetry_source
    active_geometry = select_active_geometry(available, geometry)
    selected_df = telemetry_df.filter(pl.col("geometry") == active_geometry)
    return telemetry_df, selected_df, active_geometry, telemetry_source


@app.function
def load_filtered_telemetry(
    data_root: Path,
    experiment: str,
    geometry: str,
    n_reps: int,
) -> tuple[pl.DataFrame, pl.DataFrame, str]:
    """Loads telemetry, caps repetition depth, and filters to one valid geometry.

    When the requested ``geometry`` is absent from the dataset, selects canonical
    ``"1366x768"`` or the first discovered geometry rather than mixing geometries.

    Args:
        data_root: Root directory containing ``baseline/`` and ``latest/``.
        experiment: Experiment directory name.
        geometry: Selected bitmap geometry label.
        n_reps: Maximum repetition index (or sample count when synthetic).

    Returns:
        Tuple of ``(full_df, selected_geometry_df, source_label)``.
    """
    raw_df, raw_source = load_experiment_telemetry(data_root, experiment)
    telemetry_df, selected_df, _, telemetry_source = (
        slice_telemetry_by_geometry_and_reps(raw_df, raw_source, geometry, n_reps)
    )
    return telemetry_df, selected_df, telemetry_source


@app.function
def extract_variant_latencies(
    telemetry_df: pl.DataFrame, variant: str
) -> NDArray[np.float64]:
    """Extracts 1D float64 latency samples for a given variant from a DataFrame.

    Args:
        telemetry_df: Telemetry DataFrame containing ``variant`` and ``latency_ns``.
        variant: Variant name (``"baseline"`` or ``"candidate"``).

    Returns:
        1D NumPy float64 array of latencies in nanoseconds.
    """
    filtered = telemetry_df.filter(pl.col("variant") == variant)
    return np.asarray(filtered["latency_ns"].to_numpy(), dtype=np.float64)


@app.function
def fit_student_t(samples: NDArray[np.float64]) -> tuple[float, float, float]:
    """Fits Student-t ``(df, loc, scale)`` via MAD-standardized MLE with fallback.

    Args:
        samples: 1D float64 array of positive latency measurements.

    Returns:
        Tuple of ``(degrees_of_freedom, location, scale)``.
    """
    n = len(samples)
    median = float(np.median(samples))
    mad = float(np.median(np.abs(samples - median)))
    spread = 1.4826 * mad if mad > 0.0 else float(np.std(samples, ddof=1))
    min_scale = 1e-9 * max(abs(median), 1.0)
    spread = max(spread, min_scale)

    standardized = (samples - median) / spread
    try:
        raw_df, raw_loc, raw_scale = stats.t.fit(standardized)
        df = max(float(raw_df), 1.0)
        loc = float(raw_loc * spread + median)
        scale = max(float(raw_scale * spread), min_scale)
    except RuntimeError, ValueError:
        df = max(float(n - 1), 1.0)
        loc = median
        scale = spread
    return df, loc, scale


@app.function
def sample_best_posterior_deltas(
    baseline_ns: NDArray[np.float64],
    candidate_ns: NDArray[np.float64],
) -> tuple[NDArray[np.float64], NDArray[np.float64], dict[str, float]]:
    """Draws multi-chain Student-t posterior samples for baseline vs. candidate deltas.

    Args:
        baseline_ns: Baseline latency array in nanoseconds (length >= 2).
        candidate_ns: Candidate latency array in nanoseconds (length >= 2).

    Returns:
        Tuple of ``(delta_rel_chains, delta_ns_chains, param_summary)`` where chain
        arrays have shape ``(4, 5000)``.

    Raises:
        ValueError: If fewer than 2 samples are given or non-finite values appear.
    """
    y1 = np.asarray(baseline_ns, dtype=np.float64)
    y2 = np.asarray(candidate_ns, dtype=np.float64)
    if y1.ndim != 1 or y2.ndim != 1 or len(y1) < 2 or len(y2) < 2:
        msg = f"Expected 1D arrays with >= 2 samples (got {y1.shape}, {y2.shape})"
        raise ValueError(msg)
    if not (np.all(np.isfinite(y1)) and np.all(np.isfinite(y2))):
        msg = "Latency samples must be finite"
        raise ValueError(msg)

    df1, loc1, scale1 = fit_student_t(y1)
    df2, loc2, scale2 = fit_student_t(y2)

    min_scale = 1e-9 * max(float(np.abs(np.mean(y1))), float(np.abs(np.mean(y2))), 1.0)
    se1 = max(math.sqrt((scale1**2 / len(y1)) * ((df1 + 3.0) / (df1 + 1.0))), min_scale)
    se2 = max(math.sqrt((scale2**2 / len(y2)) * ((df2 + 3.0) / (df2 + 1.0))), min_scale)

    rng = np.random.default_rng(20261115)
    shape = (4, 5000)
    post_mu1 = np.asarray(
        stats.t.rvs(
            df=max(df1, 2.0), loc=loc1, scale=se1, size=shape, random_state=rng
        ),
        dtype=np.float64,
    )
    post_mu2 = np.asarray(
        stats.t.rvs(
            df=max(df2, 2.0), loc=loc2, scale=se2, size=shape, random_state=rng
        ),
        dtype=np.float64,
    )

    safe_mu1 = np.where(np.abs(post_mu1) < 1e-12, 1e-12, post_mu1)
    delta_ns = post_mu2 - post_mu1
    delta_rel = delta_ns / safe_mu1

    pooled_scale = math.sqrt((scale1**2 + scale2**2) / 2.0)
    effect_size = (loc2 - loc1) / pooled_scale if pooled_scale > 0.0 else 0.0

    params: dict[str, float] = {
        "baseline_mean_ns": loc1,
        "candidate_mean_ns": loc2,
        "delta_mean_ns": loc2 - loc1,
        "effect_size": effect_size,
    }
    return delta_rel, delta_ns, params


@app.function
def compute_arviz_diagnostics(
    pct_draws: NDArray[np.float64],
    ns_draws: NDArray[np.float64],
    rel_draws: NDArray[np.float64],
) -> tuple[float, float, float, float, float, float]:
    """Evaluates ArviZ array HDI bounds, R-hat, and ESS on multi-chain posterior draws.

    Args:
        pct_draws: Multi-chain relative delta draws in percentage points.
        ns_draws: Multi-chain latency delta draws in nanoseconds.
        rel_draws: Multi-chain relative delta draws as unitless fractions.

    Returns:
        Tuple of ``(hdi_low_pct, hdi_high_pct, hdi_low_ns, hdi_high_ns, rhat, ess)``.
    """
    hdi_pct_raw = array_stats.hdi(pct_draws.ravel(), prob=0.95)  # pyright: ignore[reportUnknownMemberType, reportUnknownVariableType]
    hdi_ns_raw = array_stats.hdi(ns_draws.ravel(), prob=0.95)  # pyright: ignore[reportUnknownMemberType, reportUnknownVariableType]
    rhat_raw = array_stats.rhat(rel_draws)  # pyright: ignore[reportUnknownMemberType, reportUnknownVariableType]
    ess_raw = array_stats.ess(rel_draws)  # pyright: ignore[reportUnknownMemberType, reportUnknownVariableType]

    hdi_pct = np.asarray(cast("object", hdi_pct_raw), dtype=np.float64).ravel()
    hdi_ns = np.asarray(cast("object", hdi_ns_raw), dtype=np.float64).ravel()
    rhat_val = float(np.asarray(cast("object", rhat_raw), dtype=np.float64).item())
    ess_val = float(np.asarray(cast("object", ess_raw), dtype=np.float64).item())
    return (
        float(hdi_pct[0]),
        float(hdi_pct[1]),
        float(hdi_ns[0]),
        float(hdi_ns[1]),
        rhat_val,
        ess_val,
    )


@app.function
def summarize_best_posterior_draws(
    delta_rel: NDArray[np.float64],
    delta_ns: NDArray[np.float64],
    params: dict[str, float],
    rope_fraction: float = 0.02,
) -> dict[str, float]:
    """Summarizes pre-sampled posterior delta chains via ArviZ HDI, R-hat, and ROPE.

    Args:
        delta_rel: Relative delta draws with shape ``(chains, draws)``.
        delta_ns: Nanosecond delta draws with shape ``(chains, draws)``.
        params: Location/effect summary dictionary from
            ``sample_best_posterior_deltas``.
        rope_fraction: Half-width of the Region of Practical Equivalence (>= 0.0).

    Returns:
        Dictionary of posterior summary metrics.

    Raises:
        ValueError: If ``rope_fraction < 0.0``.
    """
    if rope_fraction < 0.0:
        msg = f"rope_fraction must be non-negative (got {rope_fraction})"
        raise ValueError(msg)

    delta_pct = delta_rel * 100.0

    # Organize multi-chain draws in an ArviZ DataTree and evaluate array diagnostics.
    posterior_dt = azb.from_dict({
        "posterior": {
            "delta_rel": delta_rel,
            "delta_pct": delta_pct,
            "delta_ns": delta_ns,
        }
    })
    pct_draws = np.asarray(
        posterior_dt["posterior"]["delta_pct"].values, dtype=np.float64
    )
    ns_draws = np.asarray(
        posterior_dt["posterior"]["delta_ns"].values, dtype=np.float64
    )
    rel_draws = np.asarray(
        posterior_dt["posterior"]["delta_rel"].values, dtype=np.float64
    )

    (
        hdi_low_pct,
        hdi_high_pct,
        hdi_low_ns,
        hdi_high_ns,
        rhat_val,
        ess_val,
    ) = compute_arviz_diagnostics(pct_draws, ns_draws, rel_draws)

    raw_better = float(np.mean(rel_draws < -rope_fraction))
    raw_same = float(
        np.mean((rel_draws >= -rope_fraction) & (rel_draws <= rope_fraction))
    )
    raw_worse = float(np.mean(rel_draws > rope_fraction))
    prob_sum = raw_better + raw_same + raw_worse
    if prob_sum > 0.0:
        p_better = raw_better / prob_sum
        p_same = raw_same / prob_sum
        p_worse = raw_worse / prob_sum
    else:
        p_better, p_same, p_worse = 0.0, 1.0, 0.0

    rel_diff_pct = float(np.mean(pct_draws))
    base_mean = params["baseline_mean_ns"]
    cand_mean = params["candidate_mean_ns"]
    speedup_pct = (
        ((base_mean - cand_mean) / base_mean) * 100.0 if base_mean > 0.0 else 0.0
    )

    return {
        "baseline_mean_ns": base_mean,
        "candidate_mean_ns": cand_mean,
        "delta_mean_ns": params["delta_mean_ns"],
        "relative_diff_pct": rel_diff_pct,
        "speedup_pct": speedup_pct,
        "p_better": p_better,
        "p_same": p_same,
        "p_worse": p_worse,
        "hdi_low_pct": hdi_low_pct,
        "hdi_high_pct": hdi_high_pct,
        "hdi_low_ns": hdi_low_ns,
        "hdi_high_ns": hdi_high_ns,
        "effect_size": params["effect_size"],
        "rope_fraction": rope_fraction,
        "rhat": rhat_val,
        "ess": ess_val,
    }


@app.function
def compute_best_posterior_summary(
    baseline_ns: NDArray[np.float64],
    candidate_ns: NDArray[np.float64],
    rope_fraction: float = 0.02,
) -> dict[str, float]:
    """Computes Bayesian BEST + ROPE posterior probabilities and ``arviz_stats`` HDIs.

    Args:
        baseline_ns: Baseline latency array in nanoseconds.
        candidate_ns: Candidate latency array in nanoseconds.
        rope_fraction: Half-width of the Region of Practical Equivalence (>= 0.0).

    Returns:
        Dictionary of posterior summary metrics including ``p_better``, ``p_same``,
        ``p_worse``, ``hdi_low_pct``, ``hdi_high_pct``, ``hdi_low_ns``, ``hdi_high_ns``,
        ``relative_diff_pct``, ``speedup_pct``, ``effect_size``, ``rhat``, and ``ess``.

    Raises:
        ValueError: If ``rope_fraction < 0.0`` or sample arrays fail validation.
    """
    if rope_fraction < 0.0:
        msg = f"rope_fraction must be non-negative (got {rope_fraction})"
        raise ValueError(msg)

    delta_rel, delta_ns, params = sample_best_posterior_deltas(
        baseline_ns, candidate_ns
    )
    return summarize_best_posterior_draws(
        delta_rel,
        delta_ns,
        params,
        rope_fraction=rope_fraction,
    )


@app.function
def build_kpi_row(posterior_summary: dict[str, float]) -> mo.Html:
    """Builds the horizontal row of ``mo.stat`` KPI cards for the posterior summary.

    Args:
        posterior_summary: Dictionary returned by ``compute_best_posterior_summary``.

    Returns:
        Horizontal Marimo stack containing five metric cards.
    """
    rope_pct = posterior_summary["rope_fraction"] * 100.0
    speedup = posterior_summary["speedup_pct"]
    return mo.hstack(
        [
            mo.stat(
                value=f"{posterior_summary['baseline_mean_ns']:.1f} ns",
                label="Baseline Mean",
                caption="Student-t posterior location",
            ),
            mo.stat(
                value=f"{posterior_summary['candidate_mean_ns']:.1f} ns",
                label="Candidate Mean",
                caption=f"Delta: {posterior_summary['delta_mean_ns']:+.1f} ns",
            ),
            mo.stat(
                value=f"{speedup:+.2f}%",
                label="Speedup",
                caption=(
                    f"95% HDI: [{posterior_summary['hdi_low_pct']:.2f}%, "
                    f"{posterior_summary['hdi_high_pct']:.2f}%]"
                ),
                direction="increase" if speedup > 0.0 else "decrease",
            ),
            mo.stat(
                value=f"{posterior_summary['p_better'] * 100.0:.1f}%",
                label="P(Better)",
                caption=(
                    f"ROPE ±{rope_pct:.1f}% | "
                    f"P(Same)={posterior_summary['p_same'] * 100.0:.1f}%"
                ),
            ),
            mo.stat(
                value=f"{posterior_summary['effect_size']:.2f}",
                label="Cohen's d",
                caption=(
                    f"R-hat {posterior_summary['rhat']:.4f} | "
                    f"ESS {posterior_summary['ess']:.0f}"
                ),
            ),
        ],
        justify="space-between",
        gap=1,
        wrap=True,
    )


@app.function
def build_posterior_figure(
    posterior_deltas_pct: NDArray[np.float64],
    posterior_summary: dict[str, float],
    geometry: str,
    template: str,
) -> go.Figure:
    """Constructs the Plotly posterior relative-delta histogram with ROPE & HDI bands.

    Args:
        posterior_deltas_pct: 1D posterior draws of relative latency change (%).
        posterior_summary: Dictionary returned by ``compute_best_posterior_summary``.
        geometry: Selected bitmap geometry label.
        template: Explicit Plotly template name (``"plotly_dark"`` / ``"plotly_white"``).

    Returns:
        Configured ``plotly.graph_objects.Figure`` instance.
    """
    rope_pct = posterior_summary["rope_fraction"] * 100.0
    hdi_low = posterior_summary["hdi_low_pct"]
    hdi_high = posterior_summary["hdi_high_pct"]

    fig = go.Figure()
    fig.add_trace(  # pyright: ignore[reportUnknownMemberType]
        go.Histogram(
            x=posterior_deltas_pct,
            nbinsx=60,
            histnorm="probability density",
            name="Posterior Δ Latency (%)",
            marker_color="#38BDF8",
            opacity=0.8,
        )
    )
    fig.add_vrect(  # pyright: ignore[reportUnknownMemberType]
        x0=-rope_pct,
        x1=rope_pct,
        fillcolor="rgba(250, 204, 21, 0.22)",
        line_width=0,
        annotation_text=f"ROPE ±{rope_pct:.1f}%",
        annotation_position="top left",
    )
    fig.add_vline(  # pyright: ignore[reportUnknownMemberType]
        x=hdi_low,
        line_dash="dash",
        line_color="#22C55E",
        annotation_text=f"HDI Low {hdi_low:.2f}%",
        annotation_position="bottom left",
    )
    fig.add_vline(  # pyright: ignore[reportUnknownMemberType]
        x=hdi_high,
        line_dash="dash",
        line_color="#22C55E",
        annotation_text=f"HDI High {hdi_high:.2f}%",
        annotation_position="bottom right",
    )
    fig.add_vline(x=0.0, line_dash="dot", line_color="#94A3B8")  # pyright: ignore[reportUnknownMemberType]
    fig.update_layout(  # pyright: ignore[reportUnknownMemberType]
        template=template,
        title=f"Posterior Relative Latency Difference Δ% ({geometry})",
        xaxis_title="(Candidate - Baseline) / Baseline (%)",
        yaxis_title="Posterior Density",
        height=400,
    )
    return fig


@app.function
def build_distribution_figure(
    baseline_ns: NDArray[np.float64],
    candidate_ns: NDArray[np.float64],
    geometry: str,
    template: str,
) -> go.Figure:
    """Constructs the Plotly paired baseline vs. candidate empirical latency boxplot.

    Args:
        baseline_ns: Baseline sample latencies in nanoseconds.
        candidate_ns: Candidate sample latencies in nanoseconds.
        geometry: Selected bitmap geometry label.
        template: Explicit Plotly template name (``"plotly_dark"`` / ``"plotly_white"``).

    Returns:
        Configured ``plotly.graph_objects.Figure`` instance.
    """
    fig = go.Figure()
    fig.add_trace(  # pyright: ignore[reportUnknownMemberType]
        go.Box(
            y=baseline_ns,
            name="Baseline",
            boxpoints="all",
            jitter=0.35,
            marker_color="#60A5FA",
        )
    )
    fig.add_trace(  # pyright: ignore[reportUnknownMemberType]
        go.Box(
            y=candidate_ns,
            name="Candidate",
            boxpoints="all",
            jitter=0.35,
            marker_color="#34D399",
        )
    )
    fig.update_layout(  # pyright: ignore[reportUnknownMemberType]
        template=template,
        title=f"Empirical Repetition Latency Distribution ({geometry})",
        yaxis_title="CPU Time (ns)",
        height=380,
    )
    return fig


@app.function
def verify_telemetry_invariants() -> None:
    """Verifies synthetic telemetry, live JSON loading fallback, and BEST invariants.

    Raises:
        AssertionError: If any data generation, fallback, or posterior invariant fails.
    """
    df_a = generate_synthetic_telemetry(seed=42, n_reps=30)
    df_b = generate_synthetic_telemetry(seed=42, n_reps=30)
    assert df_a.equals(df_b)  # ruff: ignore[assert]
    assert df_a.columns == [  # ruff: ignore[assert]
        "experiment",
        "geometry",
        "variant",
        "repetition",
        "latency_ns",
    ]
    assert df_a.height == 180  # ruff: ignore[assert]
    assert float(np.min(df_a["latency_ns"].to_numpy())) > 0.0  # ruff: ignore[assert]

    try:
        generate_synthetic_telemetry(seed=42, n_reps=1)
    except ValueError:
        pass
    else:
        raise AssertionError("Expected ValueError for n_reps < 2")

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        fallback_df, fallback_src = load_experiment_telemetry(
            tmp_path, "flip_bitmap_unaligned"
        )
        assert "synthetic" in fallback_src  # ruff: ignore[assert]
        assert fallback_df.height == 180  # ruff: ignore[assert]

        latest_dir = tmp_path / "latest" / "flip_bitmap_unaligned"
        latest_dir.mkdir(parents=True)
        (latest_dir / "telemetry.json").write_text(
            json.dumps({
                "benchmarks": {
                    "64x64": {
                        "baseline": [100.0, 102.0, 99.0, 101.0],
                        "candidate": [70.0, 71.0, 69.5, 70.5],
                    }
                }
            }),
            encoding="utf-8",
        )
        live_df, live_src = load_experiment_telemetry(tmp_path, "flip_bitmap_unaligned")
        assert live_src.startswith("live")  # ruff: ignore[assert]
        assert live_df.height == 8  # ruff: ignore[assert]

    geo_df = df_a.filter(pl.col("geometry") == "1366x768")
    base = extract_variant_latencies(geo_df, "baseline")
    cand = extract_variant_latencies(geo_df, "candidate")
    summary = compute_best_posterior_summary(base, cand, rope_fraction=0.02)
    prob_sum = summary["p_better"] + summary["p_same"] + summary["p_worse"]
    assert math.isclose(prob_sum, 1.0, abs_tol=1e-9)  # ruff: ignore[assert]
    assert summary["hdi_low_pct"] <= summary["hdi_high_pct"]  # ruff: ignore[assert]
    assert summary["hdi_low_ns"] <= summary["hdi_high_ns"]  # ruff: ignore[assert]
    assert summary["p_better"] > 0.95  # ruff: ignore[assert]

    wide_rope = compute_best_posterior_summary(base, cand, rope_fraction=0.50)
    assert wide_rope["p_same"] >= summary["p_same"]  # ruff: ignore[assert]

    parsed = parse_cli_explorer_params({
        "": "",
        "geometry": "64x64",
        "rope": "0.03",
        "n-reps": "15",
        "theme": "plotly_white",
    })
    assert parsed.geometry == "64x64"  # ruff: ignore[assert]
    assert math.isclose(parsed.rope_fraction, 0.03)  # ruff: ignore[assert]
    assert parsed.n_reps == 15  # ruff: ignore[assert]
    assert parsed.theme == "plotly_white"  # ruff: ignore[assert]

    geometries = discover_available_geometries(df_a)
    assert geometries == ["64x64", "1366x768", "4096x2160"]  # ruff: ignore[assert]
    assert select_active_geometry(geometries, "missing") == "1366x768"  # ruff: ignore[assert]
    assert resolve_plotly_template("auto", "dark") == "plotly_dark"  # ruff: ignore[assert]
    assert resolve_plotly_template("auto", "light") == "plotly_white"  # ruff: ignore[assert]
    assert resolve_plotly_template("plotly_dark", "light") == "plotly_dark"  # ruff: ignore[assert]


@app.cell
def render_header() -> None:
    """Renders the notebook title and Bayesian BEST + ROPE overview."""
    mo.md(
        """
        # Benchmark Telemetry Explorer (Bayesian BEST + ROPE)

        Reactive analysis of C++23 `systems::bitmap_flip` microbenchmarks comparing
        **baseline** vs. **candidate** repetition latencies with Kruschke's Student-$t$
        BEST model, Region of Practical Equivalence (ROPE), and `arviz_stats` 95% HDI
        diagnostics.
        """
    )


@app.cell
def resolve_cli_and_source() -> tuple[ExplorerParams, mo.ui.dropdown, bool]:
    """Resolves script/CLI configuration and builds the experiment selector.

    Returns:
        Tuple of ``(cli_params, experiment_dropdown, is_script_mode)``.
    """
    is_script_mode = mo.app_meta().mode == "script"
    _empty_cli: dict[str, object] = {}
    _raw_cli = (
        cast("Mapping[str, object]", mo.cli_args()) if is_script_mode else _empty_cli
    )
    if is_script_mode and ("help" in _raw_cli or "h" in _raw_cli):
        _help_text = format_cli_help()
        sys.stdout.write(f"{_help_text}\n")
        mo.stop(True, mo.md(f"```text\n{_help_text}```"))

    cli_params = parse_cli_explorer_params(_raw_cli)
    _experiments = discover_available_experiments(
        cli_params.data_root,
        preferred=cli_params.experiment,
    )
    experiment_dropdown = mo.ui.dropdown(
        options=_experiments,
        value=cli_params.experiment,
        label="Benchmark Experiment",
    )
    mo.hstack([experiment_dropdown], justify="start")
    return cli_params, experiment_dropdown, is_script_mode


@app.cell
def load_source_telemetry(
    cli_params: ExplorerParams,
    experiment_dropdown: mo.ui.dropdown,
    is_script_mode: bool,
) -> tuple[str, list[str], str, pl.DataFrame, str]:
    """Loads source telemetry once for the active experiment and discovers geometries.

    Args:
        cli_params: Parsed script/CLI defaults and overrides.
        experiment_dropdown: Experiment selector widget.
        is_script_mode: Whether executing headless via ``app.run()``.

    Returns:
        Tuple of ``(active_experiment, available_geometries, default_geometry,
        raw_telemetry_df, raw_telemetry_source)``.
    """
    active_experiment = (
        cli_params.experiment if is_script_mode else str(experiment_dropdown.value)
    )
    raw_telemetry_df, raw_telemetry_source = load_experiment_telemetry(
        cli_params.data_root, active_experiment
    )
    available_geometries = discover_available_geometries(raw_telemetry_df)
    default_geometry = select_active_geometry(available_geometries, cli_params.geometry)
    mo.callout(
        mo.md(
            f"**Experiment:** `{active_experiment}` | "
            f"**Source:** `{raw_telemetry_source}` | "
            f"**Geometries:** `{', '.join(available_geometries)}`"
        ),
        kind="info",
    )
    return (
        active_experiment,
        available_geometries,
        default_geometry,
        raw_telemetry_df,
        raw_telemetry_source,
    )


@app.cell
def create_controls(
    available_geometries: list[str],
    cli_params: ExplorerParams,
    default_geometry: str,
) -> tuple[mo.ui.form[dict[str, object], dict[str, object]], mo.ui.radio]:
    """Creates batched form controls and a separate reactive theme switcher.

    Args:
        available_geometries: Dynamically discovered geometries from loaded telemetry.
        cli_params: Parsed defaults/overrides for initial widget values.
        default_geometry: Canonical or requested default geometry present in options.

    Returns:
        Tuple of ``(param_form, theme_switch)``.
    """
    _geometry_control = mo.ui.dropdown(
        options=available_geometries,
        value=default_geometry,
        label="Bitmap Geometry",
    )
    _rope_control = mo.ui.slider(
        start=0.005,
        stop=0.10,
        step=0.005,
        value=min(max(cli_params.rope_fraction, 0.005), 0.10),
        label="ROPE Half-Width (fraction)",
    )
    _sample_control = mo.ui.slider(
        start=10,
        stop=100,
        step=5,
        value=min(max(cli_params.n_reps, 10), 100),
        label="Sample Repetitions (N)",
    )
    param_form: mo.ui.form[dict[str, object], dict[str, object]] = (
        mo
        .md(
            """
            ### Analysis Parameters

            {geometry} {rope_fraction} {n_reps}
            """
        )
        .batch(
            geometry=_geometry_control,
            rope_fraction=_rope_control,
            n_reps=_sample_control,
        )
        .form(
            submit_button_label="Apply Parameters",
            clear_on_submit=False,
        )
    )
    theme_switch = mo.ui.radio(
        options=["auto", "plotly_dark", "plotly_white"],
        value=cli_params.theme,
        inline=True,
        label="Plotly Theme",
    )
    mo.vstack([param_form, theme_switch], gap=1)
    return param_form, theme_switch


@app.cell
def resolve_analysis_params(
    available_geometries: list[str],
    cli_params: ExplorerParams,
    is_script_mode: bool,
    param_form: mo.ui.form[dict[str, object], dict[str, object]],
) -> tuple[str, int, float]:
    """Resolves active analysis parameters from CLI flags or batched form submission.

    Args:
        available_geometries: Dynamically discovered geometry options.
        cli_params: Validated script/CLI parameter model.
        is_script_mode: Whether running in headless script mode.
        param_form: Batched parameter form widget.

    Returns:
        Tuple of ``(active_geometry, active_n_reps, active_rope)``.

    Raises:
        ValueError: If interactive form submission unexpectedly remains ``None``.
    """
    if is_script_mode:
        active_geometry = select_active_geometry(
            available_geometries, cli_params.geometry
        )
        active_rope = cli_params.rope_fraction
        active_n_reps = cli_params.n_reps
        _mode_label = "script/cli"
    else:
        _submitted = param_form.value
        mo.stop(
            _submitted is None,
            mo.callout(
                "Configure geometry, ROPE, and repetitions above and click "
                "**Apply Parameters** to compute Bayesian BEST diagnostics.",
                kind="info",
            ),
        )
        if _submitted is None:
            msg = "Form parameters were not submitted"
            raise ValueError(msg)
        active_geometry = select_active_geometry(
            available_geometries, str(_submitted["geometry"])
        )
        active_rope = float(cast("float | int | str", _submitted["rope_fraction"]))
        active_n_reps = int(cast("int | float | str", _submitted["n_reps"]))
        _mode_label = "interactive-form"

    mo.callout(
        mo.md(
            f"**Mode:** `{_mode_label}` | "
            f"**Geometry:** `{active_geometry}` | "
            f"**ROPE:** `±{active_rope * 100.0:.2f}%` | "
            f"**Repetition Cap:** `{active_n_reps}`"
        ),
        kind="neutral",
    )
    return active_geometry, active_n_reps, active_rope


@app.cell
def run_best_analysis(
    active_geometry: str,
    active_n_reps: int,
    active_rope: float,
    raw_telemetry_df: pl.DataFrame,
    raw_telemetry_source: str,
) -> tuple[
    NDArray[np.float64],
    NDArray[np.float64],
    NDArray[np.float64],
    dict[str, float],
    pl.DataFrame,
    pl.DataFrame,
    str,
]:
    """Slices telemetry and runs single-pass Bayesian BEST posterior diagnostics.

    Args:
        active_geometry: Selected benchmark geometry label.
        active_n_reps: Repetition depth cap.
        active_rope: ROPE half-width fraction.
        raw_telemetry_df: Loaded source telemetry DataFrame.
        raw_telemetry_source: Label describing loaded telemetry source.

    Returns:
        Tuple containing baseline latencies, candidate latencies, posterior
        percentage draws, summary metrics, filtered DataFrame, full DataFrame,
        and source label.
    """
    telemetry_df, selected_df, _resolved_geo, telemetry_source = (
        slice_telemetry_by_geometry_and_reps(
            raw_telemetry_df,
            raw_telemetry_source,
            active_geometry,
            active_n_reps,
        )
    )
    baseline_ns = extract_variant_latencies(selected_df, "baseline")
    candidate_ns = extract_variant_latencies(selected_df, "candidate")

    mo.stop(
        len(baseline_ns) < 2 or len(candidate_ns) < 2,
        mo.callout(
            f"Need >= 2 baseline and candidate samples for {_resolved_geo}.",
            kind="warn",
        ),
    )

    _delta_rel, _delta_ns, _params = sample_best_posterior_deltas(
        baseline_ns, candidate_ns
    )
    posterior_summary = summarize_best_posterior_draws(
        _delta_rel,
        _delta_ns,
        _params,
        rope_fraction=active_rope,
    )
    posterior_deltas_pct = (_delta_rel * 100.0).ravel()

    mo.callout(
        mo.md(
            f"**Data Source:** `{telemetry_source}` | "
            f"**Geometry:** `{_resolved_geo}` | "
            f"**Baseline N:** `{len(baseline_ns)}` | "
            f"**Candidate N:** `{len(candidate_ns)}`"
        ),
        kind="info",
    )
    return (
        baseline_ns,
        candidate_ns,
        posterior_deltas_pct,
        posterior_summary,
        selected_df,
        telemetry_df,
        telemetry_source,
    )


@app.cell
def render_kpi_cards(posterior_summary: dict[str, float]) -> None:
    """Displays KPI summary cards for latency, speedup, ROPE decision, and HDI.

    Args:
        posterior_summary: Dictionary returned by ``summarize_best_posterior_draws``.
    """
    build_kpi_row(posterior_summary)


@app.cell
def render_posterior_chart(
    active_geometry: str,
    posterior_deltas_pct: NDArray[np.float64],
    posterior_summary: dict[str, float],
    theme_switch: mo.ui.radio,
) -> tuple[mo.ui.plotly, str]:
    """Builds the interactive Plotly posterior ROPE and HDI histogram chart.

    Args:
        active_geometry: Selected bitmap geometry label.
        posterior_deltas_pct: 1D posterior draws of relative latency change (%).
        posterior_summary: Computed BEST posterior summary dictionary.
        theme_switch: Plotly theme selector (``auto`` / ``plotly_dark`` /
            ``plotly_white``).

    Returns:
        Tuple of ``(posterior_chart, resolved_template)``.
    """
    resolved_template = resolve_plotly_template(
        str(theme_switch.value), mo.app_meta().theme
    )
    posterior_chart = mo.ui.plotly(
        build_posterior_figure(
            posterior_deltas_pct,
            posterior_summary,
            active_geometry,
            resolved_template,
        )
    )
    mo.vstack([posterior_chart])
    return posterior_chart, resolved_template


@app.cell
def render_distribution_chart(
    active_geometry: str,
    baseline_ns: NDArray[np.float64],
    candidate_ns: NDArray[np.float64],
    theme_switch: mo.ui.radio,
) -> tuple[mo.ui.plotly]:
    """Builds the interactive Plotly baseline vs. candidate latency boxplot.

    Args:
        active_geometry: Selected bitmap geometry label.
        baseline_ns: Exported baseline latency array in nanoseconds.
        candidate_ns: Exported candidate latency array in nanoseconds.
        theme_switch: Plotly theme selector (``auto`` / ``plotly_dark`` /
            ``plotly_white``).

    Returns:
        Single-element tuple containing ``(distribution_chart,)``.
    """
    _template = resolve_plotly_template(str(theme_switch.value), mo.app_meta().theme)
    distribution_chart = mo.ui.plotly(
        build_distribution_figure(
            baseline_ns,
            candidate_ns,
            active_geometry,
            _template,
        )
    )
    mo.vstack([distribution_chart])
    return (distribution_chart,)


@app.cell
def render_telemetry_table(telemetry_df: pl.DataFrame) -> tuple[mo.ui.table]:
    """Displays an interactive, downloadable Polars telemetry table.

    Args:
        telemetry_df: Full telemetry Polars DataFrame across geometries.

    Returns:
        Single-element tuple containing the interactive ``mo.ui.table`` widget.
    """
    telemetry_table = mo.ui.table(
        data=telemetry_df,
        pagination=True,
        page_size=12,
        selection="multi",
        show_column_summaries=True,
        show_download=True,
    )
    mo.vstack([telemetry_table])
    return (telemetry_table,)


@app.cell
def test_telemetry_explorer_invariants() -> None:
    """Embedded pytest suite verifying synthetic telemetry, fallback, and BEST invariants."""
    verify_telemetry_invariants()


if __name__ == "__main__":
    app.run()
