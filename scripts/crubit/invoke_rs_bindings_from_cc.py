"""Wrapper CLI to invoke Crubit's ``rs_bindings_from_cc`` from CMake.

Resolves Pixi Clang resource headers, ``libc++`` include paths, and macOS SDK
sysroot flags, runs ``rs_bindings_from_cc`` to generate ``lib.rs`` and
``*_rust_api_impl.cc``, and optionally emits a ``Cargo.toml`` for the generated
Rust bindings crate.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import platform
import subprocess
import sys
from pathlib import Path
from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from collections.abc import Sequence


logger = logging.getLogger(__name__)


def _query_command_output(cmd: Sequence[str]) -> str | None:
    """Runs a helper query command and returns stripped stdout, or ``None`` on failure.

    Args:
        cmd: Command and arguments to execute.

    Returns:
        Stripped stdout text if exit status is 0, else ``None``.
    """
    try:
        proc = subprocess.run(list(cmd), check=True, capture_output=True, text=True)
    except OSError, subprocess.CalledProcessError:
        return None
    output = proc.stdout.strip()
    return output or None


def _resolve_clang_system_flags(cxx: Path) -> list[str]:
    """Resolves Clang resource-dir, ``libc++``, and macOS SDK flags for LibTooling.

    Args:
        cxx: Path to the ``clang++`` executable.

    Returns:
        List of Clang driver/frontend include flags.
    """
    flags: list[str] = []

    resource_dir = _query_command_output([str(cxx), '-print-resource-dir'])
    if resource_dir:
        flags.extend(['-resource-dir', resource_dir])

    # Prefer CONDA_PREFIX libc++ headers so LibTooling sees the exact same STL as Pixi builds.
    libcxx_candidates: list[Path] = []
    conda_prefix = os.environ.get('CONDA_PREFIX')
    if conda_prefix:
        libcxx_candidates.append(Path(conda_prefix) / 'include' / 'c++' / 'v1')
    libcxx_candidates.append(cxx.resolve().parent.parent / 'include' / 'c++' / 'v1')
    for candidate in libcxx_candidates:
        if candidate.is_dir():
            flags.extend(['-isystem', str(candidate)])
            break

    if platform.system() == 'Darwin':
        sdkroot = os.environ.get('SDKROOT') or _query_command_output(['xcrun', '--show-sdk-path'])
        if sdkroot:
            flags.extend(['-isysroot', sdkroot])

    return flags


def _normalize_header(header: str, include_dirs: Sequence[Path]) -> str:
    """Resolves a public header to the exact canonical path Clang records in SourceManager.

    Crubit's ``Importer::GetOwningTargetOfLocation`` compares Clang's resolved
    header filename verbatim against entries in ``--public_headers`` and
    ``--target_args``. Resolving symlinks consistently on both headers and
    ``-I`` / ``-isystem`` include roots ensures macOS ``/var`` -> ``/private/var``
    and relative ``PUBLIC_HEADERS`` arguments always match.

    Args:
        header: Raw header path or include-relative spelling.
        include_dirs: Ordered include directories to check.

    Returns:
        Canonical POSIX header path when found on disk, otherwise ``header``.
    """
    header_path = Path(header)
    if header_path.is_absolute():
        return header_path.resolve().as_posix()

    for inc_dir in include_dirs:
        candidate = inc_dir / header_path
        if candidate.exists():
            return candidate.resolve().as_posix()
    return header_path.as_posix()


def _render_cargo_toml(crate_name: str, rs_out: Path, cargo_toml_out: Path, crubit_src: Path) -> str:
    """Renders a standalone ``Cargo.toml`` for the generated C++->Rust bindings crate.

    Args:
        crate_name: Generated Rust crate name.
        rs_out: Path to generated ``lib.rs``.
        cargo_toml_out: Destination ``Cargo.toml`` path.
        crubit_src: Root of the pinned Crubit source tree.

    Returns:
        TOML text for the generated crate manifest.
    """
    try:
        lib_path = os.path.relpath(rs_out.resolve(), cargo_toml_out.resolve().parent).replace('\\', '/')
    except ValueError:
        lib_path = str(rs_out.resolve()).replace('\\', '/')

    src_root = crubit_src.resolve()
    deps = {
        'crubit_support': src_root / 'support' / 'crubit_support',
        'crubit_bridge_rust': src_root / 'support' / 'crubit_bridge_rust',
        'crubit_annotate': src_root / 'support' / 'crubit_annotate',
        'ffi_11': src_root / 'support' / 'ffi_11',
        'ctor': src_root / 'cargo' / 'support' / 'ctor',
        'cref': src_root / 'cargo' / 'support' / 'cref',
        'forward_declare': src_root / 'cargo' / 'support' / 'forward_declare',
        'oops': src_root / 'cargo' / 'support' / 'oops',
        'operator': src_root / 'cargo' / 'support' / 'operator',
        'dyn_callable_rs': src_root / 'cargo' / 'support' / 'rs_std' / 'dyn_callable_rs',
        'lossy_formatter': src_root / 'cargo' / 'support' / 'rs_std' / 'lossy_formatter',
        'inline_cpp_macro': src_root / 'cargo' / 'support' / 'extract_cpp_from_rust' / 'inline_cpp_macro',
    }
    dep_lines = [f'{name} = {{ path = {json.dumps(str(path))} }}' for name, path in deps.items()]
    dep_lines.append('cxx = "1"')
    dep_lines.append('static_assertions = "1"')

    lines = [
        '# Auto-generated by scripts/crubit/invoke_rs_bindings_from_cc.py. Do not edit.',
        '[package]',
        f'name = {json.dumps(crate_name)}',
        'version = "0.1.0"',
        'edition = "2024"',
        'publish = false',
        '',
        '[workspace]',
        '',
        '[lib]',
        f'path = {json.dumps(lib_path)}',
        '',
        '[dependencies]',
        *dep_lines,
        '',
    ]
    return '\n'.join(lines)


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point for invoking ``rs_bindings_from_cc``.

    Args:
        argv: Optional command-line arguments.

    Returns:
        ``0`` on success, non-zero on failure.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rs-bindings-from-cc', type=Path, required=True)
    parser.add_argument('--crubit-src', type=Path, required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--crate-name', required=True)
    parser.add_argument('--rs-out', type=Path, required=True)
    parser.add_argument('--cc-out', type=Path, required=True)
    parser.add_argument('--cargo-toml-out', type=Path, default=None)
    parser.add_argument('--public-header', action='append', required=True)
    parser.add_argument('--include-dir', action='append', type=Path, default=[])
    parser.add_argument('--system-include-dir', action='append', type=Path, default=[])
    parser.add_argument('--define', action='append', default=[])
    parser.add_argument('--cxx', type=Path, required=True)
    parser.add_argument('--clang-format', type=Path, default=None)
    parser.add_argument('--rustfmt', type=Path, default=None)
    parser.add_argument('--cxx-standard', default='23')
    parser.add_argument('--extra-rs-src', action='append', default=[])
    parser.add_argument('--extra-cpp-src', action='append', default=[])
    parser.add_argument('--extra-clang-arg', action='append', default=[])
    parser.add_argument('--crubit-feature', action='append', default=[])
    args = parser.parse_args(list(argv) if argv is not None else None)

    logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')

    args.rs_out.parent.mkdir(parents=True, exist_ok=True)
    args.cc_out.parent.mkdir(parents=True, exist_ok=True)

    all_include_dirs: list[Path] = [*args.include_dir, *args.system_include_dir]
    normalized_headers = [_normalize_header(hdr, all_include_dirs) for hdr in args.public_header]
    crubit_features = args.crubit_feature or ['supported', 'assume_lifetimes']
    target_args = json.dumps(
        [
            {
                't': args.target,
                'h': normalized_headers,
                'f': crubit_features,
                'c': args.crate_name,
            }
        ],
        separators=(',', ':'),
    )

    cmd: list[str] = [
        str(args.rs_bindings_from_cc),
        f'--target={args.target}',
        f'--rs_out={args.rs_out}',
        f'--cc_out={args.cc_out}',
        '--crubit_support_path_format=<support/{header}>',
        f'--public_headers={",".join(normalized_headers)}',
        f'--target_args={target_args}',
        f'--driver_path={args.cxx}',
    ]
    if args.clang_format is not None and args.clang_format.is_file():
        cmd.append(f'--clang_format_exe_path={args.clang_format}')
    if args.rustfmt is not None and args.rustfmt.is_file():
        cmd.append(f'--rustfmt_exe_path={args.rustfmt}')
    if args.extra_rs_src:
        cmd.append(f'--extra_rs_srcs={",".join(args.extra_rs_src)}')
    if args.extra_cpp_src:
        cmd.append(f'--extra_cpp_srcs={",".join(args.extra_cpp_src)}')

    clang_args: list[str] = [
        '-x',
        'c++',
        f'-std=c++{args.cxx_standard}',
        '-stdlib=libc++',
        *_resolve_clang_system_flags(args.cxx),
        '-isystem',
        str(args.crubit_src.resolve()),
    ]
    for inc_dir in args.include_dir:
        clang_args.extend(['-I', str(inc_dir.resolve())])
    for sys_inc_dir in args.system_include_dir:
        clang_args.extend(['-isystem', str(sys_inc_dir.resolve())])
    clang_args.extend(f'-D{define}' for define in args.define)
    clang_args.extend(args.extra_clang_arg)

    cmd.append('--')
    cmd.extend(clang_args)

    logger.debug('Invoking rs_bindings_from_cc: %s', ' '.join(cmd))
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        logger.error('rs_bindings_from_cc failed with exit code %s', exc.returncode)
        return exc.returncode or 1

    if args.cargo_toml_out is not None:
        args.cargo_toml_out.parent.mkdir(parents=True, exist_ok=True)
        cargo_toml_text = _render_cargo_toml(args.crate_name, args.rs_out, args.cargo_toml_out, args.crubit_src)
        args.cargo_toml_out.write_text(cargo_toml_text, encoding='utf-8')

    return 0


if __name__ == '__main__':
    sys.exit(main())
