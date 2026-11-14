"""Idempotent bootstrapper for Google Crubit and its pinned Rust 1.99.1 sysroot.

Downloads and SHA-256-verifies the official Rust ``1.99.1`` (``2026-10-15``)
toolchain and ``rustc-dev`` tarballs, clones Google Crubit at a pinned commit,
builds ``cargo-cpp_api_from_rust``, ``cc_bindings_from_rs``, and
``rs_bindings_from_cc``, and emits ``crubit_paths.cmake`` and
``crubit_env.json`` under the target ``--crubit-dir``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

import httpx


if TYPE_CHECKING:
    from collections.abc import Sequence


logger = logging.getLogger(__name__)

CRUBIT_COMMIT = 'b7805b5d09574033ec3144d59715a539a4f652b0'
CRUBIT_REPO_URL = 'https://github.com/google/crubit.git'
RUST_VERSION = '1.99.1'
RUST_DIST_DATE = '2026-10-15'
RUST_DIST_BASE_URL = f'https://static.rust-lang.org/dist/{RUST_DIST_DATE}'
TOOL_SCHEMA_VERSION = 1


@dataclass(frozen=True, slots=True)
class ToolchainTarballSpec:
    """Pinned official Rust tarball names and SHA-256 digests for a host triple.

    Attributes:
        triple: Rust target triple (e.g. ``aarch64-apple-darwin``).
        rust_sha256: Expected SHA-256 hex digest of ``rust-<version>-<triple>.tar.xz``.
        rustc_dev_sha256: Expected SHA-256 hex digest of ``rustc-dev-<version>-<triple>.tar.xz``.
    """

    triple: str
    rust_sha256: str
    rustc_dev_sha256: str

    @property
    def rust_filename(self) -> str:
        """File name of the ``rust`` tarball."""
        return f'rust-{RUST_VERSION}-{self.triple}.tar.xz'

    @property
    def rustc_dev_filename(self) -> str:
        """File name of the ``rustc-dev`` tarball."""
        return f'rustc-dev-{RUST_VERSION}-{self.triple}.tar.xz'

    @property
    def rust_url(self) -> str:
        """Download URL for the ``rust`` tarball."""
        return f'{RUST_DIST_BASE_URL}/{self.rust_filename}'

    @property
    def rustc_dev_url(self) -> str:
        """Download URL for the ``rustc-dev`` tarball."""
        return f'{RUST_DIST_BASE_URL}/{self.rustc_dev_filename}'


TOOLCHAIN_SPECS: dict[str, ToolchainTarballSpec] = {
    'x86_64-unknown-linux-gnu': ToolchainTarballSpec(
        triple='x86_64-unknown-linux-gnu',
        rust_sha256='231fcae47ee950621374725b5fc3b5441d2fbda2955ac45dd7dc816d8876cfe7',
        rustc_dev_sha256='e2b1c9ffe50910d5274339b52774eec05433682835219566449cd09c67c1d1d2',
    ),
    'aarch64-unknown-linux-gnu': ToolchainTarballSpec(
        triple='aarch64-unknown-linux-gnu',
        rust_sha256='a48fb1e853be56ce9b8e2751f5464dbd5e16440cd81e237920f3a5e7e806333e',
        rustc_dev_sha256='eb786738654b9f640816aec1e9bf8fed5fc5dd0137ca1a6cec494965ec08a758',
    ),
    'aarch64-apple-darwin': ToolchainTarballSpec(
        triple='aarch64-apple-darwin',
        rust_sha256='30976378b6aaefd1861ba475905c6ba22a44c981418ce52285006af26337f98c',
        rustc_dev_sha256='c89ef69fb6e7c4527bf585de2976940093323761493c1b7e08152b5a15f0c127',
    ),
}


@dataclass(frozen=True, slots=True)
class CrubitLayout:
    """Resolved paths inside a Crubit bootstrap directory.

    Attributes:
        root: Base ``crubit_dir`` resolved to an absolute path.
    """

    root: Path

    @property
    def src_dir(self) -> Path:
        """Crubit git checkout directory."""
        return self.root / 'src'

    @property
    def downloads_dir(self) -> Path:
        """Directory storing downloaded Rust archives."""
        return self.root / 'downloads'

    @property
    def toolchain_dir(self) -> Path:
        """Directory holding the extracted Rust sysroot."""
        return self.root / 'toolchain'

    @property
    def sysroot_dir(self) -> Path:
        """Installed Rust ``1.99.1`` sysroot prefix."""
        return self.toolchain_dir / 'sysroot'

    @property
    def cargo_target_dir(self) -> Path:
        """Isolated Cargo target directory for building Crubit tools."""
        return self.root / 'cargo_target'

    @property
    def rust_tools_lock_dir(self) -> Path:
        """Directory storing the writable copy of Crubit's ``Cargo.lock``."""
        return self.root / 'rust_tools_lock'

    @property
    def rs_bindings_build_dir(self) -> Path:
        """CMake build directory for ``rs_bindings_from_cc``."""
        return self.root / 'rs_bindings_from_cc_build'

    @property
    def install_dir(self) -> Path:
        """Installation prefix for Crubit tools."""
        return self.root / 'install'

    @property
    def install_bin_dir(self) -> Path:
        """Directory for user-facing Crubit executables."""
        return self.install_dir / 'bin'

    @property
    def install_libexec_dir(self) -> Path:
        """Directory for wrapped Crubit binaries."""
        return self.install_dir / 'libexec'

    @property
    def stamps_dir(self) -> Path:
        """Stamp-file directory for idempotent warm checks."""
        return self.root / 'stamps'

    @property
    def cmake_paths_file(self) -> Path:
        """Generated ``crubit_paths.cmake`` file path."""
        return self.root / 'crubit_paths.cmake'

    @property
    def env_json_file(self) -> Path:
        """Generated ``crubit_env.json`` file path."""
        return self.root / 'crubit_env.json'


class BootstrapError(RuntimeError):
    """Raised when Crubit bootstrapping or verification fails."""


def detect_host_triple(system: str | None = None, machine: str | None = None) -> str:
    """Detects the Rust host triple for the current or provided OS and architecture.

    Args:
        system: Optional OS name (defaults to ``platform.system()``).
        machine: Optional CPU architecture (defaults to ``platform.machine()``).

    Returns:
        The matching Rust host triple supported by Crubit.

    Raises:
        BootstrapError: If the OS/architecture pair is unsupported.
    """
    os_name = (system or platform.system()).strip()
    arch = (machine or platform.machine()).strip().lower()

    if os_name == 'Linux':
        if arch in {'x86_64', 'amd64'}:
            return 'x86_64-unknown-linux-gnu'
        if arch in {'aarch64', 'arm64'}:
            return 'aarch64-unknown-linux-gnu'
    elif os_name == 'Darwin' and arch in {'arm64', 'aarch64'}:
        return 'aarch64-apple-darwin'

    message = f'Unsupported host platform for Crubit bootstrap: system={os_name!r}, machine={arch!r}'
    raise BootstrapError(message)


def sha256_file(path: Path) -> str:
    """Computes the lowercase SHA-256 hex digest of a file.

    Args:
        path: File to hash.

    Returns:
        64-character lowercase SHA-256 hex digest.
    """
    digest = hashlib.sha256()
    with path.open('rb') as handle:
        while chunk := handle.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def verify_sha256(path: Path, expected_sha256: str) -> None:
    """Verifies a file against an expected SHA-256 digest.

    Args:
        path: File to check.
        expected_sha256: Expected lowercase SHA-256 hex digest.

    Raises:
        BootstrapError: If the file does not exist or its digest does not match.
    """
    if not path.is_file():
        message = f'Expected downloaded archive does not exist: {path}'
        raise BootstrapError(message)
    actual = sha256_file(path)
    if actual != expected_sha256.lower():
        message = f'SHA-256 mismatch for {path.name}: expected {expected_sha256}, got {actual}'
        raise BootstrapError(message)


def read_stamp(stamp_path: Path) -> dict[str, object] | None:
    """Reads a JSON stamp file if present and valid.

    Args:
        stamp_path: Path to the stamp file.

    Returns:
        Parsed JSON object if valid, or ``None`` if missing or malformed.
    """
    if not stamp_path.is_file():
        return None
    try:
        data = json.loads(stamp_path.read_text(encoding='utf-8'))
    except OSError, json.JSONDecodeError:
        return None
    return data if isinstance(data, dict) else None


def write_stamp(stamp_path: Path, payload: dict[str, object]) -> None:
    """Atomically writes a JSON stamp file.

    Args:
        stamp_path: Target stamp path.
        payload: JSON-serializable metadata dictionary.
    """
    stamp_path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = stamp_path.with_suffix('.tmp')
    tmp_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    tmp_path.replace(stamp_path)


def _run(
    cmd: Sequence[str],
    *,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    capture_output: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Runs a subprocess and converts failures into ``BootstrapError``.

    Args:
        cmd: Argument vector to execute.
        cwd: Optional working directory.
        env: Optional full environment mapping.
        capture_output: Whether to capture stdout/stderr instead of streaming.

    Returns:
        Completed process result.

    Raises:
        BootstrapError: If the command exits with non-zero status or cannot be launched.
    """
    logger.debug('Running command: %s (cwd=%s)', ' '.join(cmd), cwd)
    try:
        return subprocess.run(
            list(cmd),
            cwd=cwd,
            env=env,
            check=True,
            text=True,
            capture_output=capture_output,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        stderr = getattr(exc, 'stderr', '') or ''
        detail = f'\n{stderr.strip()}' if stderr else ''
        message = f'Command failed ({cmd[0]}): {exc}{detail}'
        raise BootstrapError(message) from exc


def download_verified_file(url: str, dest: Path, expected_sha256: str) -> Path:
    """Downloads ``url`` to ``dest`` and verifies its SHA-256 digest.

    Reuses an existing file at ``dest`` when its SHA-256 already matches.

    Args:
        url: HTTPS URL to fetch.
        dest: Target file path.
        expected_sha256: Expected SHA-256 hex digest.

    Returns:
        The verified ``dest`` path.

    Raises:
        BootstrapError: If the URL is not HTTPS, download fails, or SHA-256 fails.
    """
    if not url.startswith('https://'):
        message = f'Refusing non-HTTPS download URL: {url}'
        raise BootstrapError(message)

    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_file():
        try:
            verify_sha256(dest, expected_sha256)
            logger.info('Using cached verified archive %s', dest.name)
            return dest
        except BootstrapError:
            logger.warning('Cached archive %s failed SHA-256 check; re-downloading', dest.name)
            dest.unlink(missing_ok=True)

    logger.info('Downloading %s', url)
    part_path = dest.with_suffix(dest.suffix + '.part')
    part_path.unlink(missing_ok=True)
    try:
        with httpx.stream('GET', url, follow_redirects=True, timeout=120.0) as response:
            response.raise_for_status()
            with part_path.open('wb') as handle:
                for chunk in response.iter_bytes(chunk_size=1024 * 1024):
                    handle.write(chunk)
        verify_sha256(part_path, expected_sha256)
        part_path.replace(dest)
    except httpx.HTTPError as exc:
        part_path.unlink(missing_ok=True)
        message = f'Failed to download {url}: {exc}'
        raise BootstrapError(message) from exc
    except Exception:
        part_path.unlink(missing_ok=True)
        raise

    return dest


def ensure_crubit_source(layout: CrubitLayout) -> None:
    """Clones or checks out Google Crubit at the pinned commit.

    Args:
        layout: Crubit directory layout.

    Raises:
        BootstrapError: If git operations fail or HEAD does not match ``CRUBIT_COMMIT``.
    """
    stamp_path = layout.stamps_dir / 'source.stamp'
    expected_stamp: dict[str, object] = {'commit': CRUBIT_COMMIT, 'repo': CRUBIT_REPO_URL}
    if read_stamp(stamp_path) == expected_stamp and (layout.src_dir / '.git').is_dir():
        return

    src_dir = layout.src_dir
    if not (src_dir / '.git').is_dir():
        if src_dir.exists():
            shutil.rmtree(src_dir)
        src_dir.parent.mkdir(parents=True, exist_ok=True)
        _run(['git', 'clone', '--filter=blob:none', CRUBIT_REPO_URL, str(src_dir)])

    head = _run(['git', '-C', str(src_dir), 'rev-parse', 'HEAD'], capture_output=True).stdout.strip()
    if head != CRUBIT_COMMIT:
        _run(['git', '-C', str(src_dir), 'fetch', '--depth=1', 'origin', CRUBIT_COMMIT])
        _run(['git', '-C', str(src_dir), 'checkout', '--detach', CRUBIT_COMMIT])
        head = _run(['git', '-C', str(src_dir), 'rev-parse', 'HEAD'], capture_output=True).stdout.strip()
        if head != CRUBIT_COMMIT:
            message = f'Crubit checkout at {src_dir} resolved to {head}, expected {CRUBIT_COMMIT}'
            raise BootstrapError(message)

    write_stamp(stamp_path, expected_stamp)


def _install_rust_tarball(archive_path: Path, sysroot_dir: Path, *, exclude_docs: bool) -> None:
    """Extracts an official Rust installer archive and installs it into ``sysroot_dir``.

    Args:
        archive_path: Verified ``.tar.xz`` archive path.
        sysroot_dir: Target installation prefix.
        exclude_docs: If ``True``, omits ``rust-docs`` to keep extraction fast.

    Raises:
        BootstrapError: If extraction or ``install.sh`` execution fails.
    """
    with tempfile.TemporaryDirectory(prefix='crubit-rust-extract-') as tmp_name:
        tmp_dir = Path(tmp_name)
        try:
            with tarfile.open(archive_path, mode='r:xz') as tar:
                tar.extractall(tmp_dir, filter='tar')
        except (OSError, tarfile.TarError) as exc:
            message = f'Failed to extract {archive_path}: {exc}'
            raise BootstrapError(message) from exc

        installers = sorted(tmp_dir.glob('*/install.sh'))
        if not installers:
            message = f'No install.sh found inside {archive_path}'
            raise BootstrapError(message)

        installer = installers[0]
        cmd = ['sh', str(installer), f'--prefix={sysroot_dir}', '--disable-ldconfig']
        if exclude_docs:
            components_file = installer.parent / 'components'
            if components_file.is_file():
                components = [
                    line.strip()
                    for line in components_file.read_text(encoding='utf-8').splitlines()
                    if line.strip() and not line.strip().startswith('rust-docs')
                ]
                if components:
                    cmd.append(f'--components={",".join(components)}')
        _run(cmd, cwd=installer.parent)


def _has_rustc_dev(sysroot_dir: Path, triple: str) -> bool:
    """Returns whether ``rustc_driver`` shared library and ``rustc`` binary exist."""
    rustc_bin = sysroot_dir / 'bin' / 'rustc'
    cargo_bin = sysroot_dir / 'bin' / 'cargo'
    if not rustc_bin.is_file() or not cargo_bin.is_file():
        return False
    ext = 'dylib' if triple.endswith('apple-darwin') else 'so'
    lib_candidates = list((sysroot_dir / 'lib').glob(f'librustc_driver*.{ext}'))
    rustlib_candidates = list((sysroot_dir / 'lib' / 'rustlib' / triple / 'lib').glob(f'librustc_driver*.{ext}'))
    return bool(lib_candidates or rustlib_candidates)


def ensure_rust_sysroot(layout: CrubitLayout, spec: ToolchainTarballSpec) -> None:
    """Downloads, verifies, and installs Rust 1.99.1 + rustc-dev into ``sysroot_dir``.

    Args:
        layout: Crubit directory layout.
        spec: Pinned tarball specification for the host triple.

    Raises:
        BootstrapError: If downloading, verification, or installation fails.
    """
    stamp_path = layout.stamps_dir / 'toolchain.stamp'
    expected_stamp: dict[str, object] = {
        'rust_version': RUST_VERSION,
        'rust_dist_date': RUST_DIST_DATE,
        'triple': spec.triple,
        'rust_sha256': spec.rust_sha256,
        'rustc_dev_sha256': spec.rustc_dev_sha256,
    }
    if read_stamp(stamp_path) == expected_stamp and _has_rustc_dev(layout.sysroot_dir, spec.triple):
        return

    rust_archive = download_verified_file(spec.rust_url, layout.downloads_dir / spec.rust_filename, spec.rust_sha256)
    rustc_dev_archive = download_verified_file(
        spec.rustc_dev_url,
        layout.downloads_dir / spec.rustc_dev_filename,
        spec.rustc_dev_sha256,
    )

    if layout.sysroot_dir.exists():
        shutil.rmtree(layout.sysroot_dir)
    layout.sysroot_dir.mkdir(parents=True, exist_ok=True)

    _install_rust_tarball(rust_archive, layout.sysroot_dir, exclude_docs=True)
    _install_rust_tarball(rustc_dev_archive, layout.sysroot_dir, exclude_docs=False)

    if not _has_rustc_dev(layout.sysroot_dir, spec.triple):
        message = f'Sysroot installation under {layout.sysroot_dir} is missing rustc, cargo, or librustc_driver'
        raise BootstrapError(message)

    write_stamp(stamp_path, expected_stamp)


def _sysroot_lib_dirs(sysroot_dir: Path, triple: str) -> list[Path]:
    """Returns sysroot library directories containing ``librustc_driver``."""
    return [
        sysroot_dir / 'lib',
        sysroot_dir / 'lib' / 'rustlib' / triple / 'lib',
    ]


def _encode_rustflags(flags: Sequence[str]) -> str:
    """Joins rustc flags using Cargo's unit-separator encoding.

    Args:
        flags: Rustc flags to encode.

    Returns:
        Unit-separator-joined string suitable for ``CARGO_ENCODED_RUSTFLAGS``.
    """
    return '\x1f'.join(flags)


def _write_tool_wrapper(wrapper_path: Path, target_binary: Path, layout: CrubitLayout, triple: str) -> None:
    """Writes an executable POSIX wrapper script scoping sysroot and ``RUSTC_BOOTSTRAP``.

    Args:
        wrapper_path: Destination script path inside ``install/bin``.
        target_binary: Real compiled binary inside ``install/libexec``.
        layout: Crubit directory layout.
        triple: Host target triple.
    """
    sysroot = layout.sysroot_dir
    lib_dirs = ':'.join(str(path) for path in _sysroot_lib_dirs(sysroot, triple))
    lib_env_var = 'DYLD_LIBRARY_PATH' if triple.endswith('apple-darwin') else 'LD_LIBRARY_PATH'
    script = (
        '#!/bin/sh\n'
        'set -eu\n'
        f'export CARGO="${{CARGO:-{sysroot / "bin" / "cargo"}}}"\n'
        f'export RUSTC="${{RUSTC:-{sysroot / "bin" / "rustc"}}}"\n'
        'export RUSTC_BOOTSTRAP=1\n'
        f'export PATH="{layout.install_bin_dir}:{sysroot / "bin"}:$PATH"\n'
        f'export {lib_env_var}="{lib_dirs}${{{lib_env_var}:+:${lib_env_var}}}"\n'
        f'exec "{target_binary}" "$@"\n'
    )
    wrapper_path.parent.mkdir(parents=True, exist_ok=True)
    wrapper_path.write_text(script, encoding='utf-8')
    wrapper_path.chmod(0o755)


def ensure_rust_bindings_tools(layout: CrubitLayout, triple: str, *, jobs: int | None = None) -> None:
    """Builds ``cargo-cpp_api_from_rust`` and ``cc_bindings_from_rs`` into ``install/bin``.

    Args:
        layout: Crubit directory layout.
        triple: Host target triple.
        jobs: Optional parallel build job count.

    Raises:
        BootstrapError: If Cargo fails to build or install the binaries.
    """
    stamp_path = layout.stamps_dir / 'rust_tools.stamp'
    cargo_bin = layout.sysroot_dir / 'bin' / 'cargo'
    rustc_bin = layout.sysroot_dir / 'bin' / 'rustc'
    expected_bins = (
        layout.install_bin_dir / 'cargo-cpp_api_from_rust',
        layout.install_bin_dir / 'cc_bindings_from_rs',
        layout.install_libexec_dir / 'cargo-cpp_api_from_rust',
        layout.install_libexec_dir / 'cc_bindings_from_rs',
    )
    expected_stamp: dict[str, object] = {
        'commit': CRUBIT_COMMIT,
        'rust_version': RUST_VERSION,
        'triple': triple,
        'schema': TOOL_SCHEMA_VERSION,
    }
    if read_stamp(stamp_path) == expected_stamp and all(path.is_file() for path in expected_bins):
        return

    layout.rust_tools_lock_dir.mkdir(parents=True, exist_ok=True)
    lock_copy = layout.rust_tools_lock_dir / 'Cargo.lock'
    shutil.copy2(layout.src_dir / 'Cargo.lock', lock_copy)

    lib_dirs = _sysroot_lib_dirs(layout.sysroot_dir, triple)
    if triple.endswith('apple-darwin'):
        rustflags = ['-Z', 'osx-rpath-install-name']
        for lib_dir in lib_dirs:
            rustflags.extend(['-C', f'link-args=-Wl,-rpath,{lib_dir}'])
    else:
        rustflags = ['-C', 'link-args=-Wl,-z,origin']
        for lib_dir in lib_dirs:
            rustflags.extend(['-C', f'link-args=-Wl,-rpath,{lib_dir}'])

    env = os.environ.copy()
    env.update({
        'CARGO': str(cargo_bin),
        'RUSTC': str(rustc_bin),
        'RUSTC_BOOTSTRAP': '1',
        'CARGO_TARGET_DIR': str(layout.cargo_target_dir),
        'CARGO_RESOLVER_LOCKFILE_PATH': str(lock_copy),
        'CARGO_ENCODED_RUSTFLAGS': _encode_rustflags(rustflags),
        'PATH': f'{layout.sysroot_dir / "bin"}{os.pathsep}{env.get("PATH", "")}',
    })

    cmd = [
        str(cargo_bin),
        'build',
        '--release',
        '-p',
        'cargo-cpp_api_from_rust',
        '-p',
        'cc_bindings_from_rs',
    ]
    if jobs is not None:
        cmd.extend(['-j', str(jobs)])
    _run(cmd, cwd=layout.src_dir, env=env)

    layout.install_libexec_dir.mkdir(parents=True, exist_ok=True)
    layout.install_bin_dir.mkdir(parents=True, exist_ok=True)

    for tool_name in ('cargo-cpp_api_from_rust', 'cc_bindings_from_rs'):
        built = layout.cargo_target_dir / 'release' / tool_name
        if not built.is_file():
            message = f'Expected built binary missing: {built}'
            raise BootstrapError(message)
        libexec_bin = layout.install_libexec_dir / tool_name
        shutil.copy2(built, libexec_bin)
        libexec_bin.chmod(0o755)
        _write_tool_wrapper(layout.install_bin_dir / tool_name, libexec_bin, layout, triple)

    write_stamp(stamp_path, expected_stamp)


def _resolve_conda_clang() -> tuple[Path, Path]:
    """Resolves ``clang`` and ``clang++`` from ``CONDA_PREFIX`` or ``PATH``.

    Returns:
        Tuple of ``(clang_path, clangxx_path)``.

    Raises:
        BootstrapError: If either compiler cannot be located.
    """
    conda_prefix = os.environ.get('CONDA_PREFIX')
    if conda_prefix:
        clang = Path(conda_prefix) / 'bin' / 'clang'
        clangxx = Path(conda_prefix) / 'bin' / 'clang++'
        if clang.is_file() and clangxx.is_file():
            return clang, clangxx

    which_clang = shutil.which('clang')
    which_clangxx = shutil.which('clang++')
    if which_clang and which_clangxx:
        return Path(which_clang), Path(which_clangxx)

    message = 'Unable to locate clang and clang++ in $CONDA_PREFIX/bin or PATH; run via `pixi run`.'
    raise BootstrapError(message)


def _patch_crubit_cmake_inputs(src_dir: Path) -> None:
    """Filters nonexistent `.proto` entries in Crubit's CMake inputs helper.

    Crubit commit ``b7805b5d`` deleted ``rs_bindings_from_cc/ir.proto`` and
    ``generate_bindings.proto`` after migrating to the ad-hoc IR while leaving
    stale paths in ``cmake/rs_bindings_from_cc/cargo_inputs.cmake``.

    Args:
        src_dir: Cloned Crubit source root inside gitignored ``build/crubit/src``.
    """
    cargo_inputs = src_dir / 'cmake' / 'rs_bindings_from_cc' / 'cargo_inputs.cmake'
    if not cargo_inputs.is_file():
        return
    original = cargo_inputs.read_text(encoding='utf-8')
    old_block = (
        '  foreach(proto IN LISTS protos)\n'
        '    string(REGEX REPLACE "\\\\.proto$" "" stem "${proto}")\n'
        '    list(APPEND inputs "${CRUBIT_SOURCE_ROOT}/${proto}")\n'
        '    list(APPEND outputs "${proto_gen_dir}/${stem}.pb.h" "${proto_gen_dir}/${stem}.pb.cc")\n'
        '  endforeach()\n'
        '\n'
        '  add_custom_command(\n'
        '    OUTPUT ${outputs}\n'
        '    COMMAND "${protoc}" "--cpp_out=${proto_gen_dir}" "-I${CRUBIT_SOURCE_ROOT}" ${inputs}\n'
        '    DEPENDS ${inputs} ${protoc_depends}\n'
        '    COMMENT "Generating C++ Protobuf sources"\n'
        '    VERBATIM\n'
        '  )'
    )
    new_block = (
        '  foreach(proto IN LISTS protos)\n'
        '    if(EXISTS "${CRUBIT_SOURCE_ROOT}/${proto}")\n'
        '      string(REGEX REPLACE "\\\\.proto$" "" stem "${proto}")\n'
        '      list(APPEND inputs "${CRUBIT_SOURCE_ROOT}/${proto}")\n'
        '      list(APPEND outputs "${proto_gen_dir}/${stem}.pb.h" "${proto_gen_dir}/${stem}.pb.cc")\n'
        '    endif()\n'
        '  endforeach()\n'
        '\n'
        '  if(inputs)\n'
        '    add_custom_command(\n'
        '      OUTPUT ${outputs}\n'
        '      COMMAND "${protoc}" "--cpp_out=${proto_gen_dir}" "-I${CRUBIT_SOURCE_ROOT}" ${inputs}\n'
        '      DEPENDS ${inputs} ${protoc_depends}\n'
        '      COMMENT "Generating C++ Protobuf sources"\n'
        '      VERBATIM\n'
        '    )\n'
        '  endif()'
    )
    if old_block in original and new_block not in original:
        cargo_inputs.write_text(original.replace(old_block, new_block, 1), encoding='utf-8')

    function_cc = src_dir / 'rs_bindings_from_cc' / 'importers' / 'function.cc'
    if function_cc.is_file():
        src_text = function_cc.read_text(encoding='utf-8')
        updated = src_text.replace(
            'if (clang::ASTContext::isPFPField(field_decl)) {',
            'if (record_decl->getASTContext().isPFPField(field_decl)) {',
        ).replace(
            'if (clang::ASTContext::isPFPField(field) ||',
            'if (ast_context.isPFPField(field) ||',
        )
        if updated != src_text:
            function_cc.write_text(updated, encoding='utf-8')

    ir_from_cc_h = src_dir / 'rs_bindings_from_cc' / 'ir_from_cc.h'
    if ir_from_cc_h.is_file():
        src_text = ir_from_cc_h.read_text(encoding='utf-8')
        updated = (
            src_text
            .replace(
                'llvm::DenseMap<HeaderName, std::string> virtual_headers_contents_for_testing =\n      {};',
                'llvm::DenseMap<HeaderName, std::string> virtual_headers_contents_for_testing{};',
            )
            .replace(
                'llvm::DenseMap<HeaderName, BazelLabel> headers_to_targets = {};',
                'llvm::DenseMap<HeaderName, BazelLabel> headers_to_targets{};',
            )
            .replace(
                'llvm::DenseMap<BazelLabel, llvm::StringSet<>> crubit_features = {};',
                'llvm::DenseMap<BazelLabel, llvm::StringSet<>> crubit_features{};',
            )
            .replace(
                'llvm::DenseMap<BazelLabel, std::string> crate_names = {};',
                'llvm::DenseMap<BazelLabel, std::string> crate_names{};',
            )
        )
        if updated != src_text:
            ir_from_cc_h.write_text(updated, encoding='utf-8')

    cmdline_h = src_dir / 'rs_bindings_from_cc' / 'cmdline.h'
    if cmdline_h.is_file():
        src_text = cmdline_h.read_text(encoding='utf-8')
        updated = (
            src_text
            .replace(
                'llvm::DenseMap<HeaderName, BazelLabel> headers_to_targets;',
                'llvm::DenseMap<HeaderName, BazelLabel> headers_to_targets{};',
            )
            .replace(
                'llvm::DenseMap<BazelLabel, llvm::StringSet<>> target_to_features;',
                'llvm::DenseMap<BazelLabel, llvm::StringSet<>> target_to_features{};',
            )
            .replace(
                'llvm::DenseMap<BazelLabel, std::string> target_to_crate_name;',
                'llvm::DenseMap<BazelLabel, std::string> target_to_crate_name{};',
            )
        )
        if updated != src_text:
            cmdline_h.write_text(updated, encoding='utf-8')

    importer_cc = src_dir / 'rs_bindings_from_cc' / 'importer.cc'
    if importer_cc.is_file():
        src_text = importer_cc.read_text(encoding='utf-8')
        updated = src_text.replace(
            '(comment_options.RetainCommentsFromSystemHeaders ||\n'
            '         !IsInSystemHeader(sm.getLocForStartOfFile(file)))',
            '(!IsInSystemHeader(sm.getLocForStartOfFile(file)))',
        )
        if updated != src_text:
            importer_cc.write_text(updated, encoding='utf-8')

    gen_bindings_h = src_dir / 'rs_bindings_from_cc' / 'generate_bindings_and_metadata.h'
    if gen_bindings_h.is_file():
        src_text = gen_bindings_h.read_text(encoding='utf-8')
        updated = src_text.replace(
            'virtual_headers_contents_for_testing = {});',
            'virtual_headers_contents_for_testing = llvm::DenseMap<HeaderName, std::string>());',
        )
        if updated != src_text:
            gen_bindings_h.write_text(updated, encoding='utf-8')


def ensure_rs_bindings_from_cc(layout: CrubitLayout, triple: str, *, jobs: int | None = None) -> None:
    """Configures, builds, and installs ``rs_bindings_from_cc`` via CMake.

    Args:
        layout: Crubit directory layout.
        triple: Host target triple.
        jobs: Optional parallel build job count.

    Raises:
        BootstrapError: If CMake configure/build fails or the output binary is missing.
    """
    stamp_path = layout.stamps_dir / 'rs_bindings_from_cc.stamp'
    installed_bin = layout.install_bin_dir / 'rs_bindings_from_cc'
    expected_stamp: dict[str, object] = {
        'commit': CRUBIT_COMMIT,
        'rust_version': RUST_VERSION,
        'triple': triple,
        'schema': TOOL_SCHEMA_VERSION,
    }
    if read_stamp(stamp_path) == expected_stamp and installed_bin.is_file():
        return

    clang, clangxx = _resolve_conda_clang()
    _patch_crubit_cmake_inputs(layout.src_dir)
    cargo_bin = layout.sysroot_dir / 'bin' / 'cargo'
    rustc_bin = layout.sysroot_dir / 'bin' / 'rustc'

    env = os.environ.copy()
    env.update({
        'CC': str(clang),
        'CXX': str(clangxx),
        'CARGO': str(cargo_bin),
        'RUSTC': str(rustc_bin),
        'RUSTC_BOOTSTRAP': '1',
        'PATH': f'{layout.sysroot_dir / "bin"}{os.pathsep}{env.get("PATH", "")}',
    })

    configure_cmd = [
        'cmake',
        '-S',
        str(layout.src_dir / 'cmake' / 'rs_bindings_from_cc'),
        '-B',
        str(layout.rs_bindings_build_dir),
        '-G',
        'Ninja',
        '-DCMAKE_BUILD_TYPE=Release',
        f'-DCMAKE_C_COMPILER={clang}',
        f'-DCMAKE_CXX_COMPILER={clangxx}',
        f'-DCRUBIT_CARGO={cargo_bin}',
        '-DCRUBIT_CARGO_PROFILE=release',
    ]
    _run(configure_cmd, env=env)

    build_cmd = ['cmake', '--build', str(layout.rs_bindings_build_dir)]
    if jobs is not None:
        build_cmd.extend(['--parallel', str(jobs)])
    _run(build_cmd, env=env)

    built_bin = layout.rs_bindings_build_dir / 'bin' / 'rs_bindings_from_cc'
    if not built_bin.is_file():
        message = f'Expected built rs_bindings_from_cc binary missing: {built_bin}'
        raise BootstrapError(message)

    layout.install_bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built_bin, installed_bin)
    installed_bin.chmod(0o755)
    write_stamp(stamp_path, expected_stamp)


def build_env_metadata(layout: CrubitLayout, triple: str) -> dict[str, str]:
    """Constructs the serialized Crubit toolchain environment metadata dictionary.

    Args:
        layout: Crubit directory layout.
        triple: Host target triple.

    Returns:
        Dictionary of tool paths and scoped environment variables.
    """
    sysroot_bin = layout.sysroot_dir / 'bin'
    return {
        'CRUBIT_DIR': str(layout.root),
        'CRUBIT_SOURCE_DIR': str(layout.src_dir),
        'CRUBIT_SYSROOT_DIR': str(layout.sysroot_dir),
        'CRUBIT_INSTALL_DIR': str(layout.install_dir),
        'CRUBIT_SUPPORT_CRATE_DIR': str(layout.src_dir / 'support' / 'crubit_support'),
        'CRUBIT_RUSTC_EXECUTABLE': str(sysroot_bin / 'rustc'),
        'CRUBIT_CARGO_EXECUTABLE': str(sysroot_bin / 'cargo'),
        'CRUBIT_RUSTFMT_EXECUTABLE': str(sysroot_bin / 'rustfmt'),
        'CRUBIT_CLIPPY_DRIVER_EXECUTABLE': str(sysroot_bin / 'clippy-driver'),
        'CRUBIT_RS_BINDINGS_FROM_CC_EXECUTABLE': str(layout.install_bin_dir / 'rs_bindings_from_cc'),
        'CRUBIT_CARGO_CPP_API_FROM_RUST_EXECUTABLE': str(layout.install_bin_dir / 'cargo-cpp_api_from_rust'),
        'CRUBIT_CC_BINDINGS_FROM_RS_EXECUTABLE': str(layout.install_bin_dir / 'cc_bindings_from_rs'),
        'CRUBIT_COMMIT': CRUBIT_COMMIT,
        'CRUBIT_RUST_VERSION': RUST_VERSION,
        'CRUBIT_HOST_TRIPLE': triple,
        'CARGO': str(sysroot_bin / 'cargo'),
        'RUSTC': str(sysroot_bin / 'rustc'),
        'RUSTC_BOOTSTRAP': '1',
        'PATH_PREPEND': f'{layout.install_bin_dir}{os.pathsep}{sysroot_bin}',
    }


def _cmake_escape(value: str) -> str:
    """Escapes backslashes and quotes for a CMake string literal.

    Args:
        value: Raw string value.

    Returns:
        Escaped string safe inside double-quoted CMake literals.
    """
    return value.replace('\\', '/').replace('"', '\\"')


def write_metadata_files(layout: CrubitLayout, triple: str) -> dict[str, str]:
    """Writes ``crubit_paths.cmake`` and ``crubit_env.json`` if needed.

    Args:
        layout: Crubit directory layout.
        triple: Host target triple.

    Returns:
        The environment metadata dictionary written to ``crubit_env.json``.
    """
    metadata = build_env_metadata(layout, triple)
    stamp_path = layout.stamps_dir / 'metadata.stamp'
    expected_stamp: dict[str, object] = {
        'metadata': metadata,
        'schema': TOOL_SCHEMA_VERSION,
    }
    if (
        read_stamp(stamp_path) == expected_stamp
        and layout.cmake_paths_file.is_file()
        and layout.env_json_file.is_file()
    ):
        return metadata

    cmake_lines = [
        '# Auto-generated by scripts/crubit/bootstrap_crubit.py. Do not edit manually.',
        '',
    ]
    for key, value in metadata.items():
        if key.startswith('CRUBIT_'):
            cmake_lines.append(f'set({key} "{_cmake_escape(value)}")')
    cmake_lines.append('')
    layout.cmake_paths_file.write_text('\n'.join(cmake_lines), encoding='utf-8')
    layout.env_json_file.write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    write_stamp(stamp_path, expected_stamp)
    return metadata


def check_bootstrap(layout: CrubitLayout, triple: str) -> bool:
    """Validates that all bootstrap artifacts and stamps exist and match pins.

    Args:
        layout: Crubit directory layout.
        triple: Expected host target triple.

    Returns:
        ``True`` if the Crubit installation is complete and up to date, else ``False``.
    """
    spec = TOOLCHAIN_SPECS.get(triple)
    if spec is None:
        return False

    source_stamp = read_stamp(layout.stamps_dir / 'source.stamp')
    toolchain_stamp = read_stamp(layout.stamps_dir / 'toolchain.stamp')
    rust_tools_stamp = read_stamp(layout.stamps_dir / 'rust_tools.stamp')
    rs_cc_stamp = read_stamp(layout.stamps_dir / 'rs_bindings_from_cc.stamp')
    metadata_stamp = read_stamp(layout.stamps_dir / 'metadata.stamp')

    expected_metadata = build_env_metadata(layout, triple)
    checks = (
        source_stamp == {'commit': CRUBIT_COMMIT, 'repo': CRUBIT_REPO_URL},
        (layout.src_dir / '.git').is_dir(),
        toolchain_stamp
        == {
            'rust_version': RUST_VERSION,
            'rust_dist_date': RUST_DIST_DATE,
            'triple': spec.triple,
            'rust_sha256': spec.rust_sha256,
            'rustc_dev_sha256': spec.rustc_dev_sha256,
        },
        _has_rustc_dev(layout.sysroot_dir, triple),
        rust_tools_stamp
        == {
            'commit': CRUBIT_COMMIT,
            'rust_version': RUST_VERSION,
            'triple': triple,
            'schema': TOOL_SCHEMA_VERSION,
        },
        (layout.install_bin_dir / 'cargo-cpp_api_from_rust').is_file(),
        (layout.install_bin_dir / 'cc_bindings_from_rs').is_file(),
        rs_cc_stamp
        == {
            'commit': CRUBIT_COMMIT,
            'rust_version': RUST_VERSION,
            'triple': triple,
            'schema': TOOL_SCHEMA_VERSION,
        },
        (layout.install_bin_dir / 'rs_bindings_from_cc').is_file(),
        metadata_stamp == {'metadata': expected_metadata, 'schema': TOOL_SCHEMA_VERSION},
        layout.cmake_paths_file.is_file(),
        layout.env_json_file.is_file(),
    )
    return all(checks)


def bootstrap(crubit_dir: Path, *, jobs: int | None = None) -> dict[str, str]:
    """Bootstraps the pinned Crubit source, Rust 1.99.1 sysroot, and tools idempotently.

    Args:
        crubit_dir: Target root directory (typically ``build/crubit``).
        jobs: Optional parallel build job count.

    Returns:
        The generated Crubit environment metadata dictionary.
    """
    layout = CrubitLayout(root=crubit_dir.resolve())
    triple = detect_host_triple()
    spec = TOOLCHAIN_SPECS[triple]

    # Fast path for warm runs: avoid git/cmake subprocesses when stamps and binaries are intact.
    if check_bootstrap(layout, triple):
        return build_env_metadata(layout, triple)

    layout.root.mkdir(parents=True, exist_ok=True)
    ensure_crubit_source(layout)
    ensure_rust_sysroot(layout, spec)
    ensure_rust_bindings_tools(layout, triple, jobs=jobs)
    ensure_rs_bindings_from_cc(layout, triple, jobs=jobs)
    return write_metadata_files(layout, triple)


def _positive_int(raw: str) -> int:
    """Parses a strictly positive integer CLI argument.

    Args:
        raw: Raw string value from the command line.

    Returns:
        Parsed positive integer.

    Raises:
        ArgumentTypeError: If ``raw`` is not a positive integer.
    """
    value = int(raw)
    if value < 1:
        message = f'Expected positive integer, got {raw!r}'
        raise argparse.ArgumentTypeError(message)
    return value


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point for Crubit bootstrapper.

    Args:
        argv: Optional CLI arguments (defaults to ``sys.argv[1:]``).

    Returns:
        ``0`` on success, ``1`` on check failure or bootstrap error.
    """
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--crubit-dir',
        type=Path,
        default=Path('build/crubit'),
        help='Directory where Crubit source, toolchain, and binaries are installed (default: build/crubit).',
    )
    parser.add_argument(
        '--check',
        action='store_true',
        help='Verify that Crubit is already bootstrapped without downloading or building.',
    )
    parser.add_argument(
        '--print-env',
        action='store_true',
        help='Print the Crubit environment JSON (crubit_env.json) to stdout.',
    )
    parser.add_argument(
        '--jobs',
        type=_positive_int,
        default=None,
        help='Maximum parallel jobs passed to Cargo and CMake builds.',
    )
    parser.add_argument('--verbose', '-v', action='store_true', help='Enable debug logging.')
    args = parser.parse_args(list(argv) if argv is not None else None)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format='%(levelname)s: %(message)s',
    )

    start = time.monotonic()
    layout = CrubitLayout(root=args.crubit_dir.resolve())
    try:
        triple = detect_host_triple()
        if args.check:
            if not check_bootstrap(layout, triple):
                logger.error('Crubit bootstrap incomplete or stale under %s', layout.root)
                return 1
            env_data = build_env_metadata(layout, triple)
        else:
            env_data = bootstrap(layout.root, jobs=args.jobs)
    except BootstrapError as exc:
        logger.error('%s', exc)
        return 1

    elapsed = time.monotonic() - start
    logger.debug('Crubit bootstrap finished in %.3fs', elapsed)
    if args.print_env:
        print(json.dumps(env_data, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
