"""Orchestrate and verify the Google Crubit C++23 <-> Rust 2024 showcase demos.

Locates (or optionally builds) the five per-module C++23 demo binaries and the
unified Rust 2024 showcase binary, executes all six executables, parses their
structured ``[CRUBIT_CPP_DEMO]`` and ``[CRUBIT_RUST_DEMO]`` output lines, and
verifies that all five showcase modules report ``status=ok`` on both sides.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from dataclasses import field
from pathlib import Path
from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from collections.abc import Sequence


REQUIRED_MODULES: tuple[str, ...] = (
    'primitives_enums',
    'references_views',
    'ownership_raii',
    'traits_errors',
    'telemetry_capstone',
)

CPP_DEMO_BINARIES: tuple[tuple[str, str], ...] = (
    ('primitives_enums', 'crubit_demo_primitives_enums'),
    ('references_views', 'crubit_demo_references_views'),
    ('ownership_raii', 'crubit_demo_ownership_raii'),
    ('traits_errors', 'crubit_demo_traits_errors'),
    ('telemetry_capstone', 'crubit_demo_telemetry_capstone'),
)

RUST_DEMO_BINARY = 'crubit_showcase_rust_demo'

_MODULE_ALIASES: dict[str, str] = {
    'primitives_enums': 'primitives_enums',
    'primitives_and_enums': 'primitives_enums',
    'references_views': 'references_views',
    'references_and_views': 'references_views',
    'ownership_raii': 'ownership_raii',
    'ownership_and_raii': 'ownership_raii',
    'traits_errors': 'traits_errors',
    'traits_and_errors': 'traits_errors',
    'telemetry_capstone': 'telemetry_capstone',
    'telemetry_safety_bridge': 'telemetry_capstone',
    'telemetry_bridge': 'telemetry_capstone',
    'telemetry_validator': 'telemetry_capstone',
}

_DEMO_LINE_RE = re.compile(r'^\[(CRUBIT_CPP_DEMO|CRUBIT_RUST_DEMO)\]\s+module=([^\s]+)\s+status=([^\s]+)(?:\s+(.*))?$')


@dataclass(frozen=True, slots=True)
class DemoRecord:
    """One parsed structured demo status line.

    Attributes:
        side: Language side tag (``'cpp'`` or ``'rust'``).
        module: Canonical showcase module identifier.
        raw_module: Unnormalized module token from the log line.
        status: Reported status string (``'ok'`` on success).
        details: Trailing key-value fields from the log line.
    """

    side: str
    module: str
    raw_module: str
    status: str
    details: str


@dataclass(frozen=True, slots=True)
class DiscoveredBinaries:
    """Resolved executable paths for the C++23 and Rust 2024 demos.

    Attributes:
        cpp_demos: Mapping from canonical module name to C++23 executable path.
        rust_demo: Path to the Rust 2024 ``crubit_showcase_rust_demo`` binary.
    """

    cpp_demos: dict[str, Path]
    rust_demo: Path


@dataclass(slots=True)
class VerificationResult:
    """Result of validating structured C++23 and Rust 2024 demo outputs.

    Attributes:
        passed: ``True`` when all 5 modules passed on both C++23 and Rust 2024 sides.
        cpp_records: Mapping from canonical module name to parsed C++23 record.
        rust_records: Mapping from canonical module name to parsed Rust 2024 record.
        errors: Human-readable verification failure descriptions.
    """

    passed: bool
    cpp_records: dict[str, DemoRecord] = field(default_factory=dict)
    rust_records: dict[str, DemoRecord] = field(default_factory=dict)
    errors: list[str] = field(default_factory=list)


def normalize_module_name(raw_name: str) -> str:
    """Normalizes a demo module name to its canonical showcase identifier.

    Args:
        raw_name: Raw ``module=...`` token from stdout.

    Returns:
        Canonical module name when recognized, otherwise the stripped input.
    """
    cleaned = raw_name.strip().lower()
    return _MODULE_ALIASES.get(cleaned, cleaned)


def parse_demo_log(log_text: str) -> list[DemoRecord]:
    """Parses ``[CRUBIT_CPP_DEMO]`` and ``[CRUBIT_RUST_DEMO]`` stdout lines.

    Args:
        log_text: Combined stdout text emitted by one or more demo binaries.

    Returns:
        Ordered list of parsed :class:`DemoRecord` entries.
    """
    records: list[DemoRecord] = []
    for raw_line in log_text.splitlines():
        line = raw_line.strip()
        if match := _DEMO_LINE_RE.match(line):
            tag, raw_module, status, details = match.groups()
            side = 'cpp' if tag == 'CRUBIT_CPP_DEMO' else 'rust'
            records.append(
                DemoRecord(
                    side=side,
                    module=normalize_module_name(raw_module),
                    raw_module=raw_module,
                    status=status.lower(),
                    details=(details or '').strip(),
                )
            )
    return records


def verify_demo_logs(cpp_logs: dict[str, str], rust_log: str) -> VerificationResult:
    """Verifies that all 5 showcase modules report ``status=ok`` in C++23 and Rust 2024.

    Args:
        cpp_logs: Mapping from canonical module name to stdout text for each C++23 demo.
        rust_log: Stdout text emitted by ``crubit_showcase_rust_demo``.

    Returns:
        Populated :class:`VerificationResult`.
    """
    cpp_records: dict[str, DemoRecord] = {}
    rust_records: dict[str, DemoRecord] = {}
    errors: list[str] = []

    for expected_module, log_text in cpp_logs.items():
        parsed = [rec for rec in parse_demo_log(log_text) if rec.side == 'cpp']
        if not parsed:
            errors.append(f'C++23 demo {expected_module!r} emitted no [CRUBIT_CPP_DEMO] status lines')
            continue
        for rec in parsed:
            if rec.status != 'ok':
                errors.append(f'C++23 module {rec.module!r} reported non-ok status={rec.status!r}')
            cpp_records[rec.module] = rec
        if expected_module not in cpp_records or cpp_records[expected_module].status != 'ok':
            errors.append(f'C++23 demo {expected_module!r} did not report module={expected_module} status=ok')

    parsed_rust = [rec for rec in parse_demo_log(rust_log) if rec.side == 'rust']
    if not parsed_rust:
        errors.append('Rust 2024 demo emitted no [CRUBIT_RUST_DEMO] status lines')
    for rec in parsed_rust:
        if rec.status != 'ok':
            errors.append(f'Rust 2024 module {rec.module!r} reported non-ok status={rec.status!r}')
        rust_records[rec.module] = rec

    for module in REQUIRED_MODULES:
        if module not in cpp_records:
            msg = f'Missing C++23 status=ok line for module {module!r}'
            if msg not in errors and not any(module in err and 'C++23' in err for err in errors):
                errors.append(msg)
        elif cpp_records[module].status != 'ok':
            msg = f'C++23 module {module!r} failed with status={cpp_records[module].status!r}'
            if msg not in errors:
                errors.append(msg)

        if module not in rust_records:
            errors.append(f'Missing Rust 2024 status=ok line for module {module!r}')
        elif rust_records[module].status != 'ok':
            msg = f'Rust 2024 module {module!r} failed with status={rust_records[module].status!r}'
            if msg not in errors:
                errors.append(msg)

    return VerificationResult(
        passed=not errors,
        cpp_records=cpp_records,
        rust_records=rust_records,
        errors=errors,
    )


def _is_executable(path: Path) -> bool:
    """Returns whether ``path`` exists as a regular executable file."""
    return path.is_file() and os.access(path, os.X_OK)


def _cargo_profile_candidates(preset: str) -> tuple[str, ...]:
    """Returns Cargo profile subdirectories ordered by likelihood for ``preset``."""
    if 'release' in preset.lower():
        return ('release', 'debug')
    return ('debug', 'release')


def _locate_binaries(build_dir: Path, preset: str) -> tuple[dict[str, Path], Path | None]:
    """Searches ``build/<preset>`` for all six Crubit demo binaries.

    Args:
        build_dir: Preset build directory (``build/<preset>``).
        preset: CMake preset name used to determine preferred Cargo profile.

    Returns:
        Tuple of ``(cpp_demos, rust_demo)``.
    """
    cpp_bin_dir = build_dir / 'bin'
    cpp_demos: dict[str, Path] = {}
    for module_name, binary_name in CPP_DEMO_BINARIES:
        candidate = cpp_bin_dir / binary_name
        if _is_executable(candidate):
            cpp_demos[module_name] = candidate.resolve()

    rust_demo: Path | None = None
    for profile in _cargo_profile_candidates(preset):
        candidate = build_dir / 'crubit_cargo' / profile / RUST_DEMO_BINARY
        if _is_executable(candidate):
            rust_demo = candidate.resolve()
            break

    return cpp_demos, rust_demo


def discover_binaries(
    *,
    repo_root: Path | None = None,
    preset: str = 'interop-debug',
    build_missing: bool = False,
) -> DiscoveredBinaries:
    """Locates or optionally builds all Crubit C++23 and Rust 2024 demo executables.

    Args:
        repo_root: Repository root directory (defaults to two levels above this file).
        preset: CMake preset directory under ``build/<preset>``.
        build_missing: If ``True``, runs Crubit bootstrap and CMake build when any binary is absent.

    Returns:
        :class:`DiscoveredBinaries` containing all six executable paths.

    Raises:
        FileNotFoundError: If any binary is missing (and could not be built).
    """
    root = (repo_root or Path(__file__).resolve().parent.parent.parent).resolve()
    build_dir = root / 'build' / preset
    cpp_demos, rust_demo = _locate_binaries(build_dir, preset)

    missing_cpp = [binary for module, binary in CPP_DEMO_BINARIES if module not in cpp_demos]
    if missing_cpp or rust_demo is None:
        if build_missing:
            _build_preset_demos(root, preset)
            cpp_demos, rust_demo = _locate_binaries(build_dir, preset)
            missing_cpp = [binary for module, binary in CPP_DEMO_BINARIES if module not in cpp_demos]

        if missing_cpp or rust_demo is None:
            missing_items = list(missing_cpp)
            if rust_demo is None:
                missing_items.append(RUST_DEMO_BINARY)
            hint = 'Pass --build-missing or run `pixi run examples-interop` first.'
            message = f'Missing Crubit demo binaries under {build_dir}: {", ".join(missing_items)}. {hint}'
            raise FileNotFoundError(message)

    return DiscoveredBinaries(cpp_demos=cpp_demos, rust_demo=rust_demo)


def _build_preset_demos(root: Path, preset: str) -> None:
    """Bootstraps Crubit if needed, configures ``preset``, and builds all interop demos.

    Args:
        root: Repository root directory.
        preset: CMake preset name to configure and build.

    Raises:
        FileNotFoundError: If ``cmake`` cannot be found on ``PATH``.
    """
    crubit_paths = root / 'build' / 'crubit' / 'crubit_paths.cmake'
    if not crubit_paths.is_file():
        bootstrap_script = root / 'scripts' / 'crubit' / 'bootstrap_crubit.py'
        subprocess.run([sys.executable, str(bootstrap_script)], cwd=root, check=True)

    cmake_bin = shutil.which('cmake')
    if cmake_bin is None:
        message = 'cmake executable not found on PATH; run inside `pixi run`'
        raise FileNotFoundError(message)

    cache_file = root / 'build' / preset / 'CMakeCache.txt'
    if not cache_file.is_file():
        subprocess.run([cmake_bin, '--preset', preset], cwd=root, check=True)

    subprocess.run([cmake_bin, '--build', '--preset', preset], cwd=root, check=True)


def _run_binary(binary_path: Path, timeout_sec: float) -> tuple[int, str, str]:
    """Executes one demo binary with a timeout and captures stdout/stderr.

    Args:
        binary_path: Path to executable binary.
        timeout_sec: Execution timeout in seconds.

    Returns:
        Tuple of ``(returncode, stdout, stderr)``.
    """
    proc = subprocess.run(
        [str(binary_path)],
        capture_output=True,
        text=True,
        timeout=timeout_sec,
        check=False,
    )
    return proc.returncode, proc.stdout, proc.stderr


def format_summary_table(verdict: VerificationResult) -> str:
    """Formats a crisp ASCII summary table for the 5 showcase modules.

    Args:
        verdict: Completed :class:`VerificationResult`.

    Returns:
        Multi-line formatted summary table.
    """
    header = f'{"Module":<20} | {"C++23":<8} | {"Rust 2024":<9} | Details'
    divider = '-' * len(header)
    lines = [header, divider]

    for module in REQUIRED_MODULES:
        cpp_rec = verdict.cpp_records.get(module)
        rust_rec = verdict.rust_records.get(module)
        cpp_status = cpp_rec.status.upper() if cpp_rec else 'MISSING'
        rust_status = rust_rec.status.upper() if rust_rec else 'MISSING'
        detail = (cpp_rec.details if cpp_rec and cpp_rec.details else None) or (
            rust_rec.details if rust_rec and rust_rec.details else '-'
        )
        lines.append(f'{module:<20} | {cpp_status:<8} | {rust_status:<9} | {detail}')
    return '\n'.join(lines)


def run_crubit_examples(
    *,
    preset: str = 'interop-debug',
    timeout_sec: float = 30.0,
    build_missing: bool = False,
    repo_root: Path | None = None,
) -> int:
    """Runs all 5 C++23 demos and the Rust 2024 demo, then verifies structured logs.

    Args:
        preset: CMake preset directory containing the compiled demo binaries.
        timeout_sec: Per-binary execution timeout in seconds.
        build_missing: Whether to build missing binaries automatically via CMake.
        repo_root: Optional repository root override.

    Returns:
        ``0`` if every binary exited ``0`` and all 5 modules passed on both sides, else ``1``.
    """
    try:
        binaries = discover_binaries(
            repo_root=repo_root,
            preset=preset,
            build_missing=build_missing,
        )
    except (FileNotFoundError, subprocess.CalledProcessError) as exc:
        print(f'CRUBIT EXAMPLES FAILED: {exc}', file=sys.stderr)
        return 1

    cpp_logs: dict[str, str] = {}
    failed_exec = False

    for module, _binary_name in CPP_DEMO_BINARIES:
        exe = binaries.cpp_demos[module]
        print(f'==> Running C++23 demo [{module}]: {exe}')
        try:
            code, stdout, stderr = _run_binary(exe, timeout_sec)
        except subprocess.TimeoutExpired:
            print(f'CRUBIT EXAMPLES FAILED: C++23 demo {exe.name} timed out after {timeout_sec}s', file=sys.stderr)
            return 1

        if stdout:
            sys.stdout.write(stdout)
        if stderr:
            sys.stderr.write(stderr)
        if code != 0:
            print(f'CRUBIT EXAMPLES FAILED: C++23 demo {exe.name} exited with code {code}', file=sys.stderr)
            failed_exec = True
        cpp_logs[module] = stdout

    print(f'==> Running Rust 2024 demo: {binaries.rust_demo}')
    try:
        rust_code, rust_stdout, rust_stderr = _run_binary(binaries.rust_demo, timeout_sec)
    except subprocess.TimeoutExpired:
        print(
            f'CRUBIT EXAMPLES FAILED: Rust 2024 demo {binaries.rust_demo.name} timed out after {timeout_sec}s',
            file=sys.stderr,
        )
        return 1

    if rust_stdout:
        sys.stdout.write(rust_stdout)
    if rust_stderr:
        sys.stderr.write(rust_stderr)
    if rust_code != 0:
        print(
            f'CRUBIT EXAMPLES FAILED: Rust 2024 demo {binaries.rust_demo.name} exited with code {rust_code}',
            file=sys.stderr,
        )
        failed_exec = True

    verdict = verify_demo_logs(cpp_logs, rust_stdout)

    print('\n--- Crubit Interop Showcase Summary ---')
    print(format_summary_table(verdict))

    if failed_exec or not verdict.passed:
        for err in verdict.errors:
            print(f'  [ERROR] {err}', file=sys.stderr)
        print('\nCRUBIT EXAMPLES FAILED', file=sys.stderr)
        return 1

    print('\nCRUBIT EXAMPLES PASSED (5/5 modules verified on C++23 and Rust 2024)')
    return 0


def _positive_timeout(raw: str) -> float:
    """Parses a strictly positive floating-point timeout in seconds.

    Args:
        raw: Raw CLI argument value.

    Returns:
        Positive timeout in seconds.

    Raises:
        ArgumentTypeError: If ``raw`` is not a positive float.
    """
    value = float(raw)
    if value <= 0.0:
        message = f'Timeout must be positive, got {raw!r}'
        raise argparse.ArgumentTypeError(message)
    return value


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point for the Crubit C++23 <-> Rust 2024 example runner.

    Args:
        argv: Optional CLI argument list (defaults to ``sys.argv[1:]``).

    Returns:
        Exit code ``0`` on pass, ``1`` on failure.
    """
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        '--preset',
        default='interop-debug',
        help='CMake preset name under build/<preset> (default: interop-debug)',
    )
    parser.add_argument(
        '--timeout',
        type=_positive_timeout,
        default=30.0,
        help='Per-binary timeout in seconds (default: 30.0)',
    )
    parser.add_argument(
        '--build-missing',
        action='store_true',
        help='Bootstrap and build missing binaries via CMake before running demos',
    )
    parser.add_argument(
        '--repo-root',
        type=Path,
        default=None,
        help='Optional repository root override',
    )
    args = parser.parse_args(list(argv) if argv is not None else None)

    return run_crubit_examples(
        preset=args.preset,
        timeout_sec=args.timeout,
        build_missing=args.build_missing,
        repo_root=args.repo_root,
    )


if __name__ == '__main__':
    sys.exit(main())
