"""Tests for the reactive Marimo benchmark telemetry explorer notebook."""

import json
import math
import subprocess
import sys
from pathlib import Path
from typing import cast

import numpy as np
import pytest
from pydantic import ValidationError

from notebooks import benchmark_telemetry_explorer as explorer_nb
from notebooks.benchmark_telemetry_explorer import (
    ExplorerParams,
    app,
    build_distribution_figure,
    build_posterior_figure,
    discover_available_experiments,
    discover_available_geometries,
    format_cli_help,
    load_experiment_telemetry,
    load_filtered_telemetry,
    parse_cli_explorer_params,
    resolve_plotly_template,
    select_active_geometry,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
NOTEBOOK_PATH = REPO_ROOT / "notebooks" / "benchmark_telemetry_explorer.py"


def test_app_run_executes_dag_with_visual_outputs() -> None:
    """Running app.run() produces non-None visual outputs and populated defs."""
    outputs, defs = app.run()

    # All 10 display/control @app.cell definitions emit non-None visual outputs;
    # the setup/helper slots and final embedded invariant test cell emit None.
    visual_outputs = [output for output in outputs if output is not None]
    assert len(visual_outputs) == 10
    assert outputs[-1] is None

    expected_defs = {
        "active_experiment",
        "active_geometry",
        "active_n_reps",
        "active_rope",
        "available_geometries",
        "baseline_ns",
        "candidate_ns",
        "cli_params",
        "distribution_chart",
        "experiment_dropdown",
        "param_form",
        "posterior_chart",
        "posterior_deltas_pct",
        "posterior_summary",
        "resolved_template",
        "telemetry_df",
        "telemetry_table",
        "theme_switch",
    }
    assert expected_defs.issubset(set(defs))
    available_geos = cast("list[str]", defs["available_geometries"])
    assert available_geos
    assert defs["active_geometry"] in available_geos

    summary = cast("dict[str, float]", defs["posterior_summary"])
    prob_sum = summary["p_better"] + summary["p_same"] + summary["p_worse"]
    assert math.isclose(prob_sum, 1.0, abs_tol=1e-9)
    assert summary["hdi_low_pct"] <= summary["hdi_high_pct"]
    assert math.isfinite(summary["rhat"])
    assert math.isfinite(summary["ess"])


def test_embedded_invariant_cell() -> None:
    """Directly invoking the embedded notebook pytest cell succeeds."""
    explorer_nb.test_telemetry_explorer_invariants()


def test_plotly_theme_resolution_and_figures() -> None:
    """Explicit and auto theme modes propagate into Plotly figure templates."""
    assert resolve_plotly_template("auto", "dark") == "plotly_dark"
    assert resolve_plotly_template("auto", "light") == "plotly_white"
    assert resolve_plotly_template("auto", None) == "plotly_white"
    assert resolve_plotly_template("plotly_dark", "light") == "plotly_dark"
    assert resolve_plotly_template("plotly_white", "dark") == "plotly_white"

    baseline = np.array([100.0, 102.0, 98.0, 101.0], dtype=np.float64)
    candidate = np.array([70.0, 71.0, 69.0, 70.5], dtype=np.float64)
    draws = np.array([-30.0, -29.5, -30.5, -30.0], dtype=np.float64)
    summary: dict[str, float] = {
        "rope_fraction": 0.02,
        "hdi_low_pct": -31.0,
        "hdi_high_pct": -29.0,
    }

    dark_posterior = build_posterior_figure(draws, summary, "1366x768", "plotly_dark")
    light_posterior = build_posterior_figure(draws, summary, "1366x768", "plotly_white")
    dark_dist = build_distribution_figure(
        baseline, candidate, "1366x768", "plotly_dark"
    )
    light_dist = build_distribution_figure(
        baseline, candidate, "1366x768", "plotly_white"
    )

    assert dark_posterior.to_plotly_json() != light_posterior.to_plotly_json()
    assert dark_dist.to_plotly_json() != light_dist.to_plotly_json()


def test_cli_args_parsing_and_validation(tmp_path: Path) -> None:
    """CLI parsing maps aliases, filters '--' empty keys, and enforces Pydantic bounds."""
    defaults = parse_cli_explorer_params({})
    assert defaults == ExplorerParams()

    custom_root = tmp_path / "custom_data"
    parsed = parse_cli_explorer_params({
        "": "",
        "v": "",
        "k": "ignored_pytest_flag",
        "data-root": str(custom_root),
        "experiment": "custom_exp",
        "geometry": "64x64",
        "rope": "0.0",
        "n-reps": ["10", "15"],
        "theme": "plotly_white",
    })
    assert parsed.data_root == custom_root
    assert parsed.experiment == "custom_exp"
    assert parsed.geometry == "64x64"
    assert math.isclose(parsed.rope_fraction, 0.0)
    assert parsed.n_reps == 15
    assert parsed.theme == "plotly_white"

    with pytest.raises(ValidationError):
        parse_cli_explorer_params({"rope": "-0.05"})

    with pytest.raises(ValidationError):
        parse_cli_explorer_params({"n-reps": "1"})

    with pytest.raises(ValidationError):
        parse_cli_explorer_params({"theme": "solarized"})

    help_text = format_cli_help()
    for flag in ("--data-root", "--experiment", "--geometry", "--rope", "--n-reps"):
        assert flag in help_text


def test_dynamic_geometry_discovery_and_non_mixing_fallback(tmp_path: Path) -> None:
    """Telemetry discovery adapts to live geometries and never mixes unmatched ones."""
    bench_dir = tmp_path / "latest" / "bitmap_flip"
    bench_dir.mkdir(parents=True)
    (bench_dir / "telemetry.json").write_text(
        json.dumps({
            "benchmarks": {
                "3839x2160": {
                    "baseline": [500.0, 505.0, 498.0],
                    "candidate": [320.0, 322.0, 319.0],
                },
                "1366x768": {
                    "baseline": [200.0, 202.0, 198.0],
                    "candidate": [140.0, 141.0, 139.0],
                },
                "7x1": {
                    "baseline": [15.0, 16.0, 14.5],
                    "candidate": [11.0, 11.2, 10.9],
                },
            }
        }),
        encoding="utf-8",
    )

    alt_dir = tmp_path / "latest" / "ring_buffer"
    alt_dir.mkdir(parents=True)
    (alt_dir / "telemetry.json").write_text(
        json.dumps({
            "benchmarks": {
                "128x1": {"baseline": [40.0, 41.0], "candidate": [30.0, 31.0]},
                "64x1": {"baseline": [20.0, 21.0], "candidate": [15.0, 16.0]},
            }
        }),
        encoding="utf-8",
    )

    experiments = discover_available_experiments(tmp_path, preferred="bitmap_flip")
    assert experiments == ["bitmap_flip", "ring_buffer"]

    live_df, live_source = load_experiment_telemetry(tmp_path, "bitmap_flip")
    assert live_source.startswith("live")
    geometries = discover_available_geometries(live_df)
    assert geometries == ["7x1", "1366x768", "3839x2160"]
    assert select_active_geometry(geometries, "missing") == "1366x768"

    _, selected_df, _ = load_filtered_telemetry(tmp_path, "bitmap_flip", "64x64", 30)
    assert selected_df["geometry"].unique().to_list() == ["1366x768"]

    ring_df, _ = load_experiment_telemetry(tmp_path, "ring_buffer")
    ring_geometries = discover_available_geometries(ring_df)
    assert ring_geometries == ["64x1", "128x1"]
    _, ring_selected, _ = load_filtered_telemetry(
        tmp_path, "ring_buffer", "missing", 10
    )
    assert ring_selected["geometry"].unique().to_list() == ["64x1"]


def test_app_run_respects_script_cli_overrides(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Headless script execution applies CLI flags to geometry, ROPE, N, and theme."""
    monkeypatch.setattr(
        sys,
        "argv",
        [
            str(NOTEBOOK_PATH),
            "--",
            "--data-root",
            str(tmp_path),
            "--geometry",
            "64x64",
            "--rope",
            "0.05",
            "--n-reps",
            "12",
            "--theme",
            "plotly_dark",
        ],
    )
    _, defs = app.run()
    assert defs["active_geometry"] == "64x64"
    assert defs["active_n_reps"] == 12
    assert math.isclose(cast("float", defs["active_rope"]), 0.05)
    assert defs["resolved_template"] == "plotly_dark"
    assert len(cast("np.ndarray", defs["baseline_ns"])) == 12
    assert len(cast("np.ndarray", defs["candidate_ns"])) == 12


def test_headless_cli_subprocess_modes(tmp_path: Path) -> None:
    """Running the notebook as a standalone script exits 0 without data/ and on --help."""
    bare_run = subprocess.run(
        [sys.executable, str(NOTEBOOK_PATH)],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        check=False,
    )
    assert bare_run.returncode == 0, bare_run.stderr

    help_run = subprocess.run(
        [sys.executable, str(NOTEBOOK_PATH), "--", "--help"],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        check=False,
    )
    assert help_run.returncode == 0, help_run.stderr
    assert "--geometry" in help_run.stdout
    assert "--n-reps" in help_run.stdout
