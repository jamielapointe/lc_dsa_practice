"""Tests for check_systems_traceability.py."""

from __future__ import annotations

import logging
import time
from typing import TYPE_CHECKING

import pytest

from scripts.ci import check_systems_traceability as trace


if TYPE_CHECKING:
    from pathlib import Path


SPEC = """# Demo — Software Requirements

## 6. Functional requirements

### DEMO-REQ-001: Creates a channel

The component shall create a channel.

- **Verification:** Test

### DEMO-REQ-002: Header has no heap use

- **Verification:** Review

## 7. Template example (ignored inside fences)

```markdown
### DEMO-REQ-999: Not a real requirement
```
"""

TESTS = """
TEST(DemoTest, Creates) {
  RecordProperty("requirements", "DEMO-REQ-001");
}
TEST_F(DemoFixture, AlsoCreates) {
  RecordProperty("requirements", "DEMO-REQ-001,DEMO-REQ-002");
}
"""


def _write_tree(tmp_path: Path, *, spec: str = SPEC, tests: str = TESTS) -> tuple[Path, Path]:
    """Creates a spec directory and a test directory.

    Args:
        tmp_path: Pytest temporary directory.
        spec: Spec Markdown content.
        tests: C++ test content.

    Returns:
        The spec and test directories.
    """
    spec_dir = tmp_path / 'docs'
    test_dir = tmp_path / 'tests'
    spec_dir.mkdir()
    test_dir.mkdir()
    (spec_dir / 'demo.md').write_text(spec, encoding='utf-8')
    (test_dir / 'demo_test.cpp').write_text(tests, encoding='utf-8')
    return spec_dir, test_dir


# Tier 1: canonical behavior


def test_consistent_tree_passes(tmp_path: Path) -> None:
    """A spec whose Test requirements are all referenced passes."""
    spec_dir, test_dir = _write_tree(tmp_path)
    report = trace.check(spec_dir, test_dir)
    assert report.errors == []
    assert set(report.requirements) == {'DEMO-REQ-001', 'DEMO-REQ-002'}
    assert sorted(report.tests_by_req['DEMO-REQ-001']) == ['DemoFixture.AlsoCreates', 'DemoTest.Creates']


def test_matrix_lists_requirements_and_tests(tmp_path: Path) -> None:
    """The rendered matrix has one row per requirement with its tests."""
    spec_dir, test_dir = _write_tree(tmp_path)
    report = trace.check(spec_dir, test_dir)
    matrix = trace.render_matrix(report, spec_dir / 'demo.md')
    assert '| DEMO-REQ-001 | Creates a channel | Test | `DemoFixture.AlsoCreates`<br>`DemoTest.Creates` |' in matrix
    assert '| DEMO-REQ-002 | Header has no heap use | Review | `DemoFixture.AlsoCreates` |' in matrix
    assert 'DEMO-REQ-999' not in matrix


def test_main_returns_zero_and_prints_matrix(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """``--matrix`` prints the table and exits 0."""
    spec_dir, test_dir = _write_tree(tmp_path)
    code = trace.main(['--specs', str(spec_dir), '--tests', str(test_dir), '--matrix', str(spec_dir / 'demo.md')])
    assert code == 0
    assert capsys.readouterr().out.startswith('| Requirement |')


# Tier 2: boundary / error cases


def test_untested_requirement_fails(tmp_path: Path) -> None:
    """A Test requirement without any referencing test is an error."""
    spec_dir, test_dir = _write_tree(tmp_path, tests='TEST(DemoTest, Nothing) {}\n')
    report = trace.check(spec_dir, test_dir)
    assert any('DEMO-REQ-001 is verified by Test but no test references it' in e for e in report.errors)
    assert not any('DEMO-REQ-002' in e for e in report.errors)


def test_unknown_and_malformed_ids_fail(tmp_path: Path) -> None:
    """Tests may only reference well-formed IDs that a spec defines."""
    tests = TESTS + 'TEST(DemoTest, Bad) {\n  RecordProperty("requirements", "DEMO-REQ-404,demo-req-1");\n}\n'
    spec_dir, test_dir = _write_tree(tmp_path, tests=tests)
    report = trace.check(spec_dir, test_dir)
    assert any('unknown requirement DEMO-REQ-404 referenced by DemoTest.Bad' in e for e in report.errors)
    assert any('malformed requirement ID "demo-req-1"' in e for e in report.errors)


def test_missing_or_invalid_verification_fails(tmp_path: Path) -> None:
    """Every requirement needs a known verification method."""
    spec = '### DEMO-REQ-001: A\n\ntext\n\n### DEMO-REQ-002: B\n\n- **Verification:** Vibes\n'
    spec_dir, test_dir = _write_tree(tmp_path, spec=spec, tests='')
    report = trace.check(spec_dir, test_dir)
    assert any('DEMO-REQ-001 has no "- **Verification:**' in e for e in report.errors)
    assert any('unknown verification method "Vibes"' in e for e in report.errors)


def test_duplicate_requirement_fails(tmp_path: Path) -> None:
    """IDs must be unique across all specs."""
    spec_dir, test_dir = _write_tree(tmp_path)
    (spec_dir / 'other.md').write_text('### DEMO-REQ-001: Again\n\n- **Verification:** Review\n', encoding='utf-8')
    report = trace.check(spec_dir, test_dir)
    assert any('duplicate requirement DEMO-REQ-001' in e for e in report.errors)


def test_missing_directories_are_empty(tmp_path: Path) -> None:
    """Absent spec and test directories mean nothing to check."""
    report = trace.check(tmp_path / 'nope', tmp_path / 'none')
    assert report.errors == []
    assert report.requirements == {}


def test_record_outside_a_test_is_attributed_to_file(tmp_path: Path) -> None:
    """A RecordProperty before any TEST is attributed to file:line."""
    spec_dir, test_dir = _write_tree(tmp_path, tests='RecordProperty("requirements", "DEMO-REQ-001");\n')
    report = trace.check(spec_dir, test_dir)
    assert report.tests_by_req['DEMO-REQ-001'] == ['demo_test.cpp:1']


# Tier 3: invariants


def test_main_logs_and_fails_on_errors(tmp_path: Path, caplog: pytest.LogCaptureFixture) -> None:
    """The CLI exits non-zero and logs each problem."""
    spec_dir, test_dir = _write_tree(tmp_path, tests='')
    with caplog.at_level(logging.ERROR):
        code = trace.main(['--specs', str(spec_dir), '--tests', str(test_dir)])
    assert code == 1
    assert 'problem(s) found' in caplog.text


def test_main_logs_success(tmp_path: Path, caplog: pytest.LogCaptureFixture) -> None:
    """The CLI exits zero and reports counts on success."""
    spec_dir, test_dir = _write_tree(tmp_path)
    with caplog.at_level(logging.INFO):
        code = trace.main(['--specs', str(spec_dir), '--tests', str(test_dir)])
    assert code == 0
    assert '2 requirement(s), 2 referenced by tests' in caplog.text


# Tier 4: stress


@pytest.mark.stress
def test_many_requirements_scale(tmp_path: Path) -> None:
    """A large spec and test file are checked quickly."""
    count = 999
    spec = ''.join(f'### BIG-REQ-{i:03d}: R{i}\n\n- **Verification:** Test\n\n' for i in range(1, count + 1))
    tests = ''.join(
        f'TEST(Big, T{i}) {{\n  RecordProperty("requirements", "BIG-REQ-{i:03d}");\n}}\n' for i in range(1, count + 1)
    )
    spec_dir, test_dir = _write_tree(tmp_path, spec=spec, tests=tests)
    start = time.perf_counter()
    report = trace.check(spec_dir, test_dir)
    elapsed = time.perf_counter() - start
    assert report.errors == []
    assert len(report.requirements) == count
    assert elapsed < 5.0
