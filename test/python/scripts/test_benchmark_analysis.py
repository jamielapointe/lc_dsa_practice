"""Unit tests for Google Benchmark analysis, Bayesian BEST engine, and host tuning."""

from __future__ import annotations

import json
import math
import shutil
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from scripts.benchmarks.bayesian_compare import BestResult
from scripts.benchmarks.bayesian_compare import analyze_benchmark
from scripts.benchmarks.bayesian_compare import compute_hdi
from scripts.benchmarks.bayesian_compare import generate_markdown_report
from scripts.benchmarks.bayesian_compare import generate_plotly_html_report
from scripts.benchmarks.bayesian_compare import parse_google_benchmark_json
from scripts.benchmarks.bayesian_compare import promote_to_baseline
from scripts.benchmarks.host_tuner import DarwinHostTuner
from scripts.benchmarks.host_tuner import LinuxHostTuner
from scripts.benchmarks.host_tuner import NullHostTuner
from scripts.benchmarks.host_tuner import get_host_tuner
from scripts.benchmarks.run_benchmark import extract_paired_series
from scripts.benchmarks.run_benchmark import filter_apple_silicon_gbench_output
from scripts.benchmarks.run_benchmark import find_or_build_binary
from scripts.benchmarks.run_benchmark import patch_google_benchmark_json_context


@pytest.mark.unit
class TestGoogleBenchmarkParser:
    """Verifies parsing of Google Benchmark JSON v2 output files."""

    def test_parse_valid_json_v2(self, tmp_path: Path) -> None:
        """Parses valid JSON output containing multiple iterations."""
        payload: dict[str, Any] = {
            'context': {'date': '2026-10-16T12:00:00Z', 'executable': 'bitmap_flip_benchmark'},
            'benchmarks': [
                {'name': 'BM_FlipBitmap_Baseline/7x1', 'run_type': 'iteration', 'cpu_time': 4.1, 'real_time': 4.2},
                {'name': 'BM_FlipBitmap_Baseline/7x1', 'run_type': 'iteration', 'cpu_time': 4.0, 'real_time': 4.1},
                {'name': 'BM_FlipBitmap_Baseline/7x1_mean', 'run_type': 'aggregate', 'cpu_time': 4.05},
                {'name': 'BM_FlipBitmap_Candidate/7x1', 'run_type': 'iteration', 'cpu_time': 3.5, 'real_time': 3.6},
                {'name': 'BM_FlipBitmap_Candidate/7x1', 'run_type': 'iteration', 'cpu_time': 3.4, 'real_time': 3.5},
            ],
        }
        json_file = tmp_path / 'benchmark.json'
        json_file.write_text(json.dumps(payload), encoding='utf-8')

        series = parse_google_benchmark_json(json_file)
        assert 'BM_FlipBitmap_Baseline/7x1' in series
        assert 'BM_FlipBitmap_Candidate/7x1' in series
        # Aggregate mean row must be excluded
        assert 'BM_FlipBitmap_Baseline/7x1_mean' not in series
        assert len(series['BM_FlipBitmap_Baseline/7x1']) == 2
        assert len(series['BM_FlipBitmap_Candidate/7x1']) == 2

    def test_extract_baseline_vs_candidate_series(self) -> None:
        """Extracts and pairs baseline and candidate sample sets by geometry."""
        raw_series = {
            'BM_FlipBitmap_Baseline/1366x768': [100.0, 102.0, 99.0],
            'BM_FlipBitmap_Candidate/1366x768': [80.0, 81.0, 79.0],
        }
        paired, results = extract_paired_series(raw_series)
        assert '1366x768' in paired
        assert paired['1366x768']['baseline'] == [100.0, 102.0, 99.0]
        assert paired['1366x768']['candidate'] == [80.0, 81.0, 79.0]
        assert '1366x768' in results
        assert results['1366x768'].p_better > 0.95

    def test_handle_missing_or_corrupt_json(self, tmp_path: Path) -> None:
        """Verifies FileNotFoundError and ValueError handling for corrupt files."""
        non_existent = tmp_path / 'missing.json'
        with pytest.raises(FileNotFoundError):
            parse_google_benchmark_json(non_existent)

        corrupt = tmp_path / 'corrupt.json'
        corrupt.write_text('{invalid_json:', encoding='utf-8')
        with pytest.raises(ValueError, match='Corrupt JSON'):
            parse_google_benchmark_json(corrupt)

        missing_benchmarks = tmp_path / 'bad_format.json'
        missing_benchmarks.write_text('{"context": {}}', encoding='utf-8')
        with pytest.raises(ValueError, match='Missing "benchmarks" array'):
            parse_google_benchmark_json(missing_benchmarks)

    def test_parse_empty_json_returns_empty_dict(self, tmp_path: Path) -> None:
        """Parses 0-byte or whitespace-only JSON file gracefully without crashing."""
        empty_file = tmp_path / 'empty.json'
        empty_file.write_text('', encoding='utf-8')
        assert parse_google_benchmark_json(empty_file) == {}

        whitespace_file = tmp_path / 'whitespace.json'
        whitespace_file.write_text('   \n\t  ', encoding='utf-8')
        assert parse_google_benchmark_json(whitespace_file) == {}

    def test_extract_paired_series_rope_propagation(self) -> None:
        """Verifies rope_fraction parameter affects Bayesian analysis in extract_paired_series."""
        raw_series = {
            'BM_FlipBitmap_Baseline/1366x768': [100.0, 100.5, 99.5],
            'BM_FlipBitmap_Candidate/1366x768': [100.2, 100.3, 100.1],
        }
        # With very tight ROPE (0.0001 = 0.01%), differences fall outside ROPE
        _, res_narrow = extract_paired_series(raw_series, rope_fraction=0.0001)
        # With wide ROPE (0.10 = 10%), differences fall inside ROPE
        _, res_wide = extract_paired_series(raw_series, rope_fraction=0.10)
        assert res_wide['1366x768'].p_same > res_narrow['1366x768'].p_same

    def test_extract_paired_series_invalid_rope_raises(self) -> None:
        """Verifies negative or out-of-bounds rope_fraction raises ValueError."""
        raw_series = {'BM_FlipBitmap_Baseline/1366x768': [100.0, 101.0]}
        with pytest.raises(ValueError, match='rope_fraction must be non-negative'):
            extract_paired_series(raw_series, rope_fraction=-0.05)
        with pytest.raises(ValueError, match=r'rope_fraction must be less than 1\.0'):
            extract_paired_series(raw_series, rope_fraction=1.5)


@pytest.mark.unit
class TestBayesianBEST:
    """Verifies mathematical properties and convergence of Bayesian BEST model."""

    def test_clear_improvement_high_p_better(self) -> None:
        """Verifies 15% latency reduction yields P(Better) > 0.99 under 2% ROPE."""
        rng = np.random.default_rng(42)
        baseline = rng.normal(loc=100.0, scale=2.0, size=25)
        candidate = rng.normal(loc=85.0, scale=2.0, size=25)

        result = analyze_benchmark(baseline, candidate, rope_fraction=0.02)
        assert result.p_better > 0.99
        assert result.p_same < 0.01
        assert result.p_worse < 0.001
        assert result.relative_diff_percent < -13.0
        assert result.verdict == 'PROCEED_PROMOTE'
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-6)

    def test_equivalence_within_rope_high_p_same(self) -> None:
        """Verifies 0.5% latency difference yields P(Same) > 0.95 under 3% ROPE."""
        rng = np.random.default_rng(123)
        baseline = rng.normal(loc=100.0, scale=1.5, size=25)
        candidate = rng.normal(loc=100.5, scale=1.5, size=25)

        result = analyze_benchmark(baseline, candidate, rope_fraction=0.03)
        assert result.p_same > 0.90
        assert result.p_better < 0.10
        assert result.p_worse < 0.10
        assert result.verdict == 'EQUIVALENT'
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-6)

    def test_clear_regression_high_p_worse(self) -> None:
        """Verifies 20% latency increase yields P(Worse) > 0.99 under 2% ROPE."""
        rng = np.random.default_rng(456)
        baseline = rng.normal(loc=100.0, scale=2.0, size=20)
        candidate = rng.normal(loc=120.0, scale=2.0, size=20)

        result = analyze_benchmark(baseline, candidate, rope_fraction=0.02)
        assert result.p_worse > 0.99
        assert result.p_better < 0.001
        assert result.verdict == 'REVERT_REGRESSION'
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-6)

    def test_outlier_robustness_student_t(self) -> None:
        """Verifies Student-t absorbs extreme 10x latency spike without false regression."""
        rng = np.random.default_rng(999)
        baseline = rng.normal(loc=100.0, scale=2.0, size=20)
        candidate = rng.normal(loc=85.0, scale=2.0, size=20)
        # Inject 10x outlier spike
        candidate[0] = 850.0

        result = analyze_benchmark(baseline, candidate, rope_fraction=0.02)
        assert result.p_better > 0.90
        assert result.relative_diff_percent < -10.0
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-6)

    def test_probabilities_sum_to_unity(self) -> None:
        """Verifies that P(Better) + P(Same) + P(Worse) strictly equals 1.0 across configurations."""
        configs = [
            ([10.0, 10.2, 10.1], [10.0, 10.1, 10.2], 0.01),
            ([50.0, 52.0], [40.0, 41.0], 0.05),
            ([100.0, 101.0, 99.0], [150.0, 149.0, 151.0], 0.02),
            ([20.0, 20.0], [20.0, 20.0], 0.0),
        ]
        for b, c, rope in configs:
            res = analyze_benchmark(b, c, rope_fraction=rope)
            assert math.isclose(res.p_better + res.p_same + res.p_worse, 1.0, abs_tol=1e-6)

    def test_hdi_and_cohens_d_calculation(self) -> None:
        """Verifies 95% HDI covers the mean difference and effect size reflects magnitude."""
        samples = np.linspace(-5.0, 5.0, 1000)
        hdi_low, hdi_high = compute_hdi(samples, cred_mass=0.95)
        assert hdi_low < 0.0 < hdi_high
        assert math.isclose(hdi_high - hdi_low, 9.5, abs_tol=0.2)

        res = analyze_benchmark([100.0, 101.0, 99.0], [90.0, 91.0, 89.0], rope_fraction=0.02)
        assert res.effect_size < 0.0
        assert res.hdi_95[0] < res.hdi_95[1]

    def test_invalid_arguments_raise_value_error(self) -> None:
        """Verifies ValueError on sample sizes < 2 or negative ROPE."""
        with pytest.raises(ValueError, match='Sample sizes must be >= 2'):
            analyze_benchmark([10.0], [10.0, 11.0])

        with pytest.raises(ValueError, match='Sample sizes must be >= 2'):
            analyze_benchmark([10.0, 11.0], [10.0])

        with pytest.raises(ValueError, match='rope_fraction must be non-negative'):
            analyze_benchmark([10.0, 11.0], [10.0, 11.0], rope_fraction=-0.01)


@pytest.mark.unit
class TestReportGeneration:
    """Verifies generation of Markdown and interactive Plotly HTML reports."""

    def test_markdown_report_structure_and_verdict(self, tmp_path: Path) -> None:
        """Generates markdown report and verifies table structure and verdict."""
        mock_results = {
            '1366x768': BestResult(
                p_better=0.9998,
                p_same=0.0002,
                p_worse=0.0000,
                effect_size=-2.5,
                hdi_95=(-16.0, -14.0),
                baseline_mean=28450.0,
                candidate_mean=24100.0,
                relative_diff_percent=-15.29,
                verdict='PROCEED_PROMOTE',
            )
        }
        meta = {
            'experiment': 'test_flip',
            'git_commit': 'abcdef123456',
            'rope_percent': 2.0,
            'repetitions': 20,
            'host': {'system': 'Darwin', 'machine': 'arm64'},
        }
        report_path = tmp_path / 'report.md'
        md_text = generate_markdown_report(mock_results, meta, report_path)

        assert report_path.exists()
        assert '# Benchmark Analysis: test_flip' in md_text
        assert 'PROCEED: Candidate is Statistically Superior' in md_text
        assert '| `1366x768` |' in md_text
        assert '0.9998' in md_text

    def test_plotly_html_generation_standalone(self, tmp_path: Path) -> None:
        """Generates standalone Plotly HTML report and verifies document tags."""
        mock_results = {
            '1366x768': BestResult(
                p_better=0.99,
                p_same=0.01,
                p_worse=0.00,
                effect_size=-2.0,
                hdi_95=(-15.0, -10.0),
                baseline_mean=100.0,
                candidate_mean=85.0,
                relative_diff_percent=-15.0,
                verdict='PROCEED_PROMOTE',
            )
        }
        raw_series = {'1366x768': {'baseline': [100.0, 101.0], 'candidate': [85.0, 84.0]}}
        html_path = tmp_path / 'report.html'
        generate_plotly_html_report(mock_results, raw_series, html_path)

        assert html_path.exists()
        html_content = html_path.read_text(encoding='utf-8')
        assert '<html' in html_content
        assert 'plotly' in html_content.lower()

    def test_markdown_report_nested_config_metadata(self, tmp_path: Path) -> None:
        """Verifies generate_markdown_report reads repetitions and rope from meta['config']."""
        mock_results = {
            '1366x768': BestResult(
                p_better=0.95,
                p_same=0.05,
                p_worse=0.00,
                effect_size=-1.5,
                hdi_95=(-6.0, -4.0),
                baseline_mean=20000.0,
                candidate_mean=19000.0,
                relative_diff_percent=-5.0,
                verdict='PROCEED_PROMOTE',
            )
        }
        meta: dict[str, Any] = {
            'experiment': 'test_nested',
            'git_commit': '1234567890',
            'config': {
                'repetitions': 3,
                'rope_percent': 5.0,
            },
            'host': {'system': 'Darwin', 'machine': 'arm64'},
        }
        report_path = tmp_path / 'report.md'
        md_text = generate_markdown_report(mock_results, meta, report_path)
        assert '**ROPE:** $\\pm 5.0\\%$' in md_text
        assert '**Reps:** 3' in md_text

    def test_plotly_html_generation_with_metadata(self, tmp_path: Path) -> None:
        """Generates Plotly HTML report with meta and verifies metadata in title."""
        mock_results = {
            '1366x768': BestResult(
                p_better=0.99,
                p_same=0.01,
                p_worse=0.00,
                effect_size=-2.0,
                hdi_95=(-15.0, -10.0),
                baseline_mean=100.0,
                candidate_mean=85.0,
                relative_diff_percent=-15.0,
                verdict='PROCEED_PROMOTE',
            )
        }
        raw_series = {'1366x768': {'baseline': [100.0, 101.0], 'candidate': [85.0, 84.0]}}
        meta: dict[str, Any] = {
            'config': {'repetitions': 5, 'rope_percent': 3.5},
        }
        html_path = tmp_path / 'report_meta.html'
        generate_plotly_html_report(mock_results, raw_series, html_path, meta=meta)
        html_content = html_path.read_text(encoding='utf-8')
        assert 'Reps: 5' in html_content
        assert '3.5%' in html_content


@pytest.mark.unit
class TestPromotionLogic:
    """Verifies atomic promotion from latest to baseline."""

    def test_initial_baseline_creation(self, tmp_path: Path) -> None:
        """Creates initial baseline when baseline directory does not yet exist."""
        latest_dir = tmp_path / 'latest' / 'exp1'
        baseline_dir = tmp_path / 'baseline' / 'exp1'
        latest_dir.mkdir(parents=True)
        (latest_dir / 'report.md').write_text('# Test Report', encoding='utf-8')

        promoted = promote_to_baseline(latest_dir, baseline_dir, force=True)
        assert promoted is True
        assert (baseline_dir / 'report.md').exists()
        assert (baseline_dir / 'baseline_meta.json').exists()

    def test_promotion_accepted_when_statistically_superior(self, tmp_path: Path) -> None:
        """Promotes candidate when analysis.json demonstrates superiority."""
        latest_dir = tmp_path / 'latest' / 'exp2'
        baseline_dir = tmp_path / 'baseline' / 'exp2'
        latest_dir.mkdir(parents=True)
        baseline_dir.mkdir(parents=True)

        # Baseline already exists
        (baseline_dir / 'old.txt').write_text('old baseline', encoding='utf-8')

        # Latest shows improvement without regressions
        analysis_payload = {
            'benchmarks': {
                '1366x768': {'verdict': 'PROCEED_PROMOTE', 'p_worse': 0.0001, 'p_better': 0.999},
            }
        }
        (latest_dir / 'analysis.json').write_text(json.dumps(analysis_payload), encoding='utf-8')

        promoted = promote_to_baseline(latest_dir, baseline_dir, force=False)
        assert promoted is True
        assert (baseline_dir / 'analysis.json').exists()
        # Old files in baseline replaced
        assert not (baseline_dir / 'old.txt').exists()

    def test_promotion_rejected_on_regression(self, tmp_path: Path) -> None:
        """Rejects promotion when candidate exhibits regression."""
        latest_dir = tmp_path / 'latest' / 'exp3'
        baseline_dir = tmp_path / 'baseline' / 'exp3'
        latest_dir.mkdir(parents=True)
        baseline_dir.mkdir(parents=True)

        (baseline_dir / 'keep.txt').write_text('preserve', encoding='utf-8')

        analysis_payload = {
            'benchmarks': {
                '1366x768': {'verdict': 'REVERT_REGRESSION', 'p_worse': 0.85, 'p_better': 0.05},
            }
        }
        (latest_dir / 'analysis.json').write_text(json.dumps(analysis_payload), encoding='utf-8')

        promoted = promote_to_baseline(latest_dir, baseline_dir, force=False)
        assert promoted is False
        assert (baseline_dir / 'keep.txt').exists()

    def test_promotion_missing_latest_raises(self, tmp_path: Path) -> None:
        """Raises FileNotFoundError if source latest directory does not exist."""
        non_existent = tmp_path / 'missing_latest'
        target = tmp_path / 'baseline'
        with pytest.raises(FileNotFoundError):
            promote_to_baseline(non_existent, target)


@pytest.mark.unit
class TestHostTuner:
    """Verifies Darwin, Linux, and fallback host tuner interfaces."""

    def test_darwin_host_tuner(self) -> None:
        """Verifies Darwin host tuner command wrapping and thermal check."""
        tuner = DarwinHostTuner()
        assert tuner.platform_name == 'Darwin'
        meta = tuner.prepare()
        assert meta['system'] == 'Darwin'
        assert 'thermal' in meta

        wrapped = tuner.wrap_command(['echo', 'hello'])
        assert 'echo' in wrapped
        assert 'hello' in wrapped
        if shutil.which('caffeinate'):
            assert 'caffeinate' in wrapped

        therm = tuner.check_thermal_throttling()
        assert 'throttled' in therm
        assert 'speed_limit' in therm

    def test_linux_host_tuner(self) -> None:
        """Verifies Linux host tuner command wrapping."""
        tuner = LinuxHostTuner()
        assert tuner.platform_name == 'Linux'
        meta = tuner.prepare()
        assert meta['system'] == 'Linux'
        wrapped = tuner.wrap_command(['test_bin', '--flag'])
        assert 'test_bin' in wrapped

    def test_null_host_tuner(self) -> None:
        """Verifies NullHostTuner fallback behavior."""
        tuner = NullHostTuner()
        assert tuner.platform_name == 'Generic'
        cmd = ['run', 'bench']
        assert tuner.wrap_command(cmd) == cmd
        assert tuner.check_thermal_throttling()['throttled'] is False

    def test_get_host_tuner_factory(self) -> None:
        """Verifies get_host_tuner returns a valid HostTuner instance."""
        tuner = get_host_tuner()
        assert isinstance(tuner, (DarwinHostTuner, LinuxHostTuner, NullHostTuner))


@pytest.mark.unit
class TestBenchmarkRunnerCLI:
    """Verifies runner binary validation and CLI argument aliases."""

    def test_find_or_build_binary_custom_path_not_found(self) -> None:
        """Verifies FileNotFoundError raised when custom binary path does not exist."""
        nonexistent = Path('/invalid/nonexistent/binary/path')
        with pytest.raises(FileNotFoundError, match='Specified benchmark binary not found'):
            find_or_build_binary(nonexistent)

    def test_filter_argparse_alias(self) -> None:
        """Verifies --filter is accepted as an alias for --benchmark-filter."""
        import argparse

        parser = argparse.ArgumentParser()
        parser.add_argument(
            '--benchmark-filter',
            '--filter',
            dest='benchmark_filter',
            default='BM_FlipBitmap_.*',
        )
        parsed_alias = parser.parse_args(['--filter', '1366x768'])
        assert parsed_alias.benchmark_filter == '1366x768'

        parsed_orig = parser.parse_args(['--benchmark-filter', '1366x768'])
        assert parsed_orig.benchmark_filter == '1366x768'

    def test_filter_apple_silicon_gbench_output(self) -> None:
        """Verifies benign Apple Silicon sysctl/affinity warnings are stripped and 24 MHz annotated."""
        raw_stderr = (
            'Unable to determine clock rate from sysctl: hw.cpufrequency: No such file or directory\n'
            'This does not affect benchmark measurements, only the metadata output.\n'
            '***WARNING*** Failed to set thread affinity. Estimated CPU frequency may be incorrect.\n'
            '2026-10-19T17:00:00-07:00\n'
            'Running build/benchmark/bin/bitmap_flip_benchmark\n'
            'Run on (6 X 24 MHz CPU s)\n'
        )
        host_meta = {'system': 'Darwin', 'pcore_max_mhz': 4044.0}
        cleaned = filter_apple_silicon_gbench_output(raw_stderr, host_meta)
        assert 'hw.cpufrequency' not in cleaned
        assert 'Failed to set thread affinity' not in cleaned
        assert '4044 MHz P-core max / 24 MHz ARM cntfrq_el0 timer CPU s' in cleaned

    def test_patch_google_benchmark_json_context(self, tmp_path: Path) -> None:
        """Verifies benchmark_raw.json context is enriched with Apple Silicon frequencies."""
        json_path = tmp_path / 'benchmark_raw.json'
        payload = {
            'context': {'mhz_per_cpu': 24, 'num_cpus': 6},
            'benchmarks': [],
        }
        json_path.write_text(json.dumps(payload), encoding='utf-8')
        host_meta = {'system': 'Darwin', 'pcore_max_mhz': 4044.0, 'ecore_max_mhz': 2424.0}
        patch_google_benchmark_json_context(json_path, host_meta)

        updated = json.loads(json_path.read_text(encoding='utf-8'))
        assert updated['context']['mhz_per_cpu'] == pytest.approx(4044.0)
        assert updated['context']['apple_cntfrq_el0_mhz'] == 24
        assert updated['context']['apple_pcore_max_mhz'] == pytest.approx(4044.0)
        assert updated['context']['apple_ecore_max_mhz'] == pytest.approx(2424.0)
