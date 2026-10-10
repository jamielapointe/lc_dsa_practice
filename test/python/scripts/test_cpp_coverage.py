"""Tests for cpp_coverage.py."""

from __future__ import annotations

import json
from types import SimpleNamespace
from typing import TYPE_CHECKING

import pytest

from scripts.ci import cpp_coverage


if TYPE_CHECKING:
    from pathlib import Path


def _make_build_dir(tmp_path: Path, *, binaries: tuple[str, ...], profiles: tuple[str, ...]) -> Path:
    """Creates a fake coverage build directory.

    Args:
        tmp_path: Pytest temporary directory.
        binaries: File names to create under ``bin``.
        profiles: File names to create under ``profraw``.

    Returns:
        The fake build directory.
    """
    (tmp_path / 'bin').mkdir()
    (tmp_path / 'profraw').mkdir()
    for name in binaries:
        (tmp_path / 'bin' / name).write_text('')
    for name in profiles:
        (tmp_path / 'profraw' / name).write_text('')
    return tmp_path


def test_find_tool_raises_when_missing(monkeypatch: pytest.MonkeyPatch) -> None:
    """A missing LLVM tool produces an actionable error."""
    monkeypatch.setattr('shutil.which', lambda _name: None)
    with pytest.raises(FileNotFoundError, match='pixi run'):
        cpp_coverage.find_tool('llvm-cov')


def test_find_tool_returns_path(monkeypatch: pytest.MonkeyPatch) -> None:
    """A tool found on PATH is returned unchanged."""
    monkeypatch.setattr('shutil.which', lambda name: f'/opt/{name}')
    assert cpp_coverage.find_tool('llvm-cov') == '/opt/llvm-cov'


def test_find_test_binaries_filters_and_sorts(tmp_path: Path) -> None:
    """Only ``*_test`` files are returned, sorted by name."""
    build_dir = _make_build_dir(tmp_path, binaries=('b_test', 'a_test', 'helper'), profiles=())
    (build_dir / 'bin' / 'c_test').mkdir()  # directories are ignored
    assert [path.name for path in cpp_coverage.find_test_binaries(build_dir)] == ['a_test', 'b_test']


def test_merge_profiles_without_profraw_raises(tmp_path: Path) -> None:
    """Merging with no raw profiles is an error."""
    build_dir = _make_build_dir(tmp_path, binaries=(), profiles=())
    with pytest.raises(FileNotFoundError, match='profraw'):
        cpp_coverage.merge_profiles(build_dir, build_dir / 'merged.profdata')


def test_coverage_command_lists_every_binary(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """The first binary is positional and the rest use ``-object``."""
    monkeypatch.setattr(cpp_coverage, 'find_tool', lambda name: name)
    binaries = [tmp_path / 'a_test', tmp_path / 'b_test']
    command = cpp_coverage.coverage_command('report', binaries, tmp_path / 'p.profdata', '-x')
    assert command[:3] == ['llvm-cov', 'report', str(binaries[0])]
    assert command[3:5] == ['-object', str(binaries[1])]
    assert command[-1] == '-x'
    assert f'-instr-profile={tmp_path / "p.profdata"}' in command


def test_line_coverage_percent_parses_export(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """The percentage comes from the llvm-cov JSON export totals."""
    payload = json.dumps({'data': [{'totals': {'lines': {'percent': 91.5}}}]})
    monkeypatch.setattr(cpp_coverage, 'find_tool', lambda name: name)
    monkeypatch.setattr('subprocess.run', lambda *_args, **_kwargs: SimpleNamespace(stdout=payload))
    assert cpp_coverage.line_coverage_percent([tmp_path / 'a_test'], tmp_path / 'p.profdata') == pytest.approx(91.5)


def test_main_fails_without_binaries(tmp_path: Path) -> None:
    """``main`` reports failure when no test binaries exist."""
    build_dir = _make_build_dir(tmp_path, binaries=(), profiles=())
    assert cpp_coverage.main(['--build-dir', str(build_dir)]) == 1


@pytest.mark.parametrize(('percent', 'expected'), [(90.0, 0), (85.0, 0), (84.99, 1)])
def test_main_enforces_threshold(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, percent: float, expected: int
) -> None:
    """``main`` returns 0 only when coverage meets ``--fail-under``."""
    build_dir = _make_build_dir(tmp_path, binaries=('a_test',), profiles=('1.profraw',))
    monkeypatch.setattr(cpp_coverage, 'merge_profiles', lambda *_args: None)
    monkeypatch.setattr(cpp_coverage, 'find_tool', lambda name: name)
    monkeypatch.setattr('subprocess.run', lambda *_args, **_kwargs: None)
    monkeypatch.setattr(cpp_coverage, 'line_coverage_percent', lambda *_args: percent)
    assert cpp_coverage.main(['--build-dir', str(build_dir), '--fail-under', '85']) == expected
