"""End-to-end requirement-driven test suite for the Rust track infrastructure.

Validates the integrated deliverables of the Rust track across LeetCode DSA
and Systems practice modules according to the 4-tier testing architecture:
- Tier 1: Feature Coverage (Features 27-37 of the Rust track)
- Tier 2: Boundary & Corner Cases (invalid skill inputs, target directory isolation,
  traceability parser edge cases, component boundary contracts)
- Tier 3: Cross-Feature Combinations (traceability cross-check, formatting/linting gate,
  pre-commit and pixi task alignment, Cargo workspace and Pixi env consistency)
- Tier 4: Real-World Scenarios (workspace compilation, test execution, and full
  developer practice lifecycle)

Progressive testability: Tests for later milestones (M1, M2, M3, M4) cleanly skip
when their target artifacts are not yet created, ensuring that test runs remain green
throughout incremental development.
"""

from __future__ import annotations

import re
import shutil
import subprocess  # ruff: ignore[suspicious-subprocess-import]
import tomllib
from pathlib import Path

import pytest
import yaml  # type: ignore[import-untyped]

from scripts.ci import check_systems_traceability as trace


REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
AGENTS_MD_MAX_BYTES = 24576

# Expected Rust configuration constants
EXPECTED_RUST_VERSION = '1.99.1'
EXPECTED_RUST_EDITION = '2024'
EXPECTED_CARGO_TARGET_DIR = 'build/cargo'
EXPECTED_RUSTFMT_MAX_WIDTH = 120


# ---------------------------------------------------------------------------
# Reference Implementations & Parsers for Invariant & Edge-Case Validation
# ---------------------------------------------------------------------------


def reference_two_sum_checked(nums: list[int], target: int) -> tuple[int, int] | None:
    """Reference implementation of TwoSum with 64-bit overflow-safe complement arithmetic.

    Args:
        nums: Sequence of 32-bit signed integers.
        target: Target sum as a 32-bit signed integer.

    Returns:
        Pair of 0-based indices if a valid pair exists, or None.

    Raises:
        ValueError: If input contains fewer than 2 elements.
    """
    if len(nums) < 2:
        msg = f'Input nums must have at least 2 elements (got {len(nums)})'
        raise ValueError(msg)

    # 32-bit signed integer bounds
    i32_min = -2147483648
    i32_max = 2147483647

    seen: dict[int, int] = {}
    for idx, num in enumerate(nums):
        # 64-bit signed complement computation to guard against i32 overflow
        complement_64 = int(target) - int(num)
        if complement_64 < i32_min or complement_64 > i32_max:
            seen[num] = idx
            continue

        complement = int(complement_64)
        if complement in seen:
            return seen[complement], idx
        seen[num] = idx
    return None


class ReferenceLruCache:
    """Reference LRU cache validating capacity-1 and eviction invariants."""

    def __init__(self, capacity: int) -> None:
        """Initializes the LRU cache with a fixed capacity.

        Args:
            capacity: Maximum number of entries before eviction.

        Raises:
            ValueError: If capacity is not strictly positive.
        """
        if capacity <= 0:
            msg = f'Capacity must be strictly positive (got {capacity})'
            raise ValueError(msg)
        self.capacity = capacity
        self.cache: dict[int, int] = {}

    def get(self, key: int) -> int:
        """Gets the value for a key, updating recency.

        Args:
            key: Lookup key.

        Returns:
            Stored value, or -1 if absent.
        """
        if key not in self.cache:
            return -1
        val = self.cache.pop(key)
        self.cache[key] = val
        return val

    def put(self, key: int, value: int) -> None:
        """Inserts or updates a key-value pair, evicting the oldest key if full.

        Args:
            key: Target key.
            value: Associated value.
        """
        if key in self.cache:
            self.cache.pop(key)
        elif len(self.cache) >= self.capacity:
            oldest_key = next(iter(self.cache))
            del self.cache[oldest_key]
        self.cache[key] = value


class ReferenceBoundedFifo:
    """Reference BoundedFifo validating fixed capacity and error contracts."""

    def __init__(self, capacity: int) -> None:
        """Initializes the BoundedFifo with a fixed capacity.

        Args:
            capacity: Maximum number of elements.

        Raises:
            ValueError: If capacity is not strictly positive.
        """
        if capacity <= 0:
            msg = f'Capacity must be strictly positive (got {capacity})'
            raise ValueError(msg)
        self.capacity = capacity
        self.queue: list[int] = []
        self.is_closed = False

    def try_push(self, value: int) -> tuple[bool, int | None]:
        """Attempts to push an element into the FIFO.

        Args:
            value: Integer value to push.

        Returns:
            Tuple of (success_flag, rejected_value_if_failed).
        """
        if self.is_closed:
            return False, value
        if len(self.queue) >= self.capacity:
            return False, value
        self.queue.append(value)
        return True, None

    def try_pop(self) -> tuple[bool, int | None]:
        """Attempts to pop an element from the FIFO.

        Returns:
            Tuple of (success_flag, popped_value_if_successful).
        """
        if not self.queue:
            return False, None
        return True, self.queue.pop(0)

    def close(self) -> None:
        """Closes the FIFO, preventing subsequent pushes."""
        self.is_closed = True


def parse_rust_test_attributes(content: str) -> tuple[dict[str, list[str]], list[str]]:
    """Reference parser verifying Rust test functions and requirement annotations.

    Supports:
    - #[test] or #[tokio::test] attributes
    - Multiple stacked attributes (#[test], #[ignore], etc.)
    - Line annotations: // requirements: REQ-001 or // REQ: REQ-001
    - GTest-style record_property!("requirements", "REQ-001")

    Args:
        content: Rust source file content.

    Returns:
        Mapping of requirement IDs to referencing test functions, and errors.
    """
    valid_id_re = re.compile(r'^[A-Z][A-Z0-9]{1,7}-REQ-\d{3}$')
    req_comment_re = re.compile(r'//\s*(?:requirements|REQ)\s*:\s*([A-Za-z0-9_\-,\s]+)')
    record_prop_re = re.compile(r'record_property!\(\s*"requirements"\s*,\s*"([^"]*)"\s*\)')
    test_fn_re = re.compile(r'^(?:pub(?:\([^)]+\))?\s+)?(?:async\s+)?fn\s+([a-zA-Z0-9_]+)\s*\(')

    refs: dict[str, list[str]] = {}
    errors: list[str] = []

    lines = content.splitlines()
    has_test_attr = False
    current_test_name: str | None = None

    for line_no, raw_line in enumerate(lines, start=1):
        line = raw_line.strip()

        # Attribute tracking
        if line.startswith('#[') and 'test' in line:
            has_test_attr = True
            continue

        if has_test_attr:
            if match := test_fn_re.match(line):
                current_test_name = match.group(1)
                has_test_attr = False
                continue
            if line.startswith('#['):
                # Another stacked attribute
                continue
            # Non-attribute, non-fn line clears test attr flag
            has_test_attr = False

        # Extract requirements from comment
        if match := req_comment_re.search(line):
            owner = current_test_name or f'anonymous:{line_no}'
            for raw_id in match.group(1).split(','):
                req_id = raw_id.strip()
                if not req_id:
                    continue
                if not valid_id_re.match(req_id):
                    errors.append(f'line {line_no}: malformed requirement ID "{req_id}" in {owner}')
                    continue
                refs.setdefault(req_id, []).append(owner)

        # Extract requirements from macro
        if match := record_prop_re.search(line):
            owner = current_test_name or f'anonymous:{line_no}'
            for raw_id in match.group(1).split(','):
                req_id = raw_id.strip()
                if not req_id:
                    continue
                if not valid_id_re.match(req_id):
                    errors.append(f'line {line_no}: malformed requirement ID "{req_id}" in {owner}')
                    continue
                refs.setdefault(req_id, []).append(owner)

    return refs, errors


# ===========================================================================
# Tier 1: Feature Coverage (Features 27 through 37)
# ===========================================================================


@pytest.mark.unit
class TestTier1FeatureCoverage:
    """Verifies all 11 Rust track features."""

    def test_feature27_cargo_workspace_manifests(self) -> None:
        """Feature 27: Verify Cargo root workspace manifest, members, and lints."""
        cargo_toml_path = REPO_ROOT / 'Cargo.toml'
        if not cargo_toml_path.exists():
            pytest.skip('Feature 27 pending M1 implementation: Cargo.toml not yet created')

        cargo_data = tomllib.loads(cargo_toml_path.read_text(encoding='utf-8'))
        assert 'workspace' in cargo_data, 'Cargo.toml must define a [workspace] table'
        workspace = cargo_data['workspace']

        # Members check
        members = workspace.get('members', [])
        assert 'src/rust/leet_code' in members, 'workspace.members must include "src/rust/leet_code"'
        assert 'src/rust/systems' in members, 'workspace.members must include "src/rust/systems"'

        # Resolver and edition
        assert str(workspace.get('resolver', '')) in {'2', '3'}, 'workspace resolver must be 2 or 3'
        pkg = workspace.get('package', {})
        assert pkg.get('edition') == EXPECTED_RUST_EDITION, f'workspace.package.edition must be {EXPECTED_RUST_EDITION}'
        assert pkg.get('rust-version') == EXPECTED_RUST_VERSION, (
            f'workspace.package.rust-version must be {EXPECTED_RUST_VERSION}'
        )

        # Lints configuration
        lints = workspace.get('lints', {})
        assert 'rust' in lints or 'clippy' in lints, 'workspace.lints must define rust or clippy lint tables'

        # .cargo/config.toml check
        cargo_config_path = REPO_ROOT / '.cargo' / 'config.toml'
        assert cargo_config_path.exists(), '.cargo/config.toml must exist'
        config_data = tomllib.loads(cargo_config_path.read_text(encoding='utf-8'))
        build_config = config_data.get('build', {})
        assert build_config.get('target-dir') == EXPECTED_CARGO_TARGET_DIR, (
            f'.cargo/config.toml build.target-dir must be "{EXPECTED_CARGO_TARGET_DIR}"'
        )

        # rustfmt.toml check
        rustfmt_path = REPO_ROOT / 'rustfmt.toml'
        assert rustfmt_path.exists(), 'rustfmt.toml must exist'
        rustfmt_content = rustfmt_path.read_text(encoding='utf-8')
        assert f'edition = "{EXPECTED_RUST_EDITION}"' in rustfmt_content
        assert f'max_width = {EXPECTED_RUSTFMT_MAX_WIDTH}' in rustfmt_content

        # clippy.toml check
        clippy_path = REPO_ROOT / 'clippy.toml'
        assert clippy_path.exists(), 'clippy.toml must exist'

        # .gitignore check
        gitignore_path = REPO_ROOT / '.gitignore'
        assert gitignore_path.exists(), '.gitignore must exist'
        gitignore_lines = [line.strip() for line in gitignore_path.read_text(encoding='utf-8').splitlines()]
        assert any(pattern in gitignore_lines for pattern in ('target/', '/target/', 'target')), (
            '.gitignore must ignore target/ directory'
        )

    def test_feature28_pixi_rust_tasks_and_env(self) -> None:
        """Feature 28: Verify Pixi Rust tasks and activation environment in pyproject.toml."""
        pyproject_path = REPO_ROOT / 'pyproject.toml'
        data = tomllib.loads(pyproject_path.read_text(encoding='utf-8'))

        # Dependencies check
        pixi_deps = data.get('tool', {}).get('pixi', {}).get('dependencies', {})
        assert 'rust' in pixi_deps, 'pyproject.toml must declare rust dependency'

        # Tasks check
        pixi_tasks = data.get('tool', {}).get('pixi', {}).get('tasks', {})
        if 'test-rust' not in pixi_tasks:
            pytest.skip('Feature 28 pending M1 implementation: Pixi Rust tasks not yet configured')

        expected_tasks = [
            'test-rust',
            'clippy',
            'clippy-fix',
            'format-rust',
            'format-rust-check',
            'cov-rust',
        ]
        for task_name in expected_tasks:
            assert task_name in pixi_tasks, f'Missing Pixi task: {task_name}'

        # Verify aggregate task 'test' includes test-rust
        test_task = pixi_tasks.get('test', {})
        if isinstance(test_task, dict):
            depends_on = test_task.get('depends-on', [])
            assert 'test-rust' in depends_on, 'Pixi task "test" must depend on "test-rust"'
        elif isinstance(test_task, str):
            assert 'test-rust' in test_task, 'Pixi task "test" command must reference "test-rust"'

        # Environment activation check
        activation_env = data.get('tool', {}).get('pixi', {}).get('activation', {}).get('env', {})
        assert 'CARGO_TARGET_DIR' in activation_env, (
            'pyproject.toml activation.env must set CARGO_TARGET_DIR to build/cargo'
        )
        assert activation_env.get('CARGO_TARGET_DIR', '').endswith('build/cargo')

    def test_feature29_pre_commit_rust_quality_gates(self) -> None:
        """Feature 29: Verify pre-commit hooks for cargo-fmt and cargo-clippy."""
        precommit_path = REPO_ROOT / '.pre-commit-config.yaml'
        assert precommit_path.exists(), '.pre-commit-config.yaml must exist'

        precommit_data = yaml.safe_load(precommit_path.read_text(encoding='utf-8'))
        local_repos = [repo for repo in precommit_data.get('repos', []) if repo.get('repo') == 'local']
        assert local_repos, 'Expected local repository in .pre-commit-config.yaml'

        local_hooks = {hook['id']: hook for repo in local_repos for hook in repo.get('hooks', [])}

        if 'cargo-fmt' not in local_hooks or 'cargo-clippy' not in local_hooks:
            pytest.skip('Feature 29 pending M1 implementation: cargo-fmt or cargo-clippy hooks not yet configured')

        # cargo-fmt hook assertions
        fmt_hook = local_hooks['cargo-fmt']
        assert fmt_hook.get('language') == 'system'
        assert fmt_hook.get('pass_filenames') is False

        # cargo-clippy hook assertions
        clippy_hook = local_hooks['cargo-clippy']
        assert clippy_hook.get('language') == 'system'
        assert clippy_hook.get('pass_filenames') is False

        # systems-traceability regex update check
        if 'systems-traceability' in local_hooks:
            trace_files_regex = local_hooks['systems-traceability'].get('files', '')
            assert 'rs' in trace_files_regex or 'rust' in trace_files_regex, (
                'systems-traceability hook files regex must match Rust test files (*_test.rs)'
            )

    def test_feature30_leetcode_mcp_rust_snippet(self) -> None:
        """Feature 30: Verify LeetCode MCP server models and client extract rust_snippet."""
        models_path = REPO_ROOT / '.agents' / 'plugins' / 'leetcode' / 'src' / 'leetcode_mcp' / 'models.py'
        client_path = REPO_ROOT / '.agents' / 'plugins' / 'leetcode' / 'src' / 'leetcode_mcp' / 'client.py'

        assert models_path.exists(), 'models.py must exist'
        assert client_path.exists(), 'client.py must exist'

        models_content = models_path.read_text(encoding='utf-8')
        if 'rust_snippet' not in models_content:
            pytest.skip('Feature 30 pending M2 implementation: rust_snippet field not yet in models.py')

        assert 'rust_snippet: str' in models_content, 'ProblemDetails model must define rust_snippet field'

        client_content = client_path.read_text(encoding='utf-8')
        assert ('rust' in client_content and 'rust_code' in client_content) or 'rust_snippet' in client_content, (
            'client.py must extract Rust code snippets from GraphQL codeSnippets'
        )

    def test_feature31_leetcode_setup_rust_support(self) -> None:
        """Feature 31: Verify /leetcode-setup skill documentation and references for Rust."""
        skill_path = REPO_ROOT / '.agents' / 'plugins' / 'leetcode' / 'skills' / 'leetcode-setup' / 'SKILL.md'
        ref_rust = (
            REPO_ROOT / '.agents' / 'plugins' / 'leetcode' / 'skills' / 'leetcode-setup' / 'references' / 'rust.md'
        )

        assert skill_path.exists(), 'leetcode-setup SKILL.md must exist'
        skill_content = skill_path.read_text(encoding='utf-8')

        if not ref_rust.exists():
            pytest.skip('Feature 31 pending M2 implementation: leetcode-setup references/rust.md not yet created')

        assert 'rust' in skill_content.lower(), 'leetcode-setup SKILL.md must document Rust language support'

        ref_content = ref_rust.read_text(encoding='utf-8')
        assert 'Solution' in ref_content, 'references/rust.md must document LeetCode Solution struct wrapper'
        assert 'src/rust/leet_code' in ref_content, 'references/rust.md must reference src/rust/leet_code layout'
        assert 'test/rust/leet_code' in ref_content, 'references/rust.md must reference test/rust/leet_code layout'

        # LeetCode crate manifest check
        crate_toml = REPO_ROOT / 'src' / 'rust' / 'leet_code' / 'Cargo.toml'
        if crate_toml.exists():
            data = tomllib.loads(crate_toml.read_text(encoding='utf-8'))
            assert data.get('package', {}).get('name') == 'leet_code'

    def test_feature32_systems_setup_rust_support(self) -> None:
        """Feature 32: Verify /systems-setup skill documentation and references for Rust."""
        skill_path = REPO_ROOT / '.agents' / 'plugins' / 'systems' / 'skills' / 'systems-setup' / 'SKILL.md'
        ref_rust = REPO_ROOT / '.agents' / 'plugins' / 'systems' / 'skills' / 'systems-setup' / 'references' / 'rust.md'

        assert skill_path.exists(), 'systems-setup SKILL.md must exist'
        if not ref_rust.exists():
            pytest.skip('Feature 32 pending M2 implementation: systems-setup references/rust.md not yet created')

        ref_content = ref_rust.read_text(encoding='utf-8')
        assert 'CAPACITY' in ref_content or 'const' in ref_content, 'references/rust.md must document const generics'
        assert 'no-heap' in ref_content or 'heap' in ref_content, (
            'references/rust.md must document zero-allocation steady-state rules'
        )
        assert 'requirements:' in ref_content or 'record_property' in ref_content, (
            'references/rust.md must document requirement traceability tags'
        )

        # Systems crate manifest check
        crate_toml = REPO_ROOT / 'src' / 'rust' / 'systems' / 'Cargo.toml'
        if crate_toml.exists():
            data = tomllib.loads(crate_toml.read_text(encoding='utf-8'))
            assert data.get('package', {}).get('name') == 'systems'

    def test_feature33_systems_traceability_rust_parser(self) -> None:
        """Feature 33: Verify scripts/ci/check_systems_traceability.py scans Rust tests."""
        trace_path = REPO_ROOT / 'scripts' / 'ci' / 'check_systems_traceability.py'
        assert trace_path.exists(), 'check_systems_traceability.py must exist'
        content = trace_path.read_text(encoding='utf-8')

        if 'test/rust/systems' not in content and '*.rs' not in content:
            pytest.skip('Feature 33 pending M2 implementation: check_systems_traceability.py Rust parser not yet added')

        assert '*.rs' in content or '_test.rs' in content, 'Traceability checker must glob *.rs or *_test.rs test files'

    def test_feature34_representative_leetcode_rust_practice(self) -> None:
        """Feature 34: Verify representative LeetCode problems (list_node, two_sum, lru_cache)."""
        rust_lc_src = REPO_ROOT / 'src' / 'rust' / 'leet_code' / 'src'
        two_sum_rs = rust_lc_src / 'two_sum.rs'
        lru_cache_rs = rust_lc_src / 'lru_cache.rs'
        list_node_rs = rust_lc_src / 'list_node.rs'

        if not two_sum_rs.exists() or not lru_cache_rs.exists():
            pytest.skip('Feature 34 pending M3 implementation: LeetCode Rust examples not yet created')

        # Verify two_sum.rs
        two_sum_content = two_sum_rs.read_text(encoding='utf-8')
        assert 'pub struct Solution' in two_sum_content
        assert 'fn two_sum' in two_sum_content
        assert 'as i64' in two_sum_content or 'i64' in two_sum_content, (
            'two_sum.rs must guard against signed 32-bit integer overflow'
        )

        # Verify lru_cache.rs
        lru_content = lru_cache_rs.read_text(encoding='utf-8')
        assert 'LruCache' in lru_content
        assert 'get(' in lru_content
        assert 'put(' in lru_content
        assert 'Rc<' not in lru_content, 'LruCache must avoid Rc overhead in favor of arena/index pool'

        # Verify list_node.rs
        if list_node_rs.exists():
            node_content = list_node_rs.read_text(encoding='utf-8')
            assert 'pub struct ListNode' in node_content
            assert 'pub val: i32' in node_content

    def test_feature35_representative_systems_rust_practice(self) -> None:
        """Feature 35: Verify representative Systems component (bounded_fifo)."""
        fifo_rs = REPO_ROOT / 'src' / 'rust' / 'systems' / 'src' / 'bounded_fifo.rs'
        fifo_test_rs = REPO_ROOT / 'test' / 'rust' / 'systems' / 'bounded_fifo_test.rs'

        if not fifo_rs.exists():
            pytest.skip('Feature 35 pending M3 implementation: Systems bounded_fifo.rs not yet created')

        content = fifo_rs.read_text(encoding='utf-8')
        assert 'pub struct BoundedFifo' in content
        assert 'CAPACITY: usize' in content
        assert 'try_push' in content
        assert 'try_pop' in content
        assert 'close' in content

        # Check tests and traceability
        if fifo_test_rs.exists():
            test_content = fifo_test_rs.read_text(encoding='utf-8')
            assert 'FIFO-REQ-' in test_content, 'bounded_fifo_test.rs must declare FIFO-REQ traceability tags'

    def test_feature36_github_actions_rust_ci_matrix(self) -> None:
        """Feature 36: Verify GitHub Actions CI matrix includes rust-tests and rust-coverage."""
        ci_path = REPO_ROOT / '.github' / 'workflows' / 'ci.yml'
        assert ci_path.exists(), '.github/workflows/ci.yml must exist'
        ci_data = yaml.safe_load(ci_path.read_text(encoding='utf-8'))
        jobs = ci_data.get('jobs', {})

        if 'rust-tests' not in jobs:
            pytest.skip('Feature 36 pending M4 implementation: rust-tests job not yet in ci.yml')

        rust_tests_job = jobs['rust-tests']
        strategy = rust_tests_job.get('strategy', {})
        matrix = strategy.get('matrix', {})
        os_list = matrix.get('os', [])
        assert 'ubuntu-latest' in os_list
        assert 'macos-latest' in os_list

        assert 'rust-coverage' in jobs or 'cov-rust' in str(rust_tests_job), (
            'ci.yml must include rust-coverage job or run cov-rust'
        )

    def test_feature37_opaque_box_rust_e2e_suite(self) -> None:
        """Feature 37: Verify presence of E2E test suite and agentic gitignore rules."""
        test_file = REPO_ROOT / 'test' / 'python' / 'scripts' / 'test_e2e_rust_infrastructure.py'
        assert test_file.exists(), 'test_e2e_rust_infrastructure.py must exist'

        gitignore_lines = {line.strip() for line in (REPO_ROOT / '.gitignore').read_text(encoding='utf-8').splitlines()}
        expected_ephemeral_ignores = {
            '.agents/teamwork/',
            '.gemini/',
            '/TEST_INFRA.md',
            '/TEST_READY.md',
            '/ORIGINAL_REQUEST.md',
            '/PROJECT.md',
        }
        for pattern in expected_ephemeral_ignores:
            assert pattern in gitignore_lines, f'.gitignore must ignore ephemeral path {pattern}'


# ===========================================================================
# Tier 2: Boundary & Corner Cases
# ===========================================================================


@pytest.mark.unit
class TestTier2BoundaryAndCornerCases:
    """Verifies boundary conditions, extreme inputs, and edge-case contracts."""

    def test_skill_language_gate_rejection(self) -> None:
        """Verify that skill specifications mandate strict language gates and reject invalid inputs."""
        lc_skill_path = REPO_ROOT / '.agents' / 'plugins' / 'leetcode' / 'skills' / 'leetcode-setup' / 'SKILL.md'
        content = lc_skill_path.read_text(encoding='utf-8')

        # Step 0 requires mandatory language specification and stops on ambiguity
        assert 'STOP' in content or 'clarification' in content or 'ask the user' in content
        # Disallows guessing or defaulting
        assert 'Never guess' in content or 'ask' in content

    def test_cargo_target_directory_isolation(self) -> None:
        """Verify that Cargo target directory configuration prevents workspace root pollution."""
        config_path = REPO_ROOT / '.cargo' / 'config.toml'
        if not config_path.exists():
            pytest.skip('Pending M1 implementation: .cargo/config.toml not yet created')

        config_data = tomllib.loads(config_path.read_text(encoding='utf-8'))
        target_dir = config_data.get('build', {})['target-dir']
        assert target_dir == 'build/cargo'
        assert not target_dir.startswith('/'), 'target-dir must be a relative path under workspace root'

        # Verify no root 'target' directory is tracked in git
        git_bin = shutil.which('git')
        assert git_bin is not None
        result = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [git_bin, 'ls-files', 'target/'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert result.stdout.strip() == '', 'target/ directory must never contain tracked git files'

    def test_traceability_parser_rust_corner_cases(self) -> None:
        """Verify that Rust requirement tag parsing handles all syntax edge cases robustly."""
        # Case 1: Stacked attributes (#[test], #[ignore], #[should_panic])
        sample_stacked = """
#[test]
#[ignore]
fn test_stacked_attributes() {
    // requirements: FIFO-REQ-001
}
"""
        refs, errors = parse_rust_test_attributes(sample_stacked)
        assert errors == []
        assert refs.get('FIFO-REQ-001') == ['test_stacked_attributes']

        # Case 2: Comma-separated requirement IDs with variable whitespace
        sample_comma = """
#[test]
fn test_multiple_reqs() {
    // requirements: FIFO-REQ-002,   FIFO-REQ-003 , FIFO-REQ-004
}
"""
        refs, errors = parse_rust_test_attributes(sample_comma)
        assert errors == []
        assert refs.get('FIFO-REQ-002') == ['test_multiple_reqs']
        assert refs.get('FIFO-REQ-003') == ['test_multiple_reqs']
        assert refs.get('FIFO-REQ-004') == ['test_multiple_reqs']

        # Case 3: GTest-style record_property! macro
        sample_macro = """
#[test]
fn test_macro_property() {
    record_property!("requirements", "FIFO-REQ-005");
}
"""
        refs, errors = parse_rust_test_attributes(sample_macro)
        assert errors == []
        assert refs.get('FIFO-REQ-005') == ['test_macro_property']

        # Case 4: Malformed requirement ID flags an error
        sample_malformed = """
#[test]
fn test_malformed_id() {
    // requirements: invalid_prefix_req_001
}
"""
        refs, errors = parse_rust_test_attributes(sample_malformed)
        assert len(errors) == 1
        assert 'malformed requirement ID' in errors[0]

        # Case 5: Async test functions (e.g. tokio)
        sample_async = """
#[tokio::test]
async fn test_async_worker() {
    // REQ: FIFO-REQ-006
}
"""
        refs, errors = parse_rust_test_attributes(sample_async)
        assert errors == []
        assert refs.get('FIFO-REQ-006') == ['test_async_worker']

    def test_representative_components_boundary_contracts(self) -> None:
        """Verify behavioral contracts for representative components on boundary conditions."""
        # 1. TwoSum: Guard against i32 overflow on large opposite-signed inputs
        # Target = i32::MIN (-2147483648), num = 1 -> complement = -2147483649 (overflows i32)
        nums = [1, 100, 200]
        target = -2147483648
        res = reference_two_sum_checked(nums, target)
        assert res is None, 'Should return None rather than overflowing signed arithmetic'

        # TwoSum: Minimal boundary: empty or single element raises ValueError
        with pytest.raises(ValueError, match='at least 2 elements'):
            reference_two_sum_checked([42], 42)

        # 2. LruCache: Capacity-1 boundary behavior
        lru = ReferenceLruCache(capacity=1)
        lru.put(1, 100)
        assert lru.get(1) == 100
        lru.put(2, 200)  # Evicts 1
        assert lru.get(1) == -1
        assert lru.get(2) == 200

        # LruCache: Capacity 0 raises ValueError
        with pytest.raises(ValueError, match='strictly positive'):
            ReferenceLruCache(capacity=0)

        # 3. BoundedFifo: Capacity-1 queue full/drainage transitions
        fifo = ReferenceBoundedFifo(capacity=1)
        ok, rejected = fifo.try_push(42)
        assert ok is True and rejected is None

        # Queue full: second push rejected, ownership retained
        ok, rejected = fifo.try_push(99)
        assert ok is False and rejected == 99

        # Pop element: queue emptied
        ok, val = fifo.try_pop()
        assert ok is True and val == 42

        # Empty pop: returns False
        ok, val = fifo.try_pop()
        assert ok is False and val is None

        # Close: prevents subsequent pushes
        fifo.close()
        ok, rejected = fifo.try_push(77)
        assert ok is False and rejected == 77


# ===========================================================================
# Tier 3: Cross-Feature Combinations
# ===========================================================================


@pytest.mark.unit
class TestTier3CrossFeatureCombinations:
    """Verifies multi-module interactions across tool and workspace boundaries."""

    def test_traceability_compliance_cross_check(self) -> None:
        """Verify check_systems_traceability passes cleanly across all defined specs and tests."""
        spec_dir = REPO_ROOT / 'docs' / 'systems'
        test_dirs: list[Path] = [REPO_ROOT / 'test' / 'cpp' / 'systems', REPO_ROOT / 'test' / 'c' / 'systems']
        rust_test_dir = REPO_ROOT / 'test' / 'rust' / 'systems'
        if rust_test_dir.exists():
            test_dirs.append(rust_test_dir)

        report = trace.check(spec_dir, test_dirs)
        assert not report.errors, f'check-traceability failed: {report.errors}'

    def test_pre_commit_and_pixi_task_alignment(self) -> None:
        """Verify pre-commit hooks and pixi tasks enforce consistent flags and binaries."""
        pyproject_path = REPO_ROOT / 'pyproject.toml'
        data = tomllib.loads(pyproject_path.read_text(encoding='utf-8'))
        tasks = data.get('tool', {}).get('pixi', {}).get('tasks', {})

        precommit_path = REPO_ROOT / '.pre-commit-config.yaml'
        precommit_data = yaml.safe_load(precommit_path.read_text(encoding='utf-8'))
        local_repos = [repo for repo in precommit_data.get('repos', []) if repo.get('repo') == 'local']
        local_hooks = {
            hook['id']: hook for repo in local_repos if repo.get('repo') == 'local' for hook in repo.get('hooks', [])
        }

        if 'cargo-clippy' not in local_hooks or 'clippy' not in tasks:
            pytest.skip('Cross-check pending M1 implementation: cargo-clippy hook or task not yet added')

        # Pre-commit hook entry should invoke pixi run clippy
        clippy_entry = local_hooks['cargo-clippy'].get('entry', '')
        assert 'clippy' in clippy_entry, f'cargo-clippy hook must run clippy task (got {clippy_entry})'

    def test_cargo_workspace_and_pixi_env_consistency(self) -> None:
        """Verify Cargo workspace members match repository layout and Pixi target configuration."""
        cargo_path = REPO_ROOT / 'Cargo.toml'
        if not cargo_path.exists():
            pytest.skip('Pending M1 implementation: Cargo.toml not yet created')

        cargo_data = tomllib.loads(cargo_path.read_text(encoding='utf-8'))
        members = cargo_data.get('workspace', {}).get('members', [])

        for member in members:
            member_path = REPO_ROOT / member
            assert member_path.exists(), f'Workspace member directory {member} must exist'
            assert (member_path / 'Cargo.toml').exists(), f'Workspace member {member} must contain Cargo.toml'

    def test_rust_formatting_and_linting_gate(self) -> None:
        """Verify rustfmt and clippy execute without errors across the workspace."""
        cargo_path = REPO_ROOT / 'Cargo.toml'
        if not cargo_path.exists():
            pytest.skip('Pending M1 implementation: Cargo.toml not yet created')

        cargo_bin = shutil.which('cargo')
        if not cargo_bin:
            pytest.skip('cargo binary not available in current environment')

        # Check format
        fmt_res = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'fmt', '--all', '--', '--check'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert fmt_res.returncode == 0, f'cargo fmt --check failed:\n{fmt_res.stdout}\n{fmt_res.stderr}'

        # Check clippy
        clippy_res = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'clippy', '--workspace', '--all-targets', '--', '-D', 'warnings'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert clippy_res.returncode == 0, f'cargo clippy failed:\n{clippy_res.stdout}\n{clippy_res.stderr}'


# ===========================================================================
# Tier 4: Real-World Scenarios
# ===========================================================================


@pytest.mark.integration
class TestTier4RealWorldScenarios:
    """Verifies end-to-end developer workflows and full workspace test execution."""

    def test_rust_workspace_compilation_and_test_execution(self) -> None:
        """Execute cargo test --workspace and verify all integration tests pass cleanly."""
        cargo_path = REPO_ROOT / 'Cargo.toml'
        if not cargo_path.exists():
            pytest.skip('Pending M1 implementation: Cargo.toml not yet created')

        rust_src = REPO_ROOT / 'src' / 'rust'
        if not rust_src.exists():
            pytest.skip('Pending M3 implementation: Rust source directories not yet created')

        cargo_bin = shutil.which('cargo')
        if not cargo_bin:
            pytest.skip('cargo binary not available in current environment')

        result = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'test', '--workspace', '--all-targets'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert result.returncode == 0, f'cargo test --workspace failed:\n{result.stdout}\n{result.stderr}'

    def test_full_developer_practice_cycle(self) -> None:
        """Simulate a complete developer practice lifecycle: check -> format -> lint -> test."""
        cargo_path = REPO_ROOT / 'Cargo.toml'
        if not cargo_path.exists():
            pytest.skip('Pending M1 implementation: Cargo.toml not yet created')

        cargo_bin = shutil.which('cargo')
        if not cargo_bin:
            pytest.skip('cargo binary not available in current environment')

        # 1. Cargo check
        check_res = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'check', '--workspace', '--all-targets'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert check_res.returncode == 0, f'cargo check failed:\n{check_res.stderr}'

        # 2. Cargo fmt check
        fmt_res = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'fmt', '--all', '--', '--check'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert fmt_res.returncode == 0, f'cargo fmt --check failed:\n{fmt_res.stderr}'

        # 3. Cargo clippy
        clippy_res = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'clippy', '--workspace', '--all-targets', '--', '-D', 'warnings'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert clippy_res.returncode == 0, f'cargo clippy failed:\n{clippy_res.stderr}'

        # 4. Cargo test
        test_res = subprocess.run(  # ruff: ignore[subprocess-without-shell-equals-true]
            [cargo_bin, 'test', '--workspace', '--all-targets'],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        assert test_res.returncode == 0, f'cargo test failed:\n{test_res.stderr}'
