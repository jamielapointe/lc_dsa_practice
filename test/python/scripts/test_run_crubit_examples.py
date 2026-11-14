"""Offline unit tests for scripts/examples/run_crubit_examples.py."""

from __future__ import annotations

import shutil
import subprocess  # ruff: ignore[suspicious-subprocess-import]
import time
from typing import TYPE_CHECKING

import pytest

from scripts.examples import run_crubit_examples as runner


if TYPE_CHECKING:
    from pathlib import Path


SAMPLE_CPP_LOGS: dict[str, str] = {
    'primitives_enums': '[CRUBIT_CPP_DEMO] module=primitives_enums status=ok pos=15.0 vel=12.0\n',
    'references_views': '[CRUBIT_CPP_DEMO] module=references_views status=ok rms=3.5 tag=42\n',
    'ownership_raii': '[CRUBIT_CPP_DEMO] module=ownership_raii status=ok live_after_drop=0\n',
    'traits_errors': '[CRUBIT_CPP_DEMO] module=traits_errors status=ok schedule=7:3\n',
    'telemetry_capstone': '[CRUBIT_CPP_DEMO] module=telemetry_capstone status=ok committed=2 latched=0\n',
}

SAMPLE_RUST_LOG = """\
[CRUBIT_RUST_DEMO] module=primitives_enums status=ok dist=5.0 phase=kCruise
[CRUBIT_RUST_DEMO] module=references_views status=ok peak=9.5
[CRUBIT_RUST_DEMO] module=ownership_raii status=ok active_after_drop=0
[CRUBIT_RUST_DEMO] module=traits_errors status=ok kp=12.0 kd=3.5
[CRUBIT_RUST_DEMO] module=telemetry_capstone status=ok safe_torque=40.0
"""


def _make_fake_binaries(repo_root: Path, preset: str = 'interop-debug', cargo_profile: str = 'debug') -> None:
    """Creates executable stub files for all 5 C++ demos and the Rust demo."""
    cpp_dir = repo_root / 'build' / preset / 'bin'
    cpp_dir.mkdir(parents=True, exist_ok=True)
    for _mod, name in runner.CPP_DEMO_BINARIES:
        exe = cpp_dir / name
        exe.write_text('#!/bin/sh\nexit 0\n', encoding='utf-8')
        exe.chmod(0o755)

    rust_exe = repo_root / 'build' / preset / 'crubit_cargo' / cargo_profile / runner.RUST_DEMO_BINARY
    rust_exe.parent.mkdir(parents=True, exist_ok=True)
    rust_exe.write_text('#!/bin/sh\nexit 0\n', encoding='utf-8')
    rust_exe.chmod(0o755)


# Tier 1: canonical behavior


def test_parse_demo_log_parses_cpp_and_rust_lines() -> None:
    """Parser extracts structured C++23 and Rust 2024 status records and aliases."""
    combined = (
        '[CRUBIT_CPP_DEMO] module=primitives_and_enums status=ok x=1\n'
        'noise line ignored\n'
        '[CRUBIT_RUST_DEMO] module=telemetry_safety_bridge status=ok crc=0x1234\n'
    )
    records = runner.parse_demo_log(combined)
    assert len(records) == 2
    assert records[0].side == 'cpp'
    assert records[0].module == 'primitives_enums'
    assert records[0].status == 'ok'
    assert records[0].details == 'x=1'
    assert records[1].side == 'rust'
    assert records[1].module == 'telemetry_capstone'
    assert records[1].status == 'ok'


def test_verify_demo_logs_passes_when_all_five_modules_ok() -> None:
    """Valid C++23 and Rust 2024 outputs pass verification cleanly."""
    verdict = runner.verify_demo_logs(SAMPLE_CPP_LOGS, SAMPLE_RUST_LOG)
    assert verdict.passed is True
    assert verdict.errors == []
    assert set(verdict.cpp_records) == set(runner.REQUIRED_MODULES)
    assert set(verdict.rust_records) == set(runner.REQUIRED_MODULES)

    table = runner.format_summary_table(verdict)
    for module in runner.REQUIRED_MODULES:
        assert module in table


def test_discover_binaries_finds_debug_and_release_layouts(tmp_path: Path) -> None:
    """Binary discovery locates C++ demos and Cargo debug/release Rust binaries."""
    _make_fake_binaries(tmp_path, preset='interop-debug', cargo_profile='debug')
    dbg = runner.discover_binaries(repo_root=tmp_path, preset='interop-debug')
    assert len(dbg.cpp_demos) == 5
    assert dbg.rust_demo.parent.name == 'debug'

    _make_fake_binaries(tmp_path, preset='interop-release', cargo_profile='release')
    rel = runner.discover_binaries(repo_root=tmp_path, preset='interop-release')
    assert len(rel.cpp_demos) == 5
    assert rel.rust_demo.parent.name == 'release'


def test_main_runs_all_demos_and_prints_summary(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    """End-to-end mocked run executes all 6 binaries and exits 0 on valid logs."""
    _make_fake_binaries(tmp_path, preset='interop-debug', cargo_profile='debug')

    def fake_run(
        cmd: list[str],
        *,
        capture_output: bool = True,
        text: bool = True,
        timeout: float = 30.0,
        check: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        assert capture_output is True
        assert text is True
        assert timeout > 0
        assert check is False
        exe_name = cmd[0].rsplit('/', maxsplit=1)[-1]
        if exe_name == runner.RUST_DEMO_BINARY:
            return subprocess.CompletedProcess(cmd, 0, stdout=SAMPLE_RUST_LOG, stderr='')
        for mod, bin_name in runner.CPP_DEMO_BINARIES:
            if exe_name == bin_name:
                return subprocess.CompletedProcess(cmd, 0, stdout=SAMPLE_CPP_LOGS[mod], stderr='')
        raise AssertionError(f'Unexpected binary: {exe_name}')

    monkeypatch.setattr(subprocess, 'run', fake_run)
    exit_code = runner.main(['--preset', 'interop-debug', '--timeout', '12.5', '--repo-root', str(tmp_path)])
    captured = capsys.readouterr()

    assert exit_code == 0, captured.err
    assert 'Crubit Interop Showcase Summary' in captured.out
    assert 'CRUBIT EXAMPLES PASSED (5/5 modules verified on C++23 and Rust 2024)' in captured.out


# Tier 2: boundary and error cases


def test_verify_demo_logs_detects_missing_modules_and_non_ok_status() -> None:
    """Missing modules or non-ok status lines fail verification."""
    incomplete_cpp = dict(SAMPLE_CPP_LOGS)
    incomplete_cpp['ownership_raii'] = ''
    bad_rust = SAMPLE_RUST_LOG.replace('module=traits_errors status=ok', 'module=traits_errors status=error')

    verdict = runner.verify_demo_logs(incomplete_cpp, bad_rust)
    assert verdict.passed is False
    assert any('ownership_raii' in err for err in verdict.errors)
    assert any('traits_errors' in err for err in verdict.errors)


def test_discover_binaries_raises_when_missing_without_build(tmp_path: Path) -> None:
    """Missing binaries raise FileNotFoundError when build_missing is False."""
    with pytest.raises(FileNotFoundError, match='Missing Crubit demo binaries'):
        runner.discover_binaries(repo_root=tmp_path, preset='interop-debug', build_missing=False)


def test_discover_binaries_builds_when_build_missing_enabled(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """With build_missing=True, bootstrap + CMake configure/build are invoked."""
    calls: list[list[str]] = []

    def fake_which(tool: str) -> str | None:
        return '/usr/bin/cmake' if tool == 'cmake' else None

    def fake_run(cmd: list[str], *, cwd: Path | None = None, check: bool = False) -> subprocess.CompletedProcess[str]:
        assert cwd == tmp_path.resolve()
        assert check is True
        calls.append(cmd)
        if 'bootstrap_crubit.py' in cmd[-1]:
            crubit_paths = tmp_path / 'build' / 'crubit' / 'crubit_paths.cmake'
            crubit_paths.parent.mkdir(parents=True, exist_ok=True)
            crubit_paths.write_text('# stub\n', encoding='utf-8')
        elif '--build' in cmd:
            _make_fake_binaries(tmp_path, preset='interop-debug', cargo_profile='debug')
        return subprocess.CompletedProcess(cmd, 0)

    monkeypatch.setattr(shutil, 'which', fake_which)
    monkeypatch.setattr(subprocess, 'run', fake_run)

    discovered = runner.discover_binaries(repo_root=tmp_path, preset='interop-debug', build_missing=True)
    assert len(discovered.cpp_demos) == 5
    assert discovered.rust_demo.is_file()
    assert len(calls) == 3


def test_run_crubit_examples_handles_nonzero_exit_and_timeout(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    """Non-zero binary exit codes and timeouts return exit code 1."""
    _make_fake_binaries(tmp_path, preset='interop-debug', cargo_profile='debug')

    def fake_nonzero(
        cmd: list[str],
        **_kwargs: object,
    ) -> subprocess.CompletedProcess[str]:
        exe_name = cmd[0].rsplit('/', maxsplit=1)[-1]
        if exe_name == 'crubit_demo_ownership_raii':
            return subprocess.CompletedProcess(cmd, 2, stdout='', stderr='boom\n')
        if exe_name == runner.RUST_DEMO_BINARY:
            return subprocess.CompletedProcess(cmd, 0, stdout=SAMPLE_RUST_LOG, stderr='')
        for mod, bin_name in runner.CPP_DEMO_BINARIES:
            if exe_name == bin_name:
                return subprocess.CompletedProcess(cmd, 0, stdout=SAMPLE_CPP_LOGS[mod], stderr='')
        raise AssertionError(f'Unexpected binary: {exe_name}')

    monkeypatch.setattr(subprocess, 'run', fake_nonzero)
    assert runner.run_crubit_examples(repo_root=tmp_path) == 1
    assert 'exited with code 2' in capsys.readouterr().err

    def fake_timeout(cmd: list[str], **_kwargs: object) -> subprocess.CompletedProcess[str]:
        raise subprocess.TimeoutExpired(cmd, 1.0)

    monkeypatch.setattr(subprocess, 'run', fake_timeout)
    assert runner.run_crubit_examples(repo_root=tmp_path, timeout_sec=1.0) == 1
    assert 'timed out' in capsys.readouterr().err


def test_run_crubit_examples_returns_one_on_missing_binaries(
    tmp_path: Path,
    capsys: pytest.CaptureFixture[str],
) -> None:
    """Missing binaries result in exit code 1 with diagnostic message on stderr."""
    code = runner.run_crubit_examples(repo_root=tmp_path, preset='interop-debug')
    assert code == 1
    assert 'CRUBIT EXAMPLES FAILED' in capsys.readouterr().err


def test_positive_timeout_rejects_non_positive_values() -> None:
    """CLI rejects zero or negative --timeout values."""
    with pytest.raises(SystemExit):
        runner.main(['--timeout', '0'])


# Tier 3: invariants


def test_all_cpp_binary_modules_match_required_modules() -> None:
    """Every required showcase module has a registered C++23 demo executable."""
    assert tuple(module for module, _bin in runner.CPP_DEMO_BINARIES) == runner.REQUIRED_MODULES


# Tier 4: stress


@pytest.mark.stress
def test_log_verification_scales_over_large_logs() -> None:
    """Parsing and verifying large noisy logs completes well under 1 second."""
    noise = '\n'.join(f'diagnostic line {idx}' for idx in range(5000))
    cpp_logs = {mod: f'{noise}\n{text}' for mod, text in SAMPLE_CPP_LOGS.items()}
    rust_log = f'{noise}\n{SAMPLE_RUST_LOG}\n{noise}'

    start = time.perf_counter()
    verdict = runner.verify_demo_logs(cpp_logs, rust_log)
    elapsed = time.perf_counter() - start

    assert verdict.passed is True
    assert elapsed < 1.0
