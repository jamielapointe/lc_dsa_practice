"""LeetCode #1: Two Sum (Easy).

URL: https://leetcode.com/problems/two-sum/

Given an array of integers ``nums`` and an integer ``target``, return the indices of the
two numbers such that they add up to ``target``. Each input has exactly one solution
and the same element may not be used twice.

Constraints:
    * ``2 <= len(nums) <= 10**4``
    * ``-10**9 <= nums[i] <= 10**9``
    * ``-10**9 <= target <= 10**9``

Complexity: O(n) average time, O(n) space (single pass with a value -> index hash
map).
"""

from typing import TYPE_CHECKING, Final


if TYPE_CHECKING:
    from collections.abc import Sequence


type IndexPair = tuple[int, int]

_MIN_INPUT_LENGTH: Final[int] = 2


def two_sum(nums: Sequence[int], target: int) -> IndexPair:
    """Finds two distinct indices whose elements sum to ``target``.

    Args:
        nums: The input integers.
        target: The desired sum.

    Returns:
        A pair ``(i, j)`` of 0-based indices with ``i < j`` and
        ``nums[i] + nums[j] == target``.

    Raises:
        ValueError: If ``nums`` has fewer than two elements or no pair sums
            to ``target``.

    Examples:
        >>> two_sum([2, 7, 11, 15], 9)
        (0, 1)
        >>> two_sum([3, 3], 6)
        (0, 1)
    """
    if len(nums) < _MIN_INPUT_LENGTH:
        message = "Input sequence must contain at least two elements"
        raise ValueError(message)

    seen: dict[int, int] = {}
    for index, value in enumerate(nums):
        complement_index = seen.get(target - value)
        if complement_index is not None:
            return complement_index, index
        seen.setdefault(value, index)

    message = "No two sum solution found"
    raise ValueError(message)


class Solution:
    """LeetCode compatibility wrapper matching the official Python 3 signature."""

    def twoSum(  # ruff: ignore[invalid-function-name, no-self-use] - LeetCode API.
        self,
        nums: list[int],
        target: int,
    ) -> list[int]:
        """Returns the two indices whose values sum to ``target``.

        Args:
            nums: The input integers.
            target: The desired sum.

        Returns:
            A list containing exactly two 0-based indices ``[i, j]``. Invalid
            input propagates the ``ValueError`` raised by ``two_sum``.

        Examples:
            >>> Solution().twoSum([3, 2, 4], 6)
            [1, 2]
        """
        first, second = two_sum(nums, target)
        return [first, second]
