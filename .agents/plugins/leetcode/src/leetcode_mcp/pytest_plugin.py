"""Pytest plugin to ensure leetcode-mcp test suite is discovered when invoked from repo root."""

from pathlib import Path
from typing import Any


def pytest_configure(config: Any) -> None:
    """Register custom markers and default settings.

    Args:
        config: Pytest configuration object.
    """
    config.addinivalue_line(
        "markers", "live: tests that make live network calls to LeetCode GraphQL API"
    )


def pytest_load_initial_conftests(early_config: Any, parser: Any, args: list[str]) -> None:
    """If pytest is invoked without explicit file paths from outside plugin dir, add tests dir.

    Args:
        early_config: Pytest early configuration object.
        parser: Pytest command line argument parser.
        args: Mutable list of command line arguments passed to pytest.
    """
    file_or_dir = getattr(early_config.known_args_namespace, "file_or_dir", None)
    if not file_or_dir:
        tests_dir = Path(__file__).resolve().parent.parent.parent / "tests"
        if tests_dir.is_dir():
            if isinstance(file_or_dir, list):
                file_or_dir.append(str(tests_dir))
            args.append(str(tests_dir))
