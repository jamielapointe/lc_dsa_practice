"""Offline unit tests for scripts/crubit/bootstrap_crubit.py."""

from __future__ import annotations

import hashlib
import json
from typing import TYPE_CHECKING
from typing import Self

import httpx
import pytest

from scripts.crubit import bootstrap_crubit


if TYPE_CHECKING:
    from collections.abc import Iterator
    from pathlib import Path


def _sha256_bytes(payload: bytes) -> str:
    """Computes lowercase SHA-256 hex digest for in-memory bytes.

    Args:
        payload: Byte buffer to digest.

    Returns:
        64-character hex digest.
    """
    return hashlib.sha256(payload).hexdigest()


def _populate_complete_bootstrap(layout: bootstrap_crubit.CrubitLayout, triple: str) -> None:
    """Creates minimal fake files and valid stamps representing a bootstrapped layout.

    Args:
        layout: Target ``CrubitLayout`` under ``tmp_path``.
        triple: Host triple to populate for.
    """
    spec = bootstrap_crubit.TOOLCHAIN_SPECS[triple]
    (layout.src_dir / '.git').mkdir(parents=True, exist_ok=True)
    (layout.src_dir / 'support' / 'crubit_support').mkdir(parents=True, exist_ok=True)
    (layout.sysroot_dir / 'bin').mkdir(parents=True, exist_ok=True)
    (layout.sysroot_dir / 'bin' / 'rustc').write_text('#!/bin/sh\n', encoding='utf-8')
    (layout.sysroot_dir / 'bin' / 'cargo').write_text('#!/bin/sh\n', encoding='utf-8')
    ext = 'dylib' if triple.endswith('apple-darwin') else 'so'
    (layout.sysroot_dir / 'lib').mkdir(parents=True, exist_ok=True)
    (layout.sysroot_dir / 'lib' / f'librustc_driver-test.{ext}').write_bytes(b'elf-or-macho')

    layout.install_bin_dir.mkdir(parents=True, exist_ok=True)
    layout.install_libexec_dir.mkdir(parents=True, exist_ok=True)
    for tool in ('cargo-cpp_api_from_rust', 'cc_bindings_from_rs', 'rs_bindings_from_cc'):
        (layout.install_bin_dir / tool).write_text('#!/bin/sh\n', encoding='utf-8')
    for tool in ('cargo-cpp_api_from_rust', 'cc_bindings_from_rs'):
        (layout.install_libexec_dir / tool).write_bytes(b'bin')

    bootstrap_crubit.write_stamp(
        layout.stamps_dir / 'source.stamp',
        {'commit': bootstrap_crubit.CRUBIT_COMMIT, 'repo': bootstrap_crubit.CRUBIT_REPO_URL},
    )
    bootstrap_crubit.write_stamp(
        layout.stamps_dir / 'toolchain.stamp',
        {
            'rust_version': bootstrap_crubit.RUST_VERSION,
            'rust_dist_date': bootstrap_crubit.RUST_DIST_DATE,
            'triple': spec.triple,
            'rust_sha256': spec.rust_sha256,
            'rustc_dev_sha256': spec.rustc_dev_sha256,
        },
    )
    tool_stamp: dict[str, object] = {
        'commit': bootstrap_crubit.CRUBIT_COMMIT,
        'rust_version': bootstrap_crubit.RUST_VERSION,
        'triple': triple,
        'schema': bootstrap_crubit.TOOL_SCHEMA_VERSION,
    }
    bootstrap_crubit.write_stamp(layout.stamps_dir / 'rust_tools.stamp', tool_stamp)
    bootstrap_crubit.write_stamp(layout.stamps_dir / 'rs_bindings_from_cc.stamp', tool_stamp)
    bootstrap_crubit.write_metadata_files(layout, triple)


@pytest.mark.parametrize(
    ('system', 'machine', 'expected'),
    [
        ('Linux', 'x86_64', 'x86_64-unknown-linux-gnu'),
        ('Linux', 'amd64', 'x86_64-unknown-linux-gnu'),
        ('Linux', 'aarch64', 'aarch64-unknown-linux-gnu'),
        ('Linux', 'arm64', 'aarch64-unknown-linux-gnu'),
        ('Darwin', 'arm64', 'aarch64-apple-darwin'),
        ('Darwin', 'aarch64', 'aarch64-apple-darwin'),
    ],
)
def test_detect_host_triple_supported(system: str, machine: str, expected: str) -> None:
    """Supported OS/architecture pairs map to pinned Rust triples."""
    assert bootstrap_crubit.detect_host_triple(system=system, machine=machine) == expected


@pytest.mark.parametrize(
    ('system', 'machine'),
    [
        ('Darwin', 'x86_64'),
        ('Linux', 'riscv64'),
        ('Windows', 'AMD64'),
    ],
)
def test_detect_host_triple_unsupported_raises(system: str, machine: str) -> None:
    """Unsupported OS/architecture pairs raise BootstrapError."""
    with pytest.raises(bootstrap_crubit.BootstrapError, match='Unsupported host platform'):
        bootstrap_crubit.detect_host_triple(system=system, machine=machine)


def test_sha256_verification_paths(tmp_path: Path) -> None:
    """SHA-256 verification accepts matching files and rejects missing or corrupted ones."""
    sample = tmp_path / 'sample.tar.xz'
    payload = b'crubit-test-payload'
    sample.write_bytes(payload)
    expected = _sha256_bytes(payload)

    assert bootstrap_crubit.sha256_file(sample) == expected
    bootstrap_crubit.verify_sha256(sample, expected.upper())

    with pytest.raises(bootstrap_crubit.BootstrapError, match='SHA-256 mismatch'):
        bootstrap_crubit.verify_sha256(sample, '0' * 64)

    with pytest.raises(bootstrap_crubit.BootstrapError, match='does not exist'):
        bootstrap_crubit.verify_sha256(tmp_path / 'missing.tar.xz', expected)


def test_stamp_read_write_edge_cases(tmp_path: Path) -> None:
    """Stamp helpers handle round-trips, missing files, invalid JSON, and non-dicts."""
    stamp = tmp_path / 'stamps' / 'test.stamp'
    assert bootstrap_crubit.read_stamp(stamp) is None

    bootstrap_crubit.write_stamp(stamp, {'commit': 'abc', 'version': 1})
    assert bootstrap_crubit.read_stamp(stamp) == {'commit': 'abc', 'version': 1}

    stamp.write_text('{bad json', encoding='utf-8')
    assert bootstrap_crubit.read_stamp(stamp) is None

    stamp.write_text('["not", "a", "dict"]', encoding='utf-8')
    assert bootstrap_crubit.read_stamp(stamp) is None


def test_download_verified_file_reuses_valid_cache(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Valid cached downloads short-circuit without making HTTP requests."""
    dest = tmp_path / 'downloads' / 'rust.tar.xz'
    dest.parent.mkdir(parents=True)
    payload = b'cached-tarball'
    dest.write_bytes(payload)

    def _unexpected_stream(*_args: object, **_kwargs: object) -> object:
        raise AssertionError('httpx.stream should not be called for verified cache hits')

    monkeypatch.setattr(httpx, 'stream', _unexpected_stream)
    result = bootstrap_crubit.download_verified_file(
        'https://static.rust-lang.org/dist/2026-10-15/rust.tar.xz',
        dest,
        _sha256_bytes(payload),
    )
    assert result == dest


def test_download_verified_file_redownloads_corrupt_cache_and_checks_errors(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Corrupt caches are replaced, bad downloads fail, and non-HTTPS URLs are rejected."""
    dest = tmp_path / 'downloads' / 'rust.tar.xz'
    dest.parent.mkdir(parents=True)
    dest.write_bytes(b'corrupted-cache')

    fresh_payload = b'fresh-verified-tarball'
    fresh_digest = _sha256_bytes(fresh_payload)

    class _FakeResponse:
        def __init__(self, chunks: list[bytes], *, fail_http: bool = False) -> None:
            self._chunks = chunks
            self._fail_http = fail_http

        def __enter__(self) -> Self:
            return self

        def __exit__(self, *_args: object) -> None:
            return None

        def raise_for_status(self) -> None:
            if self._fail_http:
                request = httpx.Request('GET', 'https://example.test/fail')
                raise httpx.HTTPStatusError('404', request=request, response=httpx.Response(404, request=request))

        def iter_bytes(self, chunk_size: int = 1024) -> Iterator[bytes]:
            _ = chunk_size
            yield from self._chunks

    monkeypatch.setattr(httpx, 'stream', lambda *_args, **_kwargs: _FakeResponse([fresh_payload]))
    bootstrap_crubit.download_verified_file('https://example.test/ok.tar.xz', dest, fresh_digest)
    assert dest.read_bytes() == fresh_payload

    with pytest.raises(bootstrap_crubit.BootstrapError, match='Refusing non-HTTPS'):
        bootstrap_crubit.download_verified_file('http://example.test/insecure.tar.xz', dest, fresh_digest)

    dest.unlink()
    monkeypatch.setattr(httpx, 'stream', lambda *_args, **_kwargs: _FakeResponse([b'wrong-bytes']))
    with pytest.raises(bootstrap_crubit.BootstrapError, match='SHA-256 mismatch'):
        bootstrap_crubit.download_verified_file('https://example.test/bad.tar.xz', dest, fresh_digest)
    assert not dest.exists()

    monkeypatch.setattr(httpx, 'stream', lambda *_args, **_kwargs: _FakeResponse([], fail_http=True))
    with pytest.raises(bootstrap_crubit.BootstrapError, match='Failed to download'):
        bootstrap_crubit.download_verified_file('https://example.test/404.tar.xz', dest, fresh_digest)


def test_warm_bootstrap_short_circuits_and_detects_missing_binary(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    """Warm runs skip all work when stamps and binaries exist, and re-trigger if stale."""
    triple = 'aarch64-apple-darwin'
    monkeypatch.setattr(bootstrap_crubit, 'detect_host_triple', lambda *_args, **_kwargs: triple)

    layout = bootstrap_crubit.CrubitLayout(root=tmp_path.resolve())
    assert not bootstrap_crubit.check_bootstrap(layout, triple)

    _populate_complete_bootstrap(layout, triple)
    assert bootstrap_crubit.check_bootstrap(layout, triple)

    def _fail_if_called(*_args: object, **_kwargs: object) -> None:
        raise AssertionError('Warm bootstrap should not invoke build or clone steps')

    monkeypatch.setattr(bootstrap_crubit, 'ensure_crubit_source', _fail_if_called)
    monkeypatch.setattr(bootstrap_crubit, 'ensure_rust_sysroot', _fail_if_called)
    monkeypatch.setattr(bootstrap_crubit, 'ensure_rust_bindings_tools', _fail_if_called)
    monkeypatch.setattr(bootstrap_crubit, 'ensure_rs_bindings_from_cc', _fail_if_called)

    env_data = bootstrap_crubit.bootstrap(layout.root)
    assert env_data['CRUBIT_COMMIT'] == bootstrap_crubit.CRUBIT_COMMIT
    assert env_data['RUSTC_BOOTSTRAP'] == '1'
    assert layout.cmake_paths_file.is_file()
    assert layout.env_json_file.is_file()

    (layout.install_bin_dir / 'rs_bindings_from_cc').unlink()
    assert not bootstrap_crubit.check_bootstrap(layout, triple)


def test_cli_check_and_print_env(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """CLI --check returns 1 when unbootstrapped and 0 + JSON with --print-env when valid."""
    triple = 'x86_64-unknown-linux-gnu'
    monkeypatch.setattr(bootstrap_crubit, 'detect_host_triple', lambda *_args, **_kwargs: triple)

    assert bootstrap_crubit.main(['--crubit-dir', str(tmp_path), '--check']) == 1

    layout = bootstrap_crubit.CrubitLayout(root=tmp_path.resolve())
    _populate_complete_bootstrap(layout, triple)

    assert bootstrap_crubit.main(['--crubit-dir', str(tmp_path), '--check', '--print-env']) == 0
    printed = json.loads(capsys.readouterr().out)
    assert printed['CRUBIT_HOST_TRIPLE'] == triple
    assert printed['CRUBIT_RUST_VERSION'] == bootstrap_crubit.RUST_VERSION


def test_cli_rejects_non_positive_jobs_and_unsupported_host(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """CLI validates --jobs and propagates BootstrapError as exit code 1."""
    with pytest.raises(SystemExit):
        bootstrap_crubit.main(['--crubit-dir', str(tmp_path), '--jobs', '0'])

    def _unsupported(*_args: object, **_kwargs: object) -> str:
        raise bootstrap_crubit.BootstrapError('unsupported')

    monkeypatch.setattr(bootstrap_crubit, 'detect_host_triple', _unsupported)
    assert bootstrap_crubit.main(['--crubit-dir', str(tmp_path), '--check']) == 1
