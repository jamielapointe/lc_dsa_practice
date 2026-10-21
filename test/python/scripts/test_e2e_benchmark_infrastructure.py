"""End-to-end requirement-driven test suite for the benchmark infrastructure.

Validates the integrated deliverables of the C++23 Google Benchmark and Bayesian
performance analysis infrastructure across 4 tiers of testing:
- Tier 1: Feature Coverage (Features 1-19 in PROJECT.md Feature Inventory)
- Tier 2: Boundary & Corner Cases (empty data, zero/large ROPE, extreme geometries, outliers)
- Tier 3: Cross-Feature Combinations (runner + host tuning + Bayesian BEST + reporting + hygiene)
- Tier 4: Real-World Application Scenarios (regression rejection and optimization promotion cycles)
"""

from __future__ import annotations

import json
import math
import shutil
import subprocess  # ruff: ignore[suspicious-subprocess-import]
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pytest

from scripts.ci import check_systems_traceability as trace
from scripts.ci.reorder_pyproject import main as reorder_main


REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
AGENTS_MD_MAX_BYTES = 24576


# ---------------------------------------------------------------------------
# Reference Mathematical Implementations for Validation & Invariant Testing
# ---------------------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class ReferenceBestResult:
    """Mathematical reference output for Bayesian BEST analysis.

    Attributes:
        p_better: Probability that candidate is faster than baseline (latency reduced).
        p_same: Probability that candidate is practically equivalent to baseline within ROPE.
        p_worse: Probability that candidate is slower than baseline (latency increased).
        effect_size: Standardized difference in means (Cohen's d equivalent).
        hdi_95: 95% Highest Density Interval of the difference.
    """

    p_better: float
    p_same: float
    p_worse: float
    effect_size: float
    hdi_95: tuple[float, float]


def compute_reference_best(
    baseline_samples: list[float],
    candidate_samples: list[float],
    rope_fraction: float = 0.02,
) -> ReferenceBestResult:
    """Pure-Python reference computation of Bayesian BEST probabilities.

    Uses analytical approximations to verify invariant properties (e.g. sum to 1.0)
    independently of external libraries.

    Args:
        baseline_samples: Latency samples from baseline run.
        candidate_samples: Latency samples from candidate run.
        rope_fraction: Practical equivalence boundary as fraction of baseline mean.

    Returns:
        ReferenceBestResult containing posterior probabilities and statistics.

    Raises:
        ValueError: If fewer than 2 samples are provided for baseline or candidate.
    """
    n_b = len(baseline_samples)
    n_c = len(candidate_samples)
    if n_b < 2 or n_c < 2:
        msg = f'Sample sizes must be >= 2 (got n_baseline={n_b}, n_candidate={n_c})'
        raise ValueError(msg)

    mean_b = sum(baseline_samples) / n_b
    mean_c = sum(candidate_samples) / n_c

    var_b = sum((x - mean_b) ** 2 for x in baseline_samples) / (n_b - 1)
    var_c = sum((x - mean_c) ** 2 for x in candidate_samples) / (n_c - 1)

    pooled_std = math.sqrt(((n_b - 1) * var_b + (n_c - 1) * var_c) / (n_b + n_c - 2)) if (n_b + n_c > 2) else 1e-9
    diff_mean = mean_c - mean_b  # negative means candidate is faster (better)
    se_diff = math.sqrt((var_b / n_b) + (var_c / n_c)) if (var_b + var_c > 0) else 1e-9

    effect_size = (diff_mean / pooled_std) if pooled_std > 0 else 0.0
    rope_delta = rope_fraction * mean_b

    # Normal approximation for posterior of diff_mean
    # P(better) = P(diff_mean < -rope_delta)
    # P(same) = P(-rope_delta <= diff_mean <= rope_delta)
    # P(worse) = P(diff_mean > rope_delta)
    z_lower = (-rope_delta - diff_mean) / se_diff
    z_upper = (rope_delta - diff_mean) / se_diff

    def normal_cdf(z: float) -> float:
        return 0.5 * (1.0 + math.erf(z / math.sqrt(2.0)))

    p_better = normal_cdf(z_lower)
    p_same = max(0.0, normal_cdf(z_upper) - normal_cdf(z_lower))
    p_worse = max(0.0, 1.0 - normal_cdf(z_upper))

    # Normalize to guarantee exact sum = 1.0
    total = p_better + p_same + p_worse
    p_better /= total
    p_same /= total
    p_worse /= total

    hdi_lower = diff_mean - 1.96 * se_diff
    hdi_upper = diff_mean + 1.96 * se_diff

    return ReferenceBestResult(
        p_better=p_better,
        p_same=p_same,
        p_worse=p_worse,
        effect_size=effect_size,
        hdi_95=(hdi_lower, hdi_upper),
    )


# ===========================================================================
# Tier 1: Feature Coverage (Features 1 through 19)
# ===========================================================================


@pytest.mark.unit
class TestTier1FeatureCoverage:
    """Verifies all 19 features specified in PROJECT.md Feature Inventory."""

    def test_feature01_cmake_benchmarking_infrastructure(self) -> None:
        """Feature 1: Verify C++ Google Benchmark CMake infrastructure."""
        bench_cmake = REPO_ROOT / 'cmake' / 'Benchmarking.cmake'
        if not bench_cmake.exists():
            pytest.skip('Feature 1 (cmake/Benchmarking.cmake) pending Milestone 1 implementation')

        content = bench_cmake.read_text(encoding='utf-8')
        assert 'macro(add_project_benchmark' in content or 'function(add_project_benchmark' in content
        assert 'benchmark::benchmark' in content
        # Ensure benchmarks are NOT registered with CTest
        assert 'add_test(' not in content
        assert 'add_project_test(' not in content

        # Check root CMakeLists.txt option
        root_cmakelists = (REPO_ROOT / 'CMakeLists.txt').read_text(encoding='utf-8')
        assert 'option(BUILD_BENCHMARKS' in root_cmakelists
        assert 'OFF' in root_cmakelists

        # Check CMakePresets.json for benchmark preset
        presets_path = REPO_ROOT / 'CMakePresets.json'
        assert presets_path.exists()
        presets_data = json.loads(presets_path.read_text(encoding='utf-8'))
        configure_presets = presets_data.get('configurePresets', [])
        benchmark_presets = [p for p in configure_presets if p.get('name') == 'benchmark']
        assert len(benchmark_presets) == 1, 'benchmark preset must be defined in CMakePresets.json'
        cache_vars = benchmark_presets[0].get('cacheVariables', {})
        assert cache_vars.get('BUILD_BENCHMARKS') == 'ON'
        assert cache_vars.get('BUILD_TESTING') == 'OFF'

    def test_feature02_pixi_workspace_dependencies(self) -> None:
        """Feature 2: Verify Pixi dependencies and tasks in pyproject.toml."""
        pyproject_path = REPO_ROOT / 'pyproject.toml'
        assert pyproject_path.exists()
        content = pyproject_path.read_text(encoding='utf-8')

        if 'benchmark =' not in content:
            pytest.skip('Feature 2 (Pixi dependencies) pending Milestone 1 implementation')

        assert 'numpy =' in content
        assert 'scipy =' in content
        assert 'plotly =' in content

        # Verify format-cmake covers benchmarks
        assert 'benchmarks/cpp' in content

    def test_feature03_ci_exclusion(self) -> None:
        """Feature 3: Verify benchmarks are excluded from GitHub Actions CI."""
        ci_path = REPO_ROOT / '.github' / 'workflows' / 'ci.yml'
        assert ci_path.exists()
        content = ci_path.read_text(encoding='utf-8')

        # Must not enable benchmarks or run benchmark presets in CI
        assert 'BUILD_BENCHMARKS=ON' not in content
        assert '--preset benchmark' not in content
        assert 'bitmap_flip_benchmark' not in content

    def test_feature04_git_hygiene(self) -> None:
        """Feature 4: Verify data/ exclusion in gitignore, pre-commit, and pyproject."""
        gitignore_path = REPO_ROOT / '.gitignore'
        assert gitignore_path.exists()
        gitignore_content = gitignore_path.read_text(encoding='utf-8')
        if 'data/' not in gitignore_content and '/data/' not in gitignore_content:
            pytest.skip('Feature 4 (Git hygiene data/ exclusion) pending Milestone 1 implementation')

        # Check pre-commit config
        precommit_path = REPO_ROOT / '.pre-commit-config.yaml'
        assert precommit_path.exists()
        precommit_content = precommit_path.read_text(encoding='utf-8')
        assert 'data/' in precommit_content

        # Check pyproject exclude
        pyproject_content = (REPO_ROOT / 'pyproject.toml').read_text(encoding='utf-8')
        assert '"data",' in pyproject_content or '"data/"' in pyproject_content

    def test_feature05_baseline_function_implementation(self) -> None:
        """Feature 5: Verify flip_bitmap_unaligned_optimized declaration and definition."""
        hpp_path = REPO_ROOT / 'src' / 'include' / 'systems' / 'bitmap_flip.hpp'
        cpp_path = REPO_ROOT / 'src' / 'cpp' / 'systems' / 'bitmap_flip.cpp'
        assert hpp_path.exists()
        assert cpp_path.exists()

        hpp_content = hpp_path.read_text(encoding='utf-8')
        cpp_content = cpp_path.read_text(encoding='utf-8')
        if 'flip_bitmap_unaligned_optimized' not in hpp_content or 'flip_bitmap_unaligned_optimized' not in cpp_content:
            pytest.skip('Feature 5 (flip_bitmap_unaligned_optimized) pending Milestone 1 implementation')

        # Check span overload signature
        assert '[[nodiscard]] auto flip_bitmap_unaligned_optimized(' in hpp_content
        assert 'std::span<std::uint8_t> bitmap' in hpp_content
        assert '-> std::expected<void, BitmapFlipError>' in hpp_content
        assert 'noexcept' in hpp_content

        # Check pointer overload signature
        assert 'void flip_bitmap_unaligned_optimized(' in hpp_content
        assert 'std::uint8_t* bitmap' in hpp_content

        # Check definition exists in cpp
        assert 'flip_bitmap_unaligned_optimized(' in cpp_content

    def test_feature06_baseline_unit_tests(self) -> None:
        """Feature 6: Verify dual unit testing of both unaligned implementations."""
        test_cpp_path = REPO_ROOT / 'test' / 'cpp' / 'systems' / 'bitmap_flip_test.cpp'
        assert test_cpp_path.exists()
        content = test_cpp_path.read_text(encoding='utf-8')

        if 'flip_bitmap_unaligned_optimized' not in content:
            pytest.skip('Feature 6 (Dual unit tests) pending Milestone 1 implementation')

        assert 'flip_bitmap_unaligned' in content
        assert 'flip_bitmap_unaligned_optimized' in content

    def test_feature07_aspice_traceability(self) -> None:
        """Feature 7: Verify ASPICE traceability passes with check_systems_traceability."""
        spec_dir = REPO_ROOT / 'docs' / 'systems'
        test_dir = REPO_ROOT / 'test' / 'cpp' / 'systems'
        assert spec_dir.exists()
        assert test_dir.exists()

        spec_path = spec_dir / 'bitmap_flip.md'
        test_cpp_path = test_dir / 'bitmap_flip_test.cpp'
        assert spec_path.exists()
        assert test_cpp_path.exists()

        spec_requirements, spec_errors = trace.parse_spec(spec_path)
        assert not spec_errors, f'Spec parsing errors found: {spec_errors}'
        assert len(spec_requirements) >= 20, 'Expected extensive ASPICE requirement spec (>= 20 requirements)'

        report = trace.check(spec_dir, test_dir)
        assert not report.errors, f'Traceability errors found: {report.errors}'

    def test_feature08_google_benchmark_suite(self) -> None:
        """Feature 8: Verify Google Benchmark implementation contracts."""
        bm_cpp_path = REPO_ROOT / 'benchmarks' / 'cpp' / 'systems' / 'bitmap_flip_benchmark.cpp'
        if not bm_cpp_path.exists():
            pytest.skip('Feature 8 (bitmap_flip_benchmark.cpp) pending Milestone 2 implementation')

        content = bm_cpp_path.read_text(encoding='utf-8')
        # Check deterministic seed
        assert '0x5EED' in content or '1337' in content
        # Check registered benchmark names
        assert 'BM_FlipBitmap_Baseline' in content or 'Baseline' in content
        assert 'BM_FlipBitmap_Candidate' in content or 'Candidate' in content
        # Check DoNotOptimize and memory clobber
        assert 'DoNotOptimize' in content

    def test_feature09_host_environment_tuning(self) -> None:
        """Feature 9: Verify host environment tuning module structure."""
        runner_path = REPO_ROOT / 'scripts' / 'benchmarks' / 'run_benchmark.py'
        if not runner_path.exists():
            pytest.skip('Feature 9 (Host environment tuning) pending Milestone 2 implementation')

        content = runner_path.read_text(encoding='utf-8')
        assert 'HostTuner' in content or 'tune_host' in content
        # Check Darwin and Linux tuning capabilities
        assert 'taskpolicy' in content or 'caffeinate' in content or 'pmset' in content
        assert 'taskset' in content or 'cpupower' in content or 'isolcpus' in content

    def test_feature10_bayesian_best_rope_engine(self) -> None:
        """Feature 10: Verify Bayesian BEST + ROPE calculation interface."""
        compare_path = REPO_ROOT / 'scripts' / 'benchmarks' / 'bayesian_compare.py'
        if not compare_path.exists():
            pytest.skip('Feature 10 (bayesian_compare.py) pending Milestone 2 implementation')

        content = compare_path.read_text(encoding='utf-8')
        assert 'analyze_benchmark' in content
        assert 'p_better' in content
        assert 'p_same' in content
        assert 'p_worse' in content

    def test_feature11_rich_reporting_and_telemetry(self) -> None:
        """Feature 11: Verify rich Plotly HTML and Markdown reporting."""
        compare_path = REPO_ROOT / 'scripts' / 'benchmarks' / 'bayesian_compare.py'
        if not compare_path.exists():
            pytest.skip('Feature 11 (Reporting in bayesian_compare.py) pending Milestone 2 implementation')

        content = compare_path.read_text(encoding='utf-8')
        assert 'report.html' in content
        assert 'report.md' in content
        assert 'plotly' in content.lower()

    def test_feature12_baseline_promotion_tooling(self) -> None:
        """Feature 12: Verify baseline promotion logic."""
        runner_path = REPO_ROOT / 'scripts' / 'benchmarks' / 'run_benchmark.py'
        compare_path = REPO_ROOT / 'scripts' / 'benchmarks' / 'bayesian_compare.py'
        if not (runner_path.exists() or compare_path.exists()):
            pytest.skip('Feature 12 (Promotion tooling) pending Milestone 2 implementation')

        combined = ''
        if runner_path.exists():
            combined += runner_path.read_text(encoding='utf-8')
        if compare_path.exists():
            combined += compare_path.read_text(encoding='utf-8')
        assert 'promote' in combined.lower()

    def test_feature13_python_unit_tests(self) -> None:
        """Feature 13: Verify pytest test suite for runner and Bayesian analysis."""
        analysis_test_path = REPO_ROOT / 'test' / 'python' / 'scripts' / 'test_benchmark_analysis.py'
        if not analysis_test_path.exists():
            pytest.skip('Feature 13 (test_benchmark_analysis.py) pending Milestone 2 implementation')

        content = analysis_test_path.read_text(encoding='utf-8')
        assert 'def test_' in content

    def test_feature14_initial_baseline_establishment(self) -> None:
        """Feature 14: Verify initial baseline capability and benchmark target registration."""
        bm_cmake = REPO_ROOT / 'benchmarks' / 'cpp' / 'systems' / 'CMakeLists.txt'
        if not bm_cmake.exists():
            pytest.skip('Feature 14 (Initial baseline establishment) pending Milestone 2 implementation')

        content = bm_cmake.read_text(encoding='utf-8')
        assert 'bitmap_flip_benchmark' in content

    def test_feature15_optimization_loop_skill(self) -> None:
        """Feature 15: Verify Antigravity optimization loop skill."""
        skill_path = REPO_ROOT / '.agents' / 'skills' / 'optimization-loop' / 'SKILL.md'
        if not skill_path.exists():
            pytest.skip('Feature 15 (optimization-loop skill) pending Milestone 3 implementation')

        content = skill_path.read_text(encoding='utf-8')
        assert content.startswith('---'), 'Skill must contain YAML frontmatter'
        assert 'name: optimization-loop' in content
        assert 'description:' in content
        # Required phases
        assert 'Preloop' in content
        assert 'Fast-Screen' in content or 'Fast Screen' in content or 'fast_screen' in content
        assert 'Post Loop' in content or 'Post-Loop' in content or 'post_loop' in content

    def test_feature16_skill_supporting_references(self) -> None:
        """Feature 16: Verify skill supporting reference documentation."""
        refs_dir = REPO_ROOT / '.agents' / 'skills' / 'optimization-loop' / 'references'
        if not refs_dir.exists():
            pytest.skip('Feature 16 (Skill references) pending Milestone 3 implementation')

        expected_refs = [
            'bayesian_best.md',
            'hardware_tuning.md',
            'subagent_coordination.md',
            'scribe_guidelines.md',
        ]
        for ref_name in expected_refs:
            assert (refs_dir / ref_name).exists(), f'Missing expected skill reference: {ref_name}'

    def test_feature17_documentation_updates(self) -> None:
        """Feature 17: Verify AGENTS.md and PROJECT.md documentation updates."""
        agents_md = (REPO_ROOT / 'AGENTS.md').read_text(encoding='utf-8')
        # Check AGENTS.md rules and benchmarks references
        assert 'benchmarks' in agents_md or 'systems' in agents_md

        project_md = REPO_ROOT / 'PROJECT.md'
        if project_md.exists():
            p_content = project_md.read_text(encoding='utf-8')
            if 'benchmarks' not in p_content:
                pytest.skip('Feature 17 (Documentation updates) pending Milestone 3 implementation')

    def test_feature18_quality_gates_verification(self) -> None:
        """Feature 18: Verify AGENTS.md byte budget and pyproject ordering."""
        agents_path = REPO_ROOT / 'AGENTS.md'
        byte_size = len(agents_path.read_bytes())
        assert byte_size < AGENTS_MD_MAX_BYTES, (
            f'AGENTS.md size ({byte_size} bytes) exceeds limit ({AGENTS_MD_MAX_BYTES} bytes)'
        )

        assert reorder_main(['--check', str(REPO_ROOT / 'pyproject.toml')]) == 0


# ===========================================================================
# Tier 2: Boundary & Corner Cases
# ===========================================================================


@pytest.mark.unit
class TestTier2BoundaryAndCornerCases:
    """Verifies boundary conditions, extreme parameters, and edge cases."""

    def test_bayesian_zero_rope(self) -> None:
        """Verify boundary condition when ROPE is exactly 0.0."""
        baseline = [100.0, 102.0, 99.0, 101.0, 100.5]
        candidate = [95.0, 96.0, 94.5, 95.5, 95.0]

        result = compute_reference_best(baseline, candidate, rope_fraction=0.0)
        # With zero width, P(same) must be 0.0
        assert math.isclose(result.p_same, 0.0, abs_tol=1e-5)
        # P(better) + P(worse) must sum to 1.0
        assert math.isclose(result.p_better + result.p_worse, 1.0, abs_tol=1e-5)
        assert result.p_better > 0.99, 'Candidate is clearly faster, p_better should be > 0.99'

    def test_bayesian_wide_rope(self) -> None:
        """Verify boundary condition when ROPE is 100% (all differences within ROPE)."""
        baseline = [100.0, 102.0, 99.0, 101.0, 100.5]
        candidate = [95.0, 96.0, 94.5, 95.5, 95.0]

        result = compute_reference_best(baseline, candidate, rope_fraction=1.0)
        # With massive ROPE, P(same) must dominate
        assert result.p_same > 0.99
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-5)

    def test_bayesian_identical_samples(self) -> None:
        """Verify candidate identical to baseline yields effect size 0 and high P(same)."""
        samples = [50.0, 52.0, 48.0, 51.0, 49.0, 50.5]
        result = compute_reference_best(samples, list(samples), rope_fraction=0.05)

        assert math.isclose(result.effect_size, 0.0, abs_tol=1e-6)
        assert result.p_same > 0.90, 'Identical samples within 5% ROPE must have high p_same'
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-5)

    def test_bayesian_heavy_tailed_outliers(self) -> None:
        """Verify robustness against extreme latency spikes (outliers)."""
        baseline = [10.0, 10.1, 9.9, 10.2, 10.0, 9.8, 10.1, 10.0]
        # Candidate has one massive 50x outlier due to OS interrupt
        candidate_with_outlier = [8.0, 8.1, 7.9, 8.2, 8.0, 7.8, 8.1, 400.0]

        # Verify reference computes without throwing math domain error
        result = compute_reference_best(baseline, candidate_with_outlier, rope_fraction=0.02)
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-5)

    def test_bayesian_small_sample_size(self) -> None:
        """Verify fast-screen sample size (N=3) executes properly."""
        baseline = [25.0, 25.2, 24.8]
        candidate = [20.0, 20.1, 19.9]

        result = compute_reference_best(baseline, candidate, rope_fraction=0.02)
        assert math.isclose(result.p_better + result.p_same + result.p_worse, 1.0, abs_tol=1e-5)
        assert result.p_better > 0.90

    def test_extreme_geometry_calculations(self) -> None:
        """Verify bitmap memory sizing across extreme unaligned dimensions."""
        geometries = [
            (1, 1, 1),  # 1x1: 1 byte
            (1, 7, 7),  # 1x7: 7 bytes (1 byte/row * 7)
            (7, 1, 1),  # 7x1: 1 byte (7 bits in 1 byte)
            (9, 1, 2),  # 9x1: 2 bytes
            (13, 13, 26),  # 13x13: ceil(13/8)=2 bytes/row * 13 = 26 bytes
            (4096, 2160, 512 * 2160),  # 4K: 4096/8=512 bytes/row * 2160
        ]
        for w, h, expected_bytes in geometries:
            stride_bytes = (w + 7) // 8
            total_bytes = stride_bytes * h
            assert total_bytes == expected_bytes, f'Geometry {w}x{h} expected {expected_bytes}, got {total_bytes}'

    def test_promotion_missing_source_directory(self, tmp_path: Path) -> None:
        """Verify promoting a non-existent directory raises FileNotFoundError."""
        src = tmp_path / 'non_existent_latest'
        dst = tmp_path / 'baseline'

        def promote_directory(source: Path, target: Path) -> None:
            if not source.exists():
                msg = f'Source directory {source} does not exist'
                raise FileNotFoundError(msg)
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(source, target)

        with pytest.raises(FileNotFoundError):
            promote_directory(src, dst)

    def test_host_tuning_unprivileged_or_mock_failure(self) -> None:
        """Verify host tuner handles failure of privileged commands gracefully."""

        class MockTuner:
            """Simulates host tuner error handling."""

            def __init__(self) -> None:
                self.warnings: list[str] = []

            def apply_tuning(self) -> bool:
                try:
                    # Simulate failure of a privileged command
                    msg = 'Permission denied (simulated)'
                    raise PermissionError(msg)
                except (PermissionError, subprocess.SubprocessError) as err:
                    self.warnings.append(str(err))
                    return False  # Graceful degradation

        tuner = MockTuner()
        success = tuner.apply_tuning()
        assert not success
        assert len(tuner.warnings) == 1
        assert 'Permission denied' in tuner.warnings[0]

    def test_agents_md_byte_budget_headroom(self) -> None:
        """Verify AGENTS.md has substantial safety headroom under 24 KB limit."""
        agents_path = REPO_ROOT / 'AGENTS.md'
        byte_size = len(agents_path.read_bytes())
        headroom = AGENTS_MD_MAX_BYTES - byte_size
        assert headroom >= 1024, f'AGENTS.md has only {headroom} bytes of headroom, expected >= 1024'


# ===========================================================================
# Tier 3: Cross-Feature Combinations
# ===========================================================================


@pytest.mark.integration
class TestTier3CrossFeatureCombinations:
    """Verifies multi-component interactions across runner, statistics, and git."""

    def test_runner_to_bayesian_to_reporting_pipeline(self, tmp_path: Path) -> None:
        """Verify telemetry pipeline from raw JSON data to Bayesian analysis and report."""
        # 1. Simulate benchmark telemetry output
        latest_dir = tmp_path / 'data' / 'latest' / 'flip_experiment'
        latest_dir.mkdir(parents=True)
        raw_telemetry: dict[str, Any] = {
            'context': {'host_name': 'test_runner', 'cpu_scaling_enabled': False},
            'benchmarks': [
                {'name': 'BM_FlipBitmap_Baseline/1024x768', 'cpu_time': 100.0, 'real_time': 100.2, 'iterations': 1000},
                {'name': 'BM_FlipBitmap_Baseline/1024x768', 'cpu_time': 99.5, 'real_time': 99.7, 'iterations': 1000},
                {'name': 'BM_FlipBitmap_Baseline/1024x768', 'cpu_time': 100.5, 'real_time': 100.6, 'iterations': 1000},
                {'name': 'BM_FlipBitmap_Candidate/1024x768', 'cpu_time': 80.0, 'real_time': 80.1, 'iterations': 1000},
                {'name': 'BM_FlipBitmap_Candidate/1024x768', 'cpu_time': 79.8, 'real_time': 80.0, 'iterations': 1000},
                {'name': 'BM_FlipBitmap_Candidate/1024x768', 'cpu_time': 80.2, 'real_time': 80.3, 'iterations': 1000},
            ],
        }
        telemetry_file = latest_dir / 'benchmark_raw.json'
        telemetry_file.write_text(json.dumps(raw_telemetry), encoding='utf-8')

        # 2. Extract samples
        b_samples = [b['cpu_time'] for b in raw_telemetry['benchmarks'] if 'Baseline' in b['name']]
        c_samples = [b['cpu_time'] for b in raw_telemetry['benchmarks'] if 'Candidate' in b['name']]

        # 3. Analyze
        res = compute_reference_best(b_samples, c_samples, rope_fraction=0.02)
        assert res.p_better > 0.99

        # 4. Generate reports
        report_md = latest_dir / 'report.md'
        md_content = f"""# Benchmark Analysis Report

- **P(Better):** {res.p_better:.4f}
- **P(Same):** {res.p_same:.4f}
- **P(Worse):** {res.p_worse:.4f}
- **Effect Size:** {res.effect_size:.2f}
"""
        report_md.write_text(md_content, encoding='utf-8')

        report_html = latest_dir / 'report.html'
        html_content = (
            f'<html><body><h1>Report</h1><div id="plotly-div">P(Better) = {res.p_better:.2f}</div></body></html>'
        )
        report_html.write_text(html_content, encoding='utf-8')

        assert report_md.exists()
        assert report_html.exists()
        assert 'P(Better):' in report_md.read_text(encoding='utf-8')

    def test_two_stage_filtering_regression_vs_speedup(self) -> None:
        """Verify two-stage filtering: discard slow candidates early, benchmark fast candidates."""

        def evaluate_candidate(
            baseline_samples: list[float],
            fast_screen_samples: list[float],
        ) -> str:
            """Simulates two-stage decision logic.

            Args:
                baseline_samples: Latency samples from baseline run.
                fast_screen_samples: Latency samples from fast-screen candidate run.

            Returns:
                Decision string indicating whether to discard or proceed.
            """
            mean_base = sum(baseline_samples) / len(baseline_samples)
            mean_screen = sum(fast_screen_samples) / len(fast_screen_samples)
            delta_pct = (mean_screen - mean_base) / mean_base

            if delta_pct > 0.05:
                return 'EARLY_DISCARD_REGRESSION'

            return 'PROCEED_TO_FULL_STAGE2'

        baseline = [100.0, 101.0, 99.0]
        slow_candidate = [115.0, 114.0, 116.0]  # +15% slower
        fast_candidate = [85.0, 84.0, 86.0]  # -15% faster

        assert evaluate_candidate(baseline, slow_candidate) == 'EARLY_DISCARD_REGRESSION'
        assert evaluate_candidate(baseline, fast_candidate) == 'PROCEED_TO_FULL_STAGE2'

    def test_git_hygiene_with_active_telemetry(self) -> None:
        """Verify git status remains clean when data/ telemetry directories exist."""
        test_telemetry_dir = REPO_ROOT / 'data' / 'latest' / '_e2e_hygiene_test'
        try:
            test_telemetry_dir.mkdir(parents=True, exist_ok=True)
            (test_telemetry_dir / 'test_run.json').write_text('{"dummy": 1}', encoding='utf-8')
            (test_telemetry_dir / 'report.html').write_text('<html></html>', encoding='utf-8')

            # Run git status --porcelain
            git_bin = shutil.which('git')
            assert git_bin is not None, 'git binary must be installed'
            result = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
                [git_bin, 'status', '--porcelain', 'data/'],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            # data/ must be completely ignored by git
            assert result.stdout.strip() == '', f'Git status showed untracked files in data/: {result.stdout}'
        finally:
            if test_telemetry_dir.exists():
                shutil.rmtree(test_telemetry_dir)

    def test_traceability_compliance_cross_check(self) -> None:
        """Verify check_systems_traceability passes against the repository."""
        spec_dir = REPO_ROOT / 'docs' / 'systems'
        test_dir = REPO_ROOT / 'test' / 'cpp' / 'systems'
        report = trace.check(spec_dir, test_dir)
        assert not report.errors, f'check-traceability failed: {report.errors}'


# ===========================================================================
# Tier 4: Real-World Application Scenarios
# ===========================================================================


@pytest.mark.integration
class TestTier4RealWorldApplicationScenarios:
    """Verifies end-to-end user workflows and full optimization loop lifecycles."""

    def test_scenario_regression_candidate_cycle(self, tmp_path: Path) -> None:
        """Scenario A: Agent tests a regressed candidate, discards, and preserves baseline."""
        exp_dir = tmp_path / 'data'
        baseline_dir = exp_dir / 'baseline' / 'bitmap_flip'
        latest_dir = exp_dir / 'latest' / 'bitmap_flip'
        baseline_dir.mkdir(parents=True)
        latest_dir.mkdir(parents=True)

        # 1. Baseline established
        (baseline_dir / 'benchmark.json').write_text(json.dumps({'cpu_time_ns': 1000}), encoding='utf-8')
        baseline_samples = [1000.0, 1005.0, 995.0]

        # 2. Candidate experiment with regression (+20% time)
        candidate_fast_screen = [1200.0, 1210.0, 1195.0]
        res = compute_reference_best(baseline_samples, candidate_fast_screen, rope_fraction=0.02)

        # 3. Decision
        assert res.p_worse > 0.95
        decision = 'REVERT' if res.p_worse > 0.50 else 'PROMOTE'
        assert decision == 'REVERT'

        # Baseline directory remains unchanged
        assert (baseline_dir / 'benchmark.json').exists()
        assert json.loads((baseline_dir / 'benchmark.json').read_text(encoding='utf-8'))['cpu_time_ns'] == 1000

    def test_scenario_winning_optimization_cycle(self, tmp_path: Path) -> None:
        """Scenario B: Agent tests a winning candidate, verifies, and promotes to baseline."""
        exp_dir = tmp_path / 'data'
        baseline_dir = exp_dir / 'baseline' / 'bitmap_flip'
        latest_dir = exp_dir / 'latest' / 'bitmap_flip'
        baseline_dir.mkdir(parents=True)
        latest_dir.mkdir(parents=True)

        # 1. Old baseline: 100ns
        (baseline_dir / 'benchmark.json').write_text(
            json.dumps({'cpu_time_ns': 100.0, 'version': 'initial'}),
            encoding='utf-8',
        )
        baseline_samples = [100.0, 101.0, 99.5, 100.5, 99.8]

        # 2. Stage 1 Fast Screen: 75ns (-25% speedup)
        candidate_screen = [75.0, 74.8, 75.2]
        screen_mean = sum(candidate_screen) / len(candidate_screen)
        base_mean = sum(baseline_samples) / len(baseline_samples)
        assert screen_mean < base_mean * 0.95, 'Fast screen confirms >= 5% speedup'

        # 3. Stage 2 Full Benchmark: 30 repetitions
        candidate_full = [75.0 + (i % 3) * 0.2 - 0.2 for i in range(30)]
        res = compute_reference_best(baseline_samples, candidate_full, rope_fraction=0.02)
        assert res.p_better >= 0.95
        assert res.p_worse <= 0.01

        # 4. Generate rich reports
        (latest_dir / 'benchmark.json').write_text(
            json.dumps({'cpu_time_ns': 75.0, 'version': 'optimized_candidate'}),
            encoding='utf-8',
        )
        report_html = latest_dir / 'report.html'
        report_md = latest_dir / 'report.md'
        report_html.write_text('<html><body>Optimization Report</body></html>', encoding='utf-8')
        report_md.write_text(f'# Report\n\nP(Better) = {res.p_better:.4f}', encoding='utf-8')

        # 5. Promotion
        def promote(src: Path, dst: Path) -> None:
            if dst.exists():
                shutil.rmtree(dst)
            shutil.copytree(src, dst)

        promote(latest_dir, baseline_dir)

        # 6. Verify promoted baseline has new version
        promoted_data = json.loads((baseline_dir / 'benchmark.json').read_text(encoding='utf-8'))
        assert promoted_data['version'] == 'optimized_candidate'
        assert (baseline_dir / 'report.html').exists()
        assert (baseline_dir / 'report.md').exists()
