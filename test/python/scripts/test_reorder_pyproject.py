"""Tests for reorder_pyproject.py."""

from __future__ import annotations

from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from pathlib import Path

import pytest

from scripts.ci.reorder_pyproject import dump_toml_dict
from scripts.ci.reorder_pyproject import get_key_str
from scripts.ci.reorder_pyproject import has_scalars
from scripts.ci.reorder_pyproject import is_bare_key
from scripts.ci.reorder_pyproject import toml_dump_value


def test_toml_dump_value() -> None:
    """Test serialization of python objects to TOML."""
    assert toml_dump_value(True) == 'true'
    assert toml_dump_value(False) == 'false'
    assert toml_dump_value(42) == '42'
    assert toml_dump_value(3.14) == '3.14'
    assert toml_dump_value('hello') == '"hello"'
    assert toml_dump_value([]) == '[]'
    assert toml_dump_value(['a', 'b']) == '[\n    "a",\n    "b",\n]'

    with pytest.raises(ValueError):
        toml_dump_value({})


def test_is_bare_key() -> None:
    """Test TOML bare key validation."""
    assert is_bare_key('valid-key') is True
    assert is_bare_key('valid_key_1') is True
    assert is_bare_key('invalid key') is False
    assert is_bare_key('invalid.key') is False
    assert is_bare_key('') is False


def test_get_key_str() -> None:
    """Test getting formatted TOML keys."""
    assert get_key_str('valid-key') == 'valid-key'
    assert get_key_str('invalid key') == '"invalid key"'


def test_has_scalars() -> None:
    """Test has_scalars function."""
    assert has_scalars({'a': 1}) is True
    assert has_scalars({'a': {'b': 1}}) is False
    assert has_scalars({'a': [1, 2]}) is True
    assert has_scalars({'a': [{'b': 1}]}) is False


def test_dump_toml_dict() -> None:
    """Test dumping a dictionary to TOML format lines."""
    d = {'a': 1, 'b': 'str', 'c': {'d': 2}}
    out: list[str] = []
    dump_toml_dict(d, out)
    assert out == ['a = 1', 'b = "str"', '\n[c]', 'd = 2']


def test_setup_logging() -> None:
    """Test setup_logging configuration."""
    import logging

    from scripts.ci.reorder_pyproject import setup_logging

    # Reset root logger handlers for testing
    root = logging.getLogger()
    old_handlers = root.handlers.copy()
    root.handlers.clear()

    try:
        setup_logging(verbose=True)
        assert root.level == logging.DEBUG
        assert len(root.handlers) == 1

        root.handlers.clear()
        setup_logging(quiet=True)
        assert root.level == logging.WARNING

        root.handlers.clear()
        setup_logging()
        assert root.level == logging.INFO
    finally:
        root.handlers = old_handlers


def test_main(tmp_path: Path) -> None:
    """Test the main CLI entrypoint."""
    from scripts.ci.reorder_pyproject import main

    toml_file = tmp_path / 'pyproject.toml'
    unformatted = '[build-system]\nrequires = []\n\n[project]\nname = "test"\n'
    formatted = '[project]\nname = "test"\n\n[build-system]\nrequires = []\n'

    toml_file.write_text(unformatted)

    # Test formatting
    assert main([str(toml_file)]) == 1
    assert toml_file.read_text() == formatted

    # Test check mode on formatted file
    assert main(['--check', str(toml_file)]) == 0

    # Test check mode on unformatted file
    toml_file.write_text(unformatted)
    assert main(['--check', str(toml_file)]) == 1

    # Test unknown root key is preserved
    unknown_key_content = '[project]\nname = "test"\n\n[unknown]\nkey = "value"\n'
    toml_file.write_text(unknown_key_content)
    assert main([str(toml_file)]) == 0
    assert toml_file.read_text() == unknown_key_content
