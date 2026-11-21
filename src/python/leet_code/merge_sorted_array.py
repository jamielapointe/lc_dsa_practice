"""LeetCode #88: Merge Sorted Array (Easy).

URL: https://leetcode.com/problems/merge-sorted-array/

You are given two integer arrays ``nums1`` and ``nums2``, sorted in **non-decreasing
order**, and two integers ``m`` and ``n``, representing the number of elements in
``nums1`` and ``nums2`` respectively.

Merge ``nums1`` and ``nums2`` into a single array sorted in **non-decreasing order**.

The final sorted array should not be returned by the function, but instead be stored
inside ``nums1``. To accommodate this, ``nums1`` has a length of ``m + n``, where the
first ``m`` elements denote the elements that should be merged, and the last ``n``
elements are set to ``0`` and should be ignored. ``nums2`` has a length of ``n``.

Examples:
    1. ``nums1 = [1, 2, 3, 0, 0, 0], m = 3, nums2 = [2, 5, 6], n = 3``
       -> ``[1, 2, 2, 3, 5, 6]``
    2. ``nums1 = [1], m = 1, nums2 = [], n = 0`` -> ``[1]``
    3. ``nums1 = [0], m = 0, nums2 = [1], n = 1`` -> ``[1]``

Constraints:
    * ``len(nums1) == m + n``
    * ``len(nums2) == n``
    * ``0 <= m, n <= 200``
    * ``1 <= m + n <= 200``
    * ``-10**9 <= nums1[i], nums2[j] <= 10**9``

Complexity: O(m + n) time, O(1) auxiliary space (in-place mutation of ``nums1``).
"""

from typing import TYPE_CHECKING

from leet_code import utility


if TYPE_CHECKING:
    from collections.abc import Sequence


def merge_sorted_array(
    nums1: list[int],
    m: int,
    nums2: Sequence[int],
    n: int,
) -> None:
    """Merges two non-decreasing integer sequences into ``nums1`` in-place.

    ``m == 0`` and ``n == 0`` with empty ``nums1`` and ``nums2`` is vacuously valid
    and acts as a no-op. Trailing buffer slots ``nums1[m : m + n]`` may hold ``0`` or
    arbitrary sentinel values; they are ignored as data and overwritten cleanly.
    ``nums2`` is never mutated and may be any ``Sequence[int]`` (such as a ``tuple``).
    Precondition checks are atomic: neither ``nums1`` nor ``nums2`` is modified if a
    ``ValueError`` is raised.

    Args:
        nums1: Mutable destination list of length ``m + n`` whose first ``m`` elements
            are sorted in non-decreasing order and whose last ``n`` slots provide
            scratch space to be overwritten.
        m: Number of initialized sorted elements in ``nums1`` (``m >= 0``).
        nums2: Read-only sequence of ``n`` integers sorted in non-decreasing order;
            never mutated.
        n: Number of elements in ``nums2`` (``n >= 0``).

    Raises:
        ValueError: If ``m < 0`` or ``n < 0``, if ``len(nums1) != m + n`` or
            ``len(nums2) != n``, or if ``nums1[:m]`` or ``nums2[:n]`` is not sorted
            in non-decreasing order. Trailing slack ``nums1[m:]`` is not checked for
            sortedness.

    Examples:
        >>> nums1 = [1, 2, 3, 0, 0, 0]
        >>> merge_sorted_array(nums1, 3, [2, 5, 6], 3)
        >>> nums1
        [1, 2, 2, 3, 5, 6]
        >>> nums1 = [1]
        >>> merge_sorted_array(nums1, 1, [], 0)
        >>> nums1
        [1]
        >>> nums1 = [0]
        >>> merge_sorted_array(nums1, 0, (1,), 1)
        >>> nums1
        [1]
    """
    total_len = n + m
    if m < 0 or n < 0 or total_len != len(nums1) or n != len(nums2):
        raise ValueError("Invalid length")
    if not (utility.is_sorted(nums1[:m]) and utility.is_sorted(nums2)):
        raise ValueError("Input is not sorted in non-decreasing order")

    # Trivial cases first
    if n == 0:
        return
    if m == 0:
        nums1[:] = nums2
        return
    if nums1[m - 1] <= nums2[0]:
        nums1[m:] = nums2
        return
    if nums1[0] >= nums2[-1]:
        nums1[n:] = nums1[:m]
        nums1[:n] = nums2
        return

    i = m
    j = n
    write_idx = total_len
    while i > 0 and j > 0:
        write_idx -= 1
        left = nums1[i - 1]
        right = nums2[j - 1]
        if left > right:
            nums1[write_idx] = left
            i -= 1
        else:
            nums1[write_idx] = right
            j -= 1
    if j > 0:
        nums1[:write_idx] = nums2[:j]


class Solution:
    """LeetCode compatibility wrapper matching the official Python 3 signature."""

    def merge(  # ruff: ignore[no-self-use] - Official LeetCode API.
        self,
        nums1: list[int],
        m: int,
        nums2: list[int],
        n: int,
    ) -> None:
        """Merges ``nums2`` into ``nums1`` in-place in non-decreasing order.

        Delegates to ``merge_sorted_array`` and propagates any ``ValueError`` without
        mutating ``nums1`` or ``nums2``.

        Args:
            nums1: Destination list of length ``m + n`` whose first ``m`` elements are
                sorted in non-decreasing order followed by ``n`` trailing slots.
            m: Number of initialized sorted elements in ``nums1``.
            nums2: Source list of ``n`` integers sorted in non-decreasing order
                (never mutated).
            n: Number of elements in ``nums2``.

        Examples:
            >>> nums1 = [1, 2, 3, 0, 0, 0]
            >>> Solution().merge(nums1, 3, [2, 5, 6], 3)
            >>> nums1
            [1, 2, 2, 3, 5, 6]
        """
        merge_sorted_array(nums1, m, nums2, n)
