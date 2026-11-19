"""LeetCode #228: Summary Ranges (Easy).

URL: https://leetcode.com/problems/summary-ranges/

You are given a **sorted unique** integer array ``nums``.

A **range** ``[a, b]`` is the set of all integers from ``a`` to ``b`` (inclusive).

Return the **smallest sorted** list of ranges that **cover all the numbers in the
array exactly**. That is, each element of ``nums`` is covered by exactly one of the
ranges, and there is no integer ``x`` such that ``x`` is in one of the ranges but not
in ``nums``.

Each range ``[a, b]`` in the list should be output as:

* ``"a->b"`` if ``a != b``
* ``"a"`` if ``a == b``

Constraints:
    * ``0 <= len(nums) <= 20``
    * ``-2**31 <= nums[i] <= 2**31 - 1``
    * All the values of ``nums`` are **unique**.
    * ``nums`` is sorted in ascending order.

Complexity: O(n) time, O(1) extra space beyond the output list of range strings.
"""

from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from collections.abc import Sequence


def _format_range(start: int, end: int) -> str:
    """Formats a range as a string.

    Args:
        start: The starting integer of the range.
        end: The ending integer of the range.

    Returns:
        The formatted range string.

    Examples:
        >>> format_range(0, 0)
        '0'
        >>> format_range(0, 2)
        '0->2'
    """
    if start == end:
        return str(start)
    return f"{start}->{end}"


def summary_ranges(nums: Sequence[int]) -> list[str]:
    """Formats contiguous runs of strictly ascending integers as summary ranges.

    Args:
        nums: A sequence of unique integers sorted in strictly ascending order.

    Returns:
        The minimal sorted list of formatted range strings covering every element of
        ``nums`` exactly once, using ``"a->b"`` for multi-element runs and ``"a"`` for
        singletons.

    Raises:
        ValueError: If ``nums`` contains duplicate values or is not sorted in
            strictly ascending order.

    Examples:
        >>> summary_ranges([0, 1, 2, 4, 5, 7])
        ['0->2', '4->5', '7']
        >>> summary_ranges([0, 2, 3, 4, 6, 8, 9])
        ['0', '2->4', '6', '8->9']
    """
    if not nums:
        return []
    if any(nums[i] >= nums[i + 1] for i in range(len(nums) - 1)):
        raise ValueError("nums must be sorted in strictly ascending order.")
    range_list: list[str] = []
    start_num = nums[0]
    prev_num = start_num
    for num in nums[1:]:
        if num != prev_num + 1:
            range_list.append(_format_range(start_num, prev_num))
            start_num = num
        prev_num = num
    range_list.append(_format_range(start_num, prev_num))
    return range_list


class Solution:
    """LeetCode compatibility wrapper matching the official Python 3 signature."""

    @staticmethod
    def summary_ranges(
        nums: list[int],
    ) -> list[str]:
        """Returns the smallest sorted list of ranges covering ``nums`` exactly.

        Args:
            nums: A list of unique integers sorted in strictly ascending order.

        Returns:
            The minimal sorted list of formatted range strings (``"a->b"`` or
            ``"a"``).

        Examples:
            >>> Solution().summaryRanges([0, 1, 2, 4, 5, 7])
            ['0->2', '4->5', '7']
        """
        return summary_ranges(nums)
