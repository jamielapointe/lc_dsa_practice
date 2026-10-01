"""Deterministic formatter and sorter for pyproject.toml."""

import argparse
import json
import logging
import string
import sys
import tomllib
from typing import Any


logger = logging.getLogger(__name__)


def toml_dump_value(v: object, indent: int = 0) -> str:
    """Serialize a Python value into formatted TOML syntax.

    Args:
        v: The Python object to serialize (bool, number, str, or list).
        indent: The indentation level for multi-line collections.

    Returns:
        A TOML-formatted string representing the value.

    Raises:
        ValueError: If the type of v is unsupported.
    """
    if isinstance(v, bool):
        return 'true' if v else 'false'
    elif isinstance(v, (int, float)):
        return str(v)
    elif isinstance(v, str):
        return json.dumps(v)
    elif isinstance(v, list):
        if not v:
            return '[]'
        ind = '    ' * indent
        inner_ind = '    ' * (indent + 1)
        items = ',\n'.join(f'{inner_ind}{toml_dump_value(i, indent + 1)}' for i in sorted(set(v)))
        return f'[\n{items},\n{ind}]'
    else:
        raise ValueError(f'Unsupported type {type(v)}')


def is_bare_key(k: str) -> bool:
    """Check whether a TOML key requires quotes.

    Args:
        k: The key string to test.

    Returns:
        True if the key consists only of bare-key characters, False otherwise.
    """
    if not k:
        return False
    valid = set(string.ascii_letters + string.digits + '-_')
    return all(c in valid for c in k)


def get_key_str(k: str) -> str:
    """Return a bare or JSON-quoted string representation of a TOML key.

    Args:
        k: The key string.

    Returns:
        The bare key if valid, or a quoted string otherwise.
    """
    return k if is_bare_key(k) else json.dumps(k)


def has_scalars(d: dict[str, Any]) -> bool:
    """Check whether a dictionary contains any scalar values or simple lists.

    Args:
        d: The dictionary to inspect.

    Returns:
        True if at least one value is not a table or array of tables.
    """
    return any(
        not isinstance(v, (dict, list)) or (isinstance(v, list) and (not v or not isinstance(v[0], dict)))
        for v in d.values()
    )


def dump_toml_dict(d: dict[str, Any], out: list[str], prefix: str = '') -> None:
    """Recursively dump a dictionary as formatted TOML lines.

    Args:
        d: The dictionary of TOML data to dump.
        out: The output list of string lines being accumulated.
        prefix: The dot-separated table prefix for nested sections.
    """
    # Print scalars and simple arrays first
    for k in sorted(d.keys()):
        v = d[k]
        if isinstance(v, dict):
            continue
        if isinstance(v, list) and v and isinstance(v[0], dict):
            continue
        out.append(f'{get_key_str(k)} = {toml_dump_value(v)}')

    # Print nested dicts and array of dicts
    for k in sorted(d.keys()):
        v = d[k]
        if isinstance(v, dict):
            if has_scalars(v):
                out.append(f'\n[{prefix}{get_key_str(k)}]')
            dump_toml_dict(v, out, prefix + get_key_str(k) + '.')
        elif isinstance(v, list) and v and isinstance(v[0], dict):
            for item in sorted(v, key=lambda x: str(x)):
                out.append(f'\n[[{prefix}{get_key_str(k)}]]')
                dump_toml_dict(item, out, prefix + get_key_str(k) + '.')


def setup_logging(verbose: bool = False, quiet: bool = False) -> None:
    """Configure logging for the CLI application.

    Args:
        verbose: Whether to enable verbose (DEBUG) logging.
        quiet: Whether to suppress informational messages (WARNING and above only).
    """
    if quiet:
        level = logging.WARNING
    elif verbose:
        level = logging.DEBUG
    else:
        level = logging.INFO

    root = logging.getLogger()
    root.setLevel(level)

    if not root.handlers:
        handler = logging.StreamHandler()
        handler.setFormatter(logging.Formatter('%(levelname)s: %(message)s'))
        root.addHandler(handler)


def main(argv: list[str] | None = None) -> int:
    """CLI entrypoint to check or format pyproject.toml files.

    Args:
        argv: Optional list of command-line arguments. Defaults to sys.argv[1:].

    Returns:
        0 if files are already sorted and formatted correctly, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description='Sort pyproject.toml in a predictable, diff-friendly way.')
    parser.add_argument('filenames', nargs='+', help='Paths to the pyproject.toml files')
    parser.add_argument(
        '--check', action='store_true', help='Check if formatting is correct without modifying the file'
    )
    parser.add_argument('-v', '--verbose', action='store_true', help='Enable verbose/debug output')
    parser.add_argument('-q', '--quiet', action='store_true', help='Suppress informational messages')
    args = parser.parse_args(argv)

    setup_logging(verbose=args.verbose, quiet=args.quiet)

    exit_code = 0

    for file_path in args.filenames:
        try:
            with open(file_path, 'rb') as f:
                original_content = f.read().decode('utf-8')
                f.seek(0)
                data = tomllib.load(f)
        except Exception as e:
            logger.error('Error reading %s: %s', file_path, e, exc_info=args.verbose)
            exit_code = 1
            continue

        out: list[str] = []

        order = ['project', 'dependency-groups', 'build-system', 'tool']

        for k in order:
            if k in data:
                if k == 'tool':
                    continue
                if k != order[0]:
                    out.append('')
                if isinstance(data[k], dict):
                    if has_scalars(data[k]):
                        out.append(f'[{get_key_str(k)}]')
                    dump_toml_dict(data[k], out, get_key_str(k) + '.')
                else:
                    out.append(f'{get_key_str(k)} = {toml_dump_value(data[k])}')

        if 'tool' in data:
            tool_order = ['uv', 'hatch', 'ruff', 'mypy', 'pytest', 'coverage']
            for tk in tool_order:
                if tk in data['tool']:
                    if has_scalars(data['tool'][tk]):
                        out.append(f'\n[tool.{get_key_str(tk)}]')
                    dump_toml_dict(data['tool'][tk], out, f'tool.{get_key_str(tk)}.')

            for tk in sorted(data['tool'].keys()):
                if tk not in tool_order:
                    if has_scalars(data['tool'][tk]):
                        out.append(f'\n[tool.{get_key_str(tk)}]')
                    dump_toml_dict(data['tool'][tk], out, f'tool.{get_key_str(tk)}.')

        for k in sorted(data.keys()):
            if k not in order:
                out.append('')
                if isinstance(data[k], dict):
                    if has_scalars(data[k]):
                        out.append(f'[{get_key_str(k)}]')
                    dump_toml_dict(data[k], out, get_key_str(k) + '.')
                else:
                    out.append(f'{get_key_str(k)} = {toml_dump_value(data[k])}')

        new_content = '\n'.join(out) + '\n'

        if original_content != new_content:
            if args.check:
                logger.error('%s is not sorted correctly. Run without --check to fix.', file_path)
                exit_code = 1
            else:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                logger.info('Reordered and sorted %s', file_path)
                exit_code = 1
        else:
            logger.debug('%s is already sorted and formatted correctly.', file_path)

    return exit_code


if __name__ == '__main__':
    sys.exit(main())
