"""Shared utility helpers for Python LeetCode modules.

Provides small, reusable sequence and algorithm helpers shared across solutions
in ``leet_code``.
"""

from itertools import pairwise
from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from collections.abc import Sequence


def is_sorted(values: Sequence[int]) -> bool:
    """Checks whether ``values`` is sorted in non-decreasing order.

    Iterates lazily over adjacent pairs in ``O(len(values))`` time and ``O(1)``
    auxiliary space, short-circuiting on the first out-of-order pair. Sequences
    with fewer than two elements are vacuously sorted.

    Args:
        values: Read-only integer sequence to inspect.

    Returns:
        ``True`` if every adjacent pair ``(x, y)`` in ``values`` satisfies
        ``x <= y``, ``False`` otherwise.

    Examples:
        >>> is_sorted([1, 2, 2, 3])
        True
        >>> is_sorted([1, 3, 2])
        False
        >>> is_sorted([42])
        True
        >>> is_sorted([])
        True
    """
    return all(x <= y for x, y in pairwise(values))
