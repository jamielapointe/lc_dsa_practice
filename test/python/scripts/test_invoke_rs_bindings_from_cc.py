"""Unit tests for scripts/crubit/invoke_rs_bindings_from_cc.py."""

import json
import os
import subprocess
import time
import tomllib
from types import SimpleNamespace
from typing import TYPE_CHECKING

import pytest

from scripts.crubit import invoke_rs_bindings_from_cc as invoker


if TYPE_CHECKING:
    from pathlib import Path


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _empty_flags(_cxx: Path) -> list[str]:
    """Returns an empty list of Clang system flags for offline tests.

    Returns:
        Empty flag list.
    """
    return []


def _base_cli_args(tmp_path: Path) -> list[str]:
    """Builds minimal required CLI flags for ``invoke_rs_bindings_from_cc``.

    Args:
        tmp_path: Temporary pytest root directory.

    Returns:
        Minimal CLI argument vector.
    """
    return [
        "--rs-bindings-from-cc",
        str(tmp_path / "bin" / "rs_bindings_from_cc"),
        "--crubit-src",
        str(tmp_path / "crubit_src"),
        "--target",
        "//interop:demo_api",
        "--crate-name",
        "demo_api_rs",
        "--rs-out",
        str(tmp_path / "out" / "src" / "lib.rs"),
        "--cc-out",
        str(tmp_path / "out" / "demo_api_rust_api_impl.cc"),
        "--cxx",
        str(tmp_path / "bin" / "clang++"),
        "--public-header",
        "interop/demo.hpp",
    ]


# ---------------------------------------------------------------------------
# Tier 1: Canonical CMake Invocation & Command Construction
# ---------------------------------------------------------------------------


def test_parse_cmake_crubit_interop_exact_invocation(tmp_path: Path) -> None:
    """Exact flag order emitted by ``cmake/CrubitInterop.cmake`` parses cleanly."""
    argv = [
        "--rs-bindings-from-cc",
        str(tmp_path / "bin" / "rs_bindings_from_cc"),
        "--crubit-src",
        str(tmp_path / "crubit_src"),
        "--target",
        "//interop:telemetry_api",
        "--crate-name",
        "telemetry_api",
        "--rs-out",
        str(tmp_path / "out" / "src" / "lib.rs"),
        "--cc-out",
        str(tmp_path / "out" / "telemetry_api_rust_api_impl.cc"),
        "--cargo-toml-out",
        str(tmp_path / "out" / "Cargo.toml"),
        "--cxx",
        str(tmp_path / "bin" / "clang++"),
        "--cxx-standard",
        "23",
        "--clang-format",
        str(tmp_path / "bin" / "clang-format"),
        "--rustfmt",
        str(tmp_path / "bin" / "rustfmt"),
        "--public-header",
        "interop/crubit/a.hpp",
        "--public-header",
        "interop/crubit/b.hpp",
        "--include-dir",
        str(tmp_path / "inc_one"),
        "--include-dir",
        str(tmp_path / "inc_two"),
        "--system-include-dir",
        str(tmp_path / "sys_one"),
        "--system-include-dir",
        str(tmp_path / "sys_two"),
        "--define",
        "CRUBIT_ENABLED=1",
        "--define",
        "NDEBUG",
        "--extra-rs-src",
        str(tmp_path / "extra_one.rs"),
        "--extra-cpp-src",
        str(tmp_path / "extra_one.cc"),
        "--extra-clang-arg",
        "-Wall",
        "--extra-clang-arg",
        "-Wno-unused-parameter",
        "--crubit-feature",
        "supported",
        "--crubit-feature",
        "assume_lifetimes",
    ]

    opts = invoker.parse_args(argv)
    assert opts.target == "//interop:telemetry_api"
    assert opts.crate_name == "telemetry_api"
    assert opts.cxx_standard == "23"
    assert opts.public_header == ["interop/crubit/a.hpp", "interop/crubit/b.hpp"]
    assert opts.include_dir == [tmp_path / "inc_one", tmp_path / "inc_two"]
    assert opts.system_include_dir == [tmp_path / "sys_one", tmp_path / "sys_two"]
    assert opts.define == ["CRUBIT_ENABLED=1", "NDEBUG"]
    assert opts.extra_rs_src == [str(tmp_path / "extra_one.rs")]
    assert opts.extra_cpp_src == [str(tmp_path / "extra_one.cc")]
    assert opts.extra_clang_arg == ["-Wall", "-Wno-unused-parameter"]
    assert opts.crubit_feature == ["supported", "assume_lifetimes"]


def test_repeatable_flags_before_cxx_do_not_swallow_subsequent_flags(
    tmp_path: Path,
) -> None:
    """Repeating ``--include-dir`` or ``--extra-clang-arg`` never swallows ``--cxx``."""
    cxx_path = tmp_path / "toolchain" / "clang++"
    argv = [
        "--rs-bindings-from-cc",
        str(tmp_path / "rs_bindings_from_cc"),
        "--crubit-src",
        str(tmp_path / "crubit"),
        "--target",
        "//interop:demo",
        "--crate-name",
        "demo",
        "--rs-out",
        str(tmp_path / "lib.rs"),
        "--cc-out",
        str(tmp_path / "impl.cc"),
        "--include-dir",
        str(tmp_path / "a"),
        "--include-dir",
        str(tmp_path / "b"),
        "--public-header",
        "first.hpp",
        "--public-header",
        "second.hpp",
        "--extra-clang-arg",
        "--target=arm64-apple-macosx",
        "--cxx",
        str(cxx_path),
        "--cxx-standard",
        "23",
    ]

    opts = invoker.parse_args(argv)
    assert opts.include_dir == [tmp_path / "a", tmp_path / "b"]
    assert opts.public_header == ["first.hpp", "second.hpp"]
    assert opts.extra_clang_arg == ["--target=arm64-apple-macosx"]
    assert opts.cxx == cxx_path
    assert opts.cxx_standard == "23"


def test_build_rs_bindings_command_emits_expected_flags_and_target_args(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    """``build_rs_bindings_command`` serializes Crubit JSON and Clang driver flags."""
    inc_dir = tmp_path / "include"
    inc_dir.mkdir()
    hdr = inc_dir / "demo.hpp"
    hdr.write_text("#pragma once\n", encoding="utf-8")

    sys_inc = tmp_path / "sys_include"
    sys_inc.mkdir()

    clang_format = tmp_path / "bin" / "clang-format"
    clang_format.parent.mkdir(parents=True)
    clang_format.write_text("", encoding="utf-8")
    missing_rustfmt = tmp_path / "bin" / "missing-rustfmt"

    def _fake_sys_flags(_cxx: Path) -> list[str]:
        return ["-resource-dir", "/fake/res", "-isystem", "/fake/c++/v1"]

    monkeypatch.setattr(invoker, "_resolve_clang_system_flags", _fake_sys_flags)

    opts = invoker.InvokeOptions(
        rs_bindings_from_cc=tmp_path / "bin" / "rs_bindings_from_cc",
        crubit_src=tmp_path / "crubit_src",
        target="//interop:demo",
        crate_name="demo_rs",
        rs_out=tmp_path / "out" / "lib.rs",
        cc_out=tmp_path / "out" / "impl.cc",
        public_header=["demo.hpp"],
        cxx=tmp_path / "bin" / "clang++",
        include_dir=[inc_dir],
        system_include_dir=[sys_inc],
        define=["FEATURE_X=1"],
        clang_format=clang_format,
        rustfmt=missing_rustfmt,
        cxx_standard="23",
        extra_rs_src=["aux.rs"],
        extra_cpp_src=["aux.cc"],
        extra_clang_arg=["-Werror"],
        crubit_feature=[],
    )

    cmd = invoker.build_rs_bindings_command(opts)
    resolved_hdr = hdr.resolve().as_posix()
    assert cmd[0] == str(tmp_path / "bin" / "rs_bindings_from_cc")
    assert f"--public_headers={resolved_hdr}" in cmd
    assert f"--clang_format_exe_path={clang_format}" in cmd
    assert not any(arg.startswith("--rustfmt_exe_path=") for arg in cmd)
    assert "--extra_rs_srcs=aux.rs" in cmd
    assert "--extra_cpp_srcs=aux.cc" in cmd

    target_arg = next(a for a in cmd if a.startswith("--target_args="))
    payload = json.loads(target_arg.removeprefix("--target_args="))
    assert payload == [
        {
            "t": "//interop:demo",
            "h": [resolved_hdr],
            "f": ["supported", "assume_lifetimes"],
            "c": "demo_rs",
        }
    ]

    sep_idx = cmd.index("--")
    clang_args = cmd[sep_idx + 1 :]
    assert clang_args[:6] == [
        "-x",
        "c++",
        "-std=c++23",
        "-stdlib=libc++",
        "-resource-dir",
        "/fake/res",
    ]
    assert "-I" in clang_args
    assert str(inc_dir.resolve()) in clang_args
    assert "-DFEATURE_X=1" in clang_args
    assert "-Werror" in clang_args


# ---------------------------------------------------------------------------
# Tier 2: Boundary, Validation & Header Normalization
# ---------------------------------------------------------------------------


def test_missing_required_public_header_raises_system_exit(tmp_path: Path) -> None:
    """Omitting required ``--public-header`` fails CLI validation."""
    argv = _base_cli_args(tmp_path)[:-2]
    with pytest.raises(SystemExit):
        invoker.parse_args(argv)


def test_missing_value_after_repeatable_flag_raises_system_exit(
    tmp_path: Path,
) -> None:
    """Missing operand before ``--cxx`` is rejected instead of eating ``--cxx``."""
    argv = [
        "--rs-bindings-from-cc",
        str(tmp_path / "rs_bindings_from_cc"),
        "--crubit-src",
        str(tmp_path / "crubit"),
        "--target",
        "//interop:demo",
        "--crate-name",
        "demo",
        "--rs-out",
        str(tmp_path / "lib.rs"),
        "--cc-out",
        str(tmp_path / "impl.cc"),
        "--public-header",
        "demo.hpp",
        "--include-dir",
        "--cxx",
        str(tmp_path / "clang++"),
    ]
    with pytest.raises(SystemExit):
        invoker.parse_args(argv)


def test_hyphenated_defines_and_clang_args_are_accepted(tmp_path: Path) -> None:
    """Leading hyphens on ``--define`` and ``--extra-clang-arg`` pass cleanly."""
    argv = [
        *_base_cli_args(tmp_path),
        "--define",
        "-DLEADING_HYPHEN=1",
        "--extra-clang-arg",
        "-DFOO",
        "--extra-clang-arg",
        "--sysroot=/tmp/sdk",
    ]
    opts = invoker.parse_args(argv)
    assert opts.define == ["-DLEADING_HYPHEN=1"]
    assert opts.extra_clang_arg == ["-DFOO", "--sysroot=/tmp/sdk"]


def test_normalize_header_resolves_symlinks_and_include_roots(
    tmp_path: Path,
) -> None:
    """``_normalize_header`` resolves absolute, include-relative, and fallback paths."""
    inc_a = tmp_path / "inc_a"
    inc_b = tmp_path / "inc_b"
    inc_a.mkdir()
    inc_b.mkdir()
    rel_hdr = inc_b / "sub" / "api.hpp"
    rel_hdr.parent.mkdir()
    rel_hdr.write_text("#pragma once\n", encoding="utf-8")

    abs_target = tmp_path / "real.hpp"
    abs_target.write_text("#pragma once\n", encoding="utf-8")
    abs_link = tmp_path / "alias.hpp"
    abs_link.symlink_to(abs_target)

    assert (
        invoker._normalize_header(str(abs_link), []) == abs_target.resolve().as_posix()
    )
    assert (
        invoker._normalize_header("sub/api.hpp", [inc_a, inc_b])
        == rel_hdr.resolve().as_posix()
    )
    assert invoker._normalize_header("missing/hdr.hpp", [inc_a]) == "missing/hdr.hpp"


def test_resolve_clang_system_flags_prefers_conda_libcxx_and_darwin_sdk(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    """System include detection selects CONDA_PREFIX libc++ and Darwin SDKROOT."""
    conda_dir = tmp_path / "conda"
    conda_v1 = conda_dir / "include" / "c++" / "v1"
    conda_v1.mkdir(parents=True)

    toolchain_v1 = tmp_path / "tc" / "include" / "c++" / "v1"
    toolchain_v1.mkdir(parents=True)
    cxx = tmp_path / "tc" / "bin" / "clang++"
    cxx.parent.mkdir(parents=True)
    cxx.write_text("", encoding="utf-8")

    def _fake_run(
        cmd: list[str],
        *,
        check: bool,
        capture_output: bool,
        text: bool,
    ) -> SimpleNamespace:
        _ = (check, capture_output, text)
        if "-print-resource-dir" in cmd:
            return SimpleNamespace(stdout="/fake/clang/res\n")
        return SimpleNamespace(stdout="/Applications/MacOSX.sdk\n")

    def _darwin() -> str:
        return "Darwin"

    def _linux() -> str:
        return "Linux"

    monkeypatch.setattr(subprocess, "run", _fake_run)
    monkeypatch.setenv("CONDA_PREFIX", str(conda_dir))
    monkeypatch.delenv("SDKROOT", raising=False)
    monkeypatch.setattr(invoker.platform, "system", _darwin)

    darwin_flags = invoker._resolve_clang_system_flags(cxx)
    assert darwin_flags == [
        "-resource-dir",
        "/fake/clang/res",
        "-isystem",
        str(conda_v1),
        "-isysroot",
        "/Applications/MacOSX.sdk",
    ]

    monkeypatch.delenv("CONDA_PREFIX", raising=False)
    monkeypatch.setattr(invoker.platform, "system", _linux)

    linux_flags = invoker._resolve_clang_system_flags(cxx)
    assert linux_flags == [
        "-resource-dir",
        "/fake/clang/res",
        "-isystem",
        str(toolchain_v1),
    ]


def test_parse_args_help_exits_zero() -> None:
    """``parse_args(['--help'])`` exits cleanly with code ``0``."""
    with pytest.raises(SystemExit) as exc_info:
        invoker.parse_args(["--help"])
    assert exc_info.value.code == 0


# ---------------------------------------------------------------------------
# Tier 3: Subprocess Execution, Cargo.toml Emission & Error Handling
# ---------------------------------------------------------------------------


def test_main_runs_subprocess_and_writes_cargo_toml(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    """``main`` runs ``rs_bindings_from_cc`` and generates a valid ``Cargo.toml``."""
    cargo_toml = tmp_path / "crate" / "Cargo.toml"
    rs_out = tmp_path / "crate" / "src" / "lib.rs"
    cc_out = tmp_path / "crate" / "impl.cc"
    crubit_src = tmp_path / "crubit_src"
    crubit_src.mkdir()

    invoked_cmds: list[list[str]] = []

    def _fake_subprocess_run(
        cmd: list[str],
        *,
        check: bool = False,
        **_kwargs: object,
    ) -> SimpleNamespace:
        assert check is True
        invoked_cmds.append(cmd)
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    monkeypatch.setattr(subprocess, "run", _fake_subprocess_run)
    monkeypatch.setattr(invoker, "_resolve_clang_system_flags", _empty_flags)

    argv = [
        "--rs-bindings-from-cc",
        str(tmp_path / "rs_bindings_from_cc"),
        "--crubit-src",
        str(crubit_src),
        "--target",
        "//interop:fifo_cc",
        "--crate-name",
        "fifo_cc_rs",
        "--rs-out",
        str(rs_out),
        "--cc-out",
        str(cc_out),
        "--cargo-toml-out",
        str(cargo_toml),
        "--cxx",
        str(tmp_path / "clang++"),
        "--public-header",
        "fifo.hpp",
    ]
    exit_code = invoker.main(argv)

    assert exit_code == 0
    assert len(invoked_cmds) == 1
    assert rs_out.parent.is_dir()
    assert cc_out.parent.is_dir()
    assert cargo_toml.is_file()

    parsed_toml = tomllib.loads(cargo_toml.read_text(encoding="utf-8"))
    assert parsed_toml["package"]["name"] == "fifo_cc_rs"
    assert parsed_toml["package"]["edition"] == "2024"
    assert parsed_toml["lib"]["path"] == "src/lib.rs"
    assert "crubit_support" in parsed_toml["dependencies"]
    expected_support = str(crubit_src.resolve() / "support" / "crubit_support")
    assert parsed_toml["dependencies"]["crubit_support"]["path"] == expected_support


def test_main_omits_cargo_toml_when_unset(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    """Omitting ``--cargo-toml-out`` skips writing ``Cargo.toml``."""

    def _fake_ok(*_args: object, **_kwargs: object) -> SimpleNamespace:
        return SimpleNamespace(returncode=0, stdout="", stderr="")

    monkeypatch.setattr(subprocess, "run", _fake_ok)
    monkeypatch.setattr(invoker, "_resolve_clang_system_flags", _empty_flags)

    exit_code = invoker.main(_base_cli_args(tmp_path))
    assert exit_code == 0
    assert not (tmp_path / "out" / "Cargo.toml").exists()


def test_main_returns_subprocess_nonzero_exit_code(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    """Non-zero ``rs_bindings_from_cc`` exit code is returned by ``main``."""

    def _failing_run(
        cmd: list[str],
        *,
        check: bool = False,
        **_kwargs: object,
    ) -> SimpleNamespace:
        _ = check
        raise subprocess.CalledProcessError(returncode=7, cmd=cmd)

    monkeypatch.setattr(subprocess, "run", _failing_run)
    monkeypatch.setattr(invoker, "_resolve_clang_system_flags", _empty_flags)

    assert invoker.main(_base_cli_args(tmp_path)) == 7


# ---------------------------------------------------------------------------
# Tier 4: Stress Simulation
# ---------------------------------------------------------------------------


def test_stress_large_flag_fanout_and_cargo_toml_render(
    monkeypatch: pytest.MonkeyPatch,
    tmp_path: Path,
) -> None:
    """Parsing and assembling 300 repeated flags finishes quickly."""
    argv: list[str] = [
        "--rs-bindings-from-cc",
        str(tmp_path / "rs_bindings_from_cc"),
        "--crubit-src",
        str(tmp_path / "crubit_src"),
        "--target",
        "//interop:stress",
        "--crate-name",
        "stress_rs",
        "--rs-out",
        str(tmp_path / "out" / "src" / "lib.rs"),
        "--cc-out",
        str(tmp_path / "out" / "impl.cc"),
        "--cxx",
        str(tmp_path / "clang++"),
    ]
    for idx in range(100):
        argv.extend(["--include-dir", str(tmp_path / f"inc_{idx}")])
        argv.extend(["--define", f"MACRO_{idx}={idx}"])
    for idx in range(50):
        argv.extend(["--public-header", f"mod_{idx}.hpp"])
        argv.extend(["--extra-clang-arg", f"-Wcustom-{idx}"])

    monkeypatch.setattr(invoker, "_resolve_clang_system_flags", _empty_flags)
    # Ensure os.environ lookup inside helpers remains pure under stress
    assert os.fspath(tmp_path)

    start = time.perf_counter()
    opts = invoker.parse_args(argv)
    cmd = invoker.build_rs_bindings_command(opts)
    toml_text = invoker._render_cargo_toml(
        opts.crate_name,
        opts.rs_out,
        tmp_path / "out" / "Cargo.toml",
        opts.crubit_src,
    )
    elapsed = time.perf_counter() - start

    assert len(opts.include_dir) == 100
    assert len(opts.define) == 100
    assert len(opts.public_header) == 50
    assert len(opts.extra_clang_arg) == 50
    assert "-DMACRO_99=99" in cmd
    assert "[package]" in toml_text
    assert elapsed < 1.0
