# /// script
# requires-python = ">=3.15"
# dependencies = [
#   "cyclopts>=5.5.0",
#   "structlog>=26.1.0",
# ]
# ///
"""Wrapper CLI to invoke Crubit's ``rs_bindings_from_cc`` from CMake.

Resolves Pixi Clang resource headers, ``libc++`` include paths, and macOS SDK
sysroot flags, runs ``rs_bindings_from_cc`` to generate ``lib.rs`` and
``*_rust_api_impl.cc``, and optionally emits a ``Cargo.toml`` for the generated
Rust bindings crate.
"""

import json
import logging
import os
import platform
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, Final, cast
lazy from collections.abc import Sequence

import cyclopts
import structlog
from cyclopts import Parameter


DEFAULT_CRUBIT_FEATURES: Final[tuple[str, ...]] = ("supported", "assume_lifetimes")

_app = cyclopts.App(name="invoke-rs-bindings-from-cc", help=__doc__)


def _configure_logging() -> None:
    """Configures standard library logging for Crubit generator CLI execution."""
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")


def _get_logger() -> structlog.stdlib.BoundLogger:
    """Returns an isolated stdlib-backed structlog logger for this module."""
    logger = structlog.wrap_logger(
        logging.getLogger(__name__),
        processors=[
            structlog.stdlib.filter_by_level,
            structlog.stdlib.PositionalArgumentsFormatter(),
            structlog.processors.StackInfoRenderer(),
            structlog.processors.format_exc_info,
            structlog.dev.ConsoleRenderer(colors=False),
        ],
        wrapper_class=structlog.stdlib.BoundLogger,
    )
    return cast("structlog.stdlib.BoundLogger", logger)


@Parameter(name="*")
@dataclass(frozen=True, slots=True, kw_only=True)
class InvokeOptions:
    """Parsed CLI options for ``invoke_rs_bindings_from_cc``."""

    rs_bindings_from_cc: Annotated[
        Path,
        Parameter(help="Path to the compiled rs_bindings_from_cc binary."),
    ]
    crubit_src: Annotated[
        Path,
        Parameter(help="Root of the pinned Crubit source checkout."),
    ]
    target: Annotated[
        str,
        Parameter(help="Bazel-style target label for Crubit metadata."),
    ]
    crate_name: Annotated[
        str,
        Parameter(help="Generated Rust crate name."),
    ]
    rs_out: Annotated[
        Path,
        Parameter(help="Destination path for generated Rust bindings (lib.rs)."),
    ]
    cc_out: Annotated[
        Path,
        Parameter(help="Destination path for generated C++ thunk implementation."),
    ]
    public_header: Annotated[
        list[str],
        Parameter(
            consume_multiple=False,
            negative="",
            help="Public C++ header path (repeatable).",
        ),
    ]
    cxx: Annotated[
        Path,
        Parameter(help="Path to the clang++ compiler driver."),
    ]
    cargo_toml_out: Annotated[
        Path | None,
        Parameter(help="Optional destination Cargo.toml for the generated crate."),
    ] = None
    include_dir: Annotated[
        list[Path],
        Parameter(
            consume_multiple=False,
            negative="",
            help="User include directory (-I, repeatable).",
        ),
    ] = field(default_factory=list)
    system_include_dir: Annotated[
        list[Path],
        Parameter(
            consume_multiple=False,
            negative="",
            help="System include directory (-isystem, repeatable).",
        ),
    ] = field(default_factory=list)
    define: Annotated[
        list[str],
        Parameter(
            consume_multiple=False,
            negative="",
            allow_leading_hyphen=True,
            help="Preprocessor macro definition (-D, repeatable).",
        ),
    ] = field(default_factory=list)
    clang_format: Annotated[
        Path | None,
        Parameter(help="Optional path to clang-format executable."),
    ] = None
    rustfmt: Annotated[
        Path | None,
        Parameter(help="Optional path to rustfmt executable."),
    ] = None
    cxx_standard: Annotated[
        str,
        Parameter(help="C++ standard version passed to Clang (-std=c++<ver>)."),
    ] = "23"
    extra_rs_src: Annotated[
        list[str],
        Parameter(
            consume_multiple=False,
            negative="",
            help="Additional Rust source file to include (repeatable).",
        ),
    ] = field(default_factory=list)
    extra_cpp_src: Annotated[
        list[str],
        Parameter(
            consume_multiple=False,
            negative="",
            help="Additional C++ source file to include (repeatable).",
        ),
    ] = field(default_factory=list)
    extra_clang_arg: Annotated[
        list[str],
        Parameter(
            consume_multiple=False,
            negative="",
            allow_leading_hyphen=True,
            help="Extra Clang driver argument appended after '--' (repeatable).",
        ),
    ] = field(default_factory=list)
    crubit_feature: Annotated[
        list[str],
        Parameter(
            consume_multiple=False,
            negative="",
            help="Crubit feature flag enabled for the target (repeatable).",
        ),
    ] = field(default_factory=list)


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

    resource_dir = _query_command_output([str(cxx), "-print-resource-dir"])
    if resource_dir:
        flags.extend(["-resource-dir", resource_dir])

    # Prefer CONDA_PREFIX libc++ headers so LibTooling sees the exact same STL as Pixi builds.
    libcxx_candidates: list[Path] = []
    conda_prefix = os.environ.get("CONDA_PREFIX")
    if conda_prefix:
        libcxx_candidates.append(Path(conda_prefix) / "include" / "c++" / "v1")
    libcxx_candidates.append(cxx.resolve().parent.parent / "include" / "c++" / "v1")
    for candidate in libcxx_candidates:
        if candidate.is_dir():
            flags.extend(["-isystem", str(candidate)])
            break

    if platform.system() == "Darwin":
        sdkroot = os.environ.get("SDKROOT") or _query_command_output([
            "xcrun",
            "--show-sdk-path",
        ])
        if sdkroot:
            flags.extend(["-isysroot", sdkroot])

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


def _render_cargo_toml(
    crate_name: str,
    rs_out: Path,
    cargo_toml_out: Path,
    crubit_src: Path,
) -> str:
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
        lib_path = os.path.relpath(
            rs_out.resolve(),
            cargo_toml_out.resolve().parent,
        ).replace("\\", "/")
    except ValueError:
        lib_path = str(rs_out.resolve()).replace("\\", "/")

    src_root = crubit_src.resolve()
    deps = {
        "crubit_support": src_root / "support" / "crubit_support",
        "crubit_bridge_rust": src_root / "support" / "crubit_bridge_rust",
        "crubit_annotate": src_root / "support" / "crubit_annotate",
        "ffi_11": src_root / "support" / "ffi_11",
        "ctor": src_root / "cargo" / "support" / "ctor",
        "cref": src_root / "cargo" / "support" / "cref",
        "forward_declare": src_root / "cargo" / "support" / "forward_declare",
        "oops": src_root / "cargo" / "support" / "oops",
        "operator": src_root / "cargo" / "support" / "operator",
        "dyn_callable_rs": src_root
        / "cargo"
        / "support"
        / "rs_std"
        / "dyn_callable_rs",
        "lossy_formatter": src_root
        / "cargo"
        / "support"
        / "rs_std"
        / "lossy_formatter",
        "inline_cpp_macro": (
            src_root
            / "cargo"
            / "support"
            / "extract_cpp_from_rust"
            / "inline_cpp_macro"
        ),
    }
    dep_lines = [
        f"{name} = {{ path = {json.dumps(str(path))} }}" for name, path in deps.items()
    ]
    dep_lines.extend(('cxx = "1"', 'static_assertions = "1"'))

    lines = [
        "# Auto-generated by scripts/crubit/invoke_rs_bindings_from_cc.py. Do not edit.",
        "[package]",
        f"name = {json.dumps(crate_name)}",
        'version = "0.1.0"',
        'edition = "2024"',
        'rust-version = "1.99.1"',
        "publish = false",
        "",
        "[workspace]",
        'resolver = "3"',
        "",
        "[lib]",
        f"path = {json.dumps(lib_path)}",
        "",
        "[dependencies]",
        *dep_lines,
        "",
    ]
    return "\n".join(lines)


def build_rs_bindings_command(options: InvokeOptions) -> list[str]:
    """Constructs the ``rs_bindings_from_cc`` CLI argument vector.

    Args:
        options: Parsed invocation options.

    Returns:
        Full subprocess command list including Clang flags after ``--``.
    """
    all_include_dirs: list[Path] = [*options.include_dir, *options.system_include_dir]
    normalized_headers = [
        _normalize_header(hdr, all_include_dirs) for hdr in options.public_header
    ]
    crubit_features = options.crubit_feature or list(DEFAULT_CRUBIT_FEATURES)
    target_args = json.dumps(
        [
            {
                "t": options.target,
                "h": normalized_headers,
                "f": crubit_features,
                "c": options.crate_name,
            }
        ],
        separators=(",", ":"),
    )

    cmd: list[str] = [
        str(options.rs_bindings_from_cc),
        f"--target={options.target}",
        f"--rs_out={options.rs_out}",
        f"--cc_out={options.cc_out}",
        "--crubit_support_path_format=<support/{header}>",
        f"--public_headers={','.join(normalized_headers)}",
        f"--target_args={target_args}",
        f"--driver_path={options.cxx}",
    ]
    if options.clang_format is not None and options.clang_format.is_file():
        cmd.append(f"--clang_format_exe_path={options.clang_format}")
    if options.rustfmt is not None and options.rustfmt.is_file():
        cmd.append(f"--rustfmt_exe_path={options.rustfmt}")
    if options.extra_rs_src:
        cmd.append(f"--extra_rs_srcs={','.join(options.extra_rs_src)}")
    if options.extra_cpp_src:
        cmd.append(f"--extra_cpp_srcs={','.join(options.extra_cpp_src)}")

    clang_args: list[str] = [
        "-x",
        "c++",
        f"-std=c++{options.cxx_standard}",
        "-stdlib=libc++",
        *_resolve_clang_system_flags(options.cxx),
        "-isystem",
        str(options.crubit_src.resolve()),
    ]
    for inc_dir in options.include_dir:
        clang_args.extend(["-I", str(inc_dir.resolve())])
    for sys_inc_dir in options.system_include_dir:
        clang_args.extend(["-isystem", str(sys_inc_dir.resolve())])
    clang_args.extend(f"-D{define}" for define in options.define)
    clang_args.extend(options.extra_clang_arg)

    return [*cmd, "--", *clang_args]


def parse_args(argv: Sequence[str] | None = None) -> InvokeOptions:
    """Parses CLI arguments into an :class:`InvokeOptions` instance.

    Args:
        argv: Optional argument list overriding ``sys.argv[1:]``.

    Returns:
        Validated :class:`InvokeOptions` dataclass.

    Raises:
        SystemExit: If help is requested or arguments fail validation.
    """
    tokens = list(argv) if argv is not None else None
    command, bound, _ = _app.parse_args(tokens, exit_on_error=True)
    if command is not _run_cli:
        command(*bound.args, **bound.kwargs)
        raise SystemExit(0)
    return cast("InvokeOptions", bound.arguments["options"])


@_app.default
def _run_cli(options: InvokeOptions) -> int:
    """Executes ``rs_bindings_from_cc`` and optionally writes ``Cargo.toml``.

    Args:
        options: Parsed invocation options.

    Returns:
        ``0`` on success, non-zero on failure.
    """
    _configure_logging()
    logger = _get_logger()

    options.rs_out.parent.mkdir(parents=True, exist_ok=True)
    options.cc_out.parent.mkdir(parents=True, exist_ok=True)
    cmd = build_rs_bindings_command(options)

    logger.debug("Invoking rs_bindings_from_cc", command=" ".join(cmd))
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        logger.error(
            "rs_bindings_from_cc failed with exit code %s",
            exc.returncode,
            returncode=exc.returncode,
        )
        return exc.returncode or 1

    if options.cargo_toml_out is not None:
        options.cargo_toml_out.parent.mkdir(parents=True, exist_ok=True)
        cargo_toml_text = _render_cargo_toml(
            options.crate_name,
            options.rs_out,
            options.cargo_toml_out,
            options.crubit_src,
        )
        options.cargo_toml_out.write_text(cargo_toml_text, encoding="utf-8")

    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point for invoking ``rs_bindings_from_cc``.

    Args:
        argv: Optional command-line arguments.

    Returns:
        ``0`` on success, non-zero on failure.
    """
    tokens = list(argv) if argv is not None else None
    result = _app(tokens, result_action="return_value")
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    sys.exit(main())
