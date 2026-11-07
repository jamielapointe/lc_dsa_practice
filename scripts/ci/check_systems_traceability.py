"""Bidirectional requirement <-> test traceability check for the systems track.

Requirements live in ``docs/systems/<name>.md`` as level-3 headings ``### <PREFIX>-REQ-NNN: Title`` followed (within the
section) by a ``- **Verification:** <Test|Analysis|Review>`` line. GoogleTest cases in ``test/cpp/systems/*_test.cpp``
and ``test/c/systems/*_test.cpp`` declare what they verify with ``RecordProperty("requirements", "<ID>[,<ID>...]")``.
Rust test cases in ``test/rust/systems/*_test.rs`` declare requirements with comments (``// requirements: <ID>``) or
``record_property!("requirements", "<ID>")``.

The check fails when:

* a requirement ID is defined twice, lacks a verification line, or uses an unknown verification method;
* a requirement verified by ``Test`` has no test referencing it;
* a test references an ID that no spec defines, or a malformed ID.

``--matrix <spec>`` prints a Markdown requirement -> test matrix for one spec instead of checking.
"""

from __future__ import annotations

import argparse
import logging
import re
import sys
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from collections.abc import Sequence


logger = logging.getLogger(__name__)

REQ_ID_PATTERN = r'[A-Z][A-Z0-9]{1,7}-REQ-\d{3}'
_HEADING_RE = re.compile(rf'^###\s+({REQ_ID_PATTERN})\s*:\s*(.+?)\s*$')
_ANY_HEADING_RE = re.compile(r'^#{1,3}\s')
_VERIFICATION_RE = re.compile(r'^\s*[-*]\s+\*\*Verification:\*\*\s*([A-Za-z]+)')
_TEST_RE = re.compile(r'\b(?:TEST|TEST_F|TEST_P|TYPED_TEST|TYPED_TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)')
_RECORD_RE = re.compile(r'RecordProperty\(\s*"requirements"\s*,\s*"([^"]*)"\s*\)')
_RUST_TEST_RE = re.compile(
    r'#\[(?:core::prelude::v[0-9]::)?test\]\s*(?:pub(?:\([^)]+\))?\s+)?(?:async\s+)?fn\s+([a-zA-Z0-9_]+)\s*\('
)
_RUST_TEST_ATTR_RE = re.compile(r'^#\[\s*(?:[a-zA-Z0-9_:]+::)?(?:test|rstest|test_case)\b')
_RUST_COMMENT_REQ_RE = re.compile(r'(?://|///)\s*(?:requirements|REQ):\s*([^\n\r]+)', re.IGNORECASE)
_RUST_MACRO_REQ_RE = re.compile(r'record_property!?\(\s*"requirements"\s*,\s*"([^"]*)"\s*\)')
_VALID_ID_RE = re.compile(rf'^{REQ_ID_PATTERN}$')
VERIFICATION_METHODS = frozenset({'Test', 'Analysis', 'Review'})


@dataclass(frozen=True, slots=True)
class Requirement:
    """One requirement parsed from a spec.

    Attributes:
        req_id: Requirement identifier, e.g. ``SHMPS-REQ-001``.
        title: Heading text after the identifier.
        verification: Verification method (``Test``, ``Analysis``, ``Review``) or ``None`` if missing.
        source: Spec file and line, for messages.
    """

    req_id: str
    title: str
    verification: str | None
    source: str


@dataclass(slots=True)
class TraceabilityReport:
    """Result of a traceability check.

    Attributes:
        requirements: Requirements by ID (first definition wins).
        tests_by_req: Fully qualified test names (``Suite.Name``) that reference each ID.
        errors: Human-readable problems; empty means the check passed.
    """

    requirements: dict[str, Requirement] = field(default_factory=dict)
    tests_by_req: dict[str, list[str]] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)


def parse_spec(path: Path) -> tuple[list[Requirement], list[str]]:
    """Parses the requirements of one spec file.

    Args:
        path: Markdown spec path.

    Returns:
        The requirements in file order and any per-file errors.
    """
    requirements: list[Requirement] = []
    errors: list[str] = []
    current: dict[str, str | int | None] | None = None

    def flush() -> None:
        if current is None:
            return
        req_id = str(current['id'])
        verification = current['verification']
        source = f'{path}:{current["line"]}'
        if verification is None:
            errors.append(f'{source}: {req_id} has no "- **Verification:** <Test|Analysis|Review>" line')
        elif verification not in VERIFICATION_METHODS:
            errors.append(f'{source}: {req_id} has unknown verification method "{verification}"')
        requirements.append(
            Requirement(
                req_id=req_id,
                title=str(current['title']),
                verification=None if verification is None else str(verification),
                source=source,
            )
        )

    in_fence = False
    for line_no, line in enumerate(path.read_text(encoding='utf-8').splitlines(), start=1):
        if line.lstrip().startswith(('```', '~~~')):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if heading := _HEADING_RE.match(line):
            flush()
            current = {'id': heading.group(1), 'title': heading.group(2), 'verification': None, 'line': line_no}
        elif _ANY_HEADING_RE.match(line):
            flush()
            current = None
        elif current is not None and current['verification'] is None and (match := _VERIFICATION_RE.match(line)):
            current['verification'] = match.group(1)
    flush()
    return requirements, errors


def _parse_cpp_tests(path: Path) -> tuple[dict[str, list[str]], list[str]]:
    """Collects requirement references from one GoogleTest source file.

    Args:
        path: C++ test source path.

    Returns:
        Mapping of requirement ID to referencing tests, and any per-file errors.
    """
    refs: dict[str, list[str]] = {}
    errors: list[str] = []
    current_test: str | None = None
    for line_no, line in enumerate(path.read_text(encoding='utf-8').splitlines(), start=1):
        if test := _TEST_RE.search(line):
            current_test = f'{test.group(1)}.{test.group(2)}'
        for record in _RECORD_RE.finditer(line):
            owner = current_test or f'{path.name}:{line_no}'
            for raw in record.group(1).split(','):
                req_id = raw.strip()
                if not _VALID_ID_RE.match(req_id):
                    errors.append(f'{path}:{line_no}: malformed requirement ID "{req_id}" in {owner}')
                    continue
                refs.setdefault(req_id, []).append(owner)
    return refs, errors


def _parse_rust_tests(path: Path) -> tuple[dict[str, list[str]], list[str]]:
    """Collects requirement references from one Rust test source file.

    Supports:
    - ``#[test]`` or ``#[tokio::test]`` test functions (single-line or preceding lines).
    - Stacked attributes (e.g. ``#[test]`` followed by ``#[ignore]``).
    - Line annotations: ``// requirements: <ID>`` or ``// REQ: <ID>`` (including comma-separated IDs).
    - Macro annotations: ``record_property!("requirements", "<ID>")``.

    Args:
        path: Rust test source path.

    Returns:
        Mapping of requirement ID to referencing tests, and any per-file errors.
    """
    refs: dict[str, list[str]] = {}
    errors: list[str] = []
    current_test: str | None = None
    has_test_attr = False

    for line_no, raw_line in enumerate(path.read_text(encoding='utf-8').splitlines(), start=1):
        line = raw_line.strip()

        # Check for same-line #[test] fn <name>(...
        if match := _RUST_TEST_RE.search(line):
            current_test = f'{path.stem}.{match.group(1)}'
            has_test_attr = False
        elif _RUST_TEST_ATTR_RE.match(line):
            has_test_attr = True
        elif has_test_attr:
            if match := re.search(r'^(?:pub(?:\([^)]+\))?\s+)?(?:async\s+)?fn\s+([a-zA-Z0-9_]+)\s*\(', line):
                current_test = f'{path.stem}.{match.group(1)}'
                has_test_attr = False
            elif line.startswith('#['):
                # Stacked attribute (e.g. #[ignore])
                pass
            elif not line or line.startswith('//'):
                # Comments (//, ///) or blank lines between #[test] and fn
                pass
            else:
                has_test_attr = False
        elif re.search(r'^(?:pub(?:\([^)]+\))?\s+)?(?:async\s+)?fn\s+([a-zA-Z0-9_]+)\s*\(', line):
            current_test = None

        # Extract requirements from comments (e.g. // requirements: FIFO-REQ-001)
        for match in _RUST_COMMENT_REQ_RE.finditer(line):
            owner = current_test or f'{path.name}:{line_no}'
            for raw in match.group(1).split(','):
                req_id = raw.strip()
                if not req_id:
                    continue
                if not _VALID_ID_RE.match(req_id):
                    errors.append(f'{path}:{line_no}: malformed requirement ID "{req_id}" in {owner}')
                    continue
                refs.setdefault(req_id, []).append(owner)

        # Extract requirements from macro (e.g. record_property!("requirements", "FIFO-REQ-001"))
        for match in _RUST_MACRO_REQ_RE.finditer(line):
            owner = current_test or f'{path.name}:{line_no}'
            for raw in match.group(1).split(','):
                req_id = raw.strip()
                if not req_id:
                    continue
                if not _VALID_ID_RE.match(req_id):
                    errors.append(f'{path}:{line_no}: malformed requirement ID "{req_id}" in {owner}')
                    continue
                refs.setdefault(req_id, []).append(owner)

    return refs, errors


def parse_tests(path: Path) -> tuple[dict[str, list[str]], list[str]]:
    """Collects requirement references from one test source file.

    Args:
        path: Test source path (C++ or Rust).

    Returns:
        Mapping of requirement ID to referencing tests, and any per-file errors.
    """
    if path.suffix == '.rs':
        return _parse_rust_tests(path)
    return _parse_cpp_tests(path)


def check(spec_dir: Path, test_dir: Path | Sequence[Path]) -> TraceabilityReport:
    """Runs the bidirectional check over every spec and test file.

    Args:
        spec_dir: Directory containing ``*.md`` specs (missing directory means no specs).
        test_dir: Directory or sequence of directories containing test files.

    Returns:
        The populated report.
    """
    report = TraceabilityReport()
    for spec in sorted(spec_dir.glob('*.md')) if spec_dir.is_dir() else []:
        requirements, errors = parse_spec(spec)
        report.errors.extend(errors)
        for requirement in requirements:
            if (existing := report.requirements.get(requirement.req_id)) is not None:
                report.errors.append(
                    f'{requirement.source}: duplicate requirement {requirement.req_id} (first at {existing.source})'
                )
                continue
            report.requirements[requirement.req_id] = requirement

    test_dirs = [test_dir] if isinstance(test_dir, Path) else list(test_dir)
    for tdir in test_dirs:
        if not tdir.is_dir():
            continue
        test_files = sorted(set(tdir.rglob('*_test.cpp')) | set(tdir.rglob('*_test.rs')) | set(tdir.rglob('*.rs')))
        for test_file in test_files:
            refs, errors = parse_tests(test_file)
            report.errors.extend(errors)
            for req_id, tests in refs.items():
                report.tests_by_req.setdefault(req_id, []).extend(tests)

    for req_id, tests in sorted(report.tests_by_req.items()):
        if req_id not in report.requirements:
            report.errors.append(f'unknown requirement {req_id} referenced by {", ".join(sorted(set(tests)))}')

    for req_id, requirement in sorted(report.requirements.items()):
        if requirement.verification == 'Test' and not report.tests_by_req.get(req_id):
            report.errors.append(f'{requirement.source}: {req_id} is verified by Test but no test references it')
    return report


def render_matrix(report: TraceabilityReport, spec: Path) -> str:
    """Renders a Markdown requirement -> test matrix for one spec.

    Args:
        report: Report produced by :func:`check`.
        spec: Spec whose requirements should be listed.

    Returns:
        Markdown table text ending with a newline.
    """
    requirements, _ = parse_spec(spec)
    lines = ['| Requirement | Title | Verification | Tests |', '| --- | --- | --- | --- |']
    for requirement in requirements:
        tests = sorted(set(report.tests_by_req.get(requirement.req_id, [])))
        rendered = '<br>'.join(f'`{name}`' for name in tests) if tests else '—'
        lines.append(
            f'| {requirement.req_id} | {requirement.title} | {requirement.verification or "MISSING"} | {rendered} |'
        )
    return '\n'.join(lines) + '\n'


def main(argv: list[str] | None = None) -> int:
    """Entry point.

    Args:
        argv: Command-line arguments (defaults to ``sys.argv[1:]``).

    Returns:
        Process exit code: 0 when traceability is complete, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--specs', type=Path, default=Path('docs/systems'), help='directory of requirement specs')
    parser.add_argument(
        '--tests',
        type=Path,
        nargs='*',
        default=None,
        help='directories of test sources (default: test/cpp/systems, test/c/systems, test/rust/systems)',
    )
    parser.add_argument('--matrix', type=Path, default=None, help='print the matrix for this spec and exit')
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format='%(message)s')

    test_dirs: list[Path] = (
        args.tests
        if args.tests
        else [
            Path('test/cpp/systems'),
            Path('test/c/systems'),
            Path('test/rust/systems'),
        ]
    )
    report = check(args.specs, test_dirs)
    if args.matrix is not None:
        sys.stdout.write(render_matrix(report, args.matrix))
        return 0
    for error in report.errors:
        logger.error('traceability: %s', error)
    if report.errors:
        logger.error('traceability: %d problem(s) found', len(report.errors))
        return 1
    logger.info(
        'traceability: %d requirement(s), %d referenced by tests, all consistent',
        len(report.requirements),
        sum(1 for req_id in report.requirements if report.tests_by_req.get(req_id)),
    )
    return 0


if __name__ == '__main__':
    sys.exit(main())
