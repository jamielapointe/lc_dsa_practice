# /// script
# requires-python = ">=3.15"
# dependencies = [
#   "cyclopts>=5.5.0",
#   "structlog>=26.1.0",
# ]
# ///
"""Merge LLVM raw profiles and enforce the C++ line-coverage gate.

Run after ``cmake --workflow --preset coverage``. The coverage test preset makes every
instrumented test binary write ``*.profraw`` files under ``build/coverage/profraw``.
This script merges them with ``llvm-profdata`` and then uses ``llvm-cov`` to compute
line coverage for the project sources (``src/``), excluding tests and third-party
code.

Example:
    pixi run cov-cpp
"""

import json
import logging
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Annotated, Final, cast
lazy from collections.abc import Sequence

import cyclopts
import structlog
from cyclopts import Parameter


# Paths that never count toward project coverage.
IGNORE_FILENAME_REGEX: Final[str] = r"(/test/|/\.pixi/|/_deps/|/usr/|/Applications/)"
DEFAULT_BUILD_DIR: Final[Path] = Path("build/coverage")
DEFAULT_FAIL_UNDER: Final[float] = 85.0

_app = cyclopts.App(name="cpp-coverage", help=__doc__)


def _configure_logging() -> None:
    """Configures standard library logging for coverage CLI execution."""
    logging.basicConfig(level=logging.INFO, format="%(message)s")


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


def find_tool(name: str) -> str:
    """Locates an LLVM tool on PATH (the Pixi environment provides it).

    Args:
        name: Executable name such as ``llvm-cov``.

    Returns:
        The absolute path of the executable.

    Raises:
        FileNotFoundError: If the tool is not on PATH.
    """
    path = shutil.which(name)
    if path is None:
        message = f"{name} not found on PATH; run this script through `pixi run`."
        raise FileNotFoundError(message)
    return path


def find_test_binaries(build_dir: Path) -> list[Path]:
    """Returns every instrumented test executable in ``<build_dir>/bin``.

    Args:
        build_dir: The coverage preset build directory.

    Returns:
        Sorted test executables (files whose name ends with ``_test``).
    """
    return sorted(path for path in (build_dir / "bin").glob("*_test") if path.is_file())


def merge_profiles(build_dir: Path, profdata: Path) -> None:
    """Merges all raw profiles into one indexed profile.

    Args:
        build_dir: The coverage preset build directory.
        profdata: Output path of the merged profile.

    Raises:
        FileNotFoundError: If no raw profiles were produced.
    """
    raw_profiles = sorted((build_dir / "profraw").glob("*.profraw"))
    if not raw_profiles:
        message = (
            f"No *.profraw files under {build_dir / 'profraw'}; "
            "did the coverage tests run?"
        )
        raise FileNotFoundError(message)
    subprocess.run(
        [
            find_tool("llvm-profdata"),
            "merge",
            "-sparse",
            *map(str, raw_profiles),
            "-o",
            str(profdata),
        ],
        check=True,
    )


def coverage_command(
    subcommand: str,
    binaries: list[Path],
    profdata: Path,
    *extra: str,
) -> list[str]:
    """Builds an ``llvm-cov`` command line.

    Args:
        subcommand: ``report`` or ``export``.
        binaries: Instrumented executables providing coverage mappings.
        profdata: Merged profile data.
        *extra: Additional arguments appended to the command.

    Returns:
        The full argument vector.
    """
    command = [find_tool("llvm-cov"), subcommand, str(binaries[0])]
    for binary in binaries[1:]:
        command += ["-object", str(binary)]
    command += [
        f"-instr-profile={profdata}",
        f"-ignore-filename-regex={IGNORE_FILENAME_REGEX}",
    ]
    return [*command, *extra]


def line_coverage_percent(binaries: list[Path], profdata: Path) -> float:
    """Computes total project line coverage.

    Args:
        binaries: Instrumented executables providing coverage mappings.
        profdata: Merged profile data.

    Returns:
        Line coverage as a percentage in ``[0, 100]``.
    """
    result = subprocess.run(
        coverage_command("export", binaries, profdata, "-summary-only"),
        check=True,
        capture_output=True,
        text=True,
    )
    totals = json.loads(result.stdout)["data"][0]["totals"]
    return float(totals["lines"]["percent"])


@_app.default
def _run_cli(
    *,
    build_dir: Annotated[
        Path,
        Parameter(help="Coverage preset build directory."),
    ] = DEFAULT_BUILD_DIR,
    fail_under: Annotated[
        float,
        Parameter(help="Minimum required line coverage percentage."),
    ] = DEFAULT_FAIL_UNDER,
) -> int:
    """Merges coverage profiles, prints report, and checks line coverage gate.

    Args:
        build_dir: Coverage preset build directory.
        fail_under: Minimum required line coverage percentage.

    Returns:
        ``0`` when coverage meets the threshold, ``1`` otherwise.
    """
    _configure_logging()
    logger = _get_logger()

    binaries = find_test_binaries(build_dir)
    if not binaries:
        logger.error(
            "No *_test binaries in %s",
            build_dir / "bin",
            bin_dir=str(build_dir / "bin"),
        )
        return 1

    profdata = build_dir / "merged.profdata"
    merge_profiles(build_dir, profdata)
    subprocess.run(
        coverage_command("report", binaries, profdata, "-show-region-summary=false"),
        check=True,
    )

    percent = line_coverage_percent(binaries, profdata)
    logger.info(
        "C++ line coverage: %.2f%% (threshold %.2f%%)",
        percent,
        fail_under,
        percent=round(percent, 2),
        threshold=fail_under,
    )
    if percent < fail_under:
        logger.error(
            "C++ line coverage %.2f%% is below the required %.2f%%",
            percent,
            fail_under,
            percent=round(percent, 2),
            threshold=fail_under,
        )
        return 1
    return 0


def main(argv: Sequence[str] | None = None) -> int:
    """Entry point.

    Args:
        argv: Command-line arguments (defaults to ``sys.argv[1:]``).

    Returns:
        ``0`` when coverage meets the threshold, ``1`` otherwise.
    """
    tokens = list(argv) if argv is not None else None
    result = _app(tokens, result_action="return_value")
    return result if isinstance(result, int) else 0


if __name__ == "__main__":
    sys.exit(main())
