"""LeetCode #703: Kth Largest Element in a Stream (Easy).

URL: https://leetcode.com/problems/kth-largest-element-in-a-stream/

Implement a class which, for a given integer ``k``, maintains a stream of test scores
and continuously returns the ``k``th highest score in the sorted list of all scores
after each new score is submitted:

* ``KthLargest(k, nums)`` initializes the object with the integer ``k`` and the initial
  stream of integers ``nums``.
* ``add(val)`` appends ``val`` to the stream and returns the ``k``th largest element in
  the pool of elements seen so far.

Examples:
    1. ``["KthLargest", "add", "add", "add", "add", "add"]`` with
       ``[[3, [4, 5, 8, 2]], [3], [5], [10], [9], [4]]``
       -> ``[None, 4, 5, 5, 8, 8]``
    2. ``["KthLargest", "add", "add", "add", "add"]`` with
       ``[[4, [7, 7, 7, 7, 8, 3]], [2], [10], [9], [9]]``
       -> ``[None, 7, 7, 7, 8]``

Constraints:
    * ``0 <= len(nums) <= 10**4``
    * ``1 <= k <= len(nums) + 1``
    * ``-10**4 <= nums[i] <= 10**4``
    * ``-10**4 <= val <= 10**4``
    * At most ``10**4`` calls will be made to ``add``.

Complexity: O(n log k) (or O(n + (n - k) log k)) initialization time, O(log k) time per
``add`` call, and O(k) auxiliary space using a bounded min-heap of size at most ``k``.
"""

import heapq
from typing import TYPE_CHECKING


if TYPE_CHECKING:
    from collections.abc import Sequence


class KthLargestStream:
    """Maintains the ``k``th largest element dynamically over an integer stream.

    Retains at most ``k`` elements at any point. Precondition validation follows strict
    cross-language order:

    1. ``k < 1`` raises ``ValueError`` first (even when ``nums`` is empty).
    2. ``len(nums) + 1 < k`` raises ``ValueError`` second.
    3. Starved initialization ``len(nums) == k - 1`` (for example ``k = 1, nums = []``
       or ``k = 3, nums = [10, 20]``) is valid because the problem guarantees at least
       ``k`` total elements exist by the first ``add`` call.
    """

    def __init__(
        self,
        k: int,
        nums: Sequence[int],
    ) -> None:
        """Initializes the bounded stream tracker with rank ``k`` and initial ``nums``.

        ``nums`` may be any read-only ``Sequence[int]`` and is never mutated.

        Args:
            k: 1-based order-statistic rank from largest (must satisfy ``k >= 1``).
            nums: Initial stream of integers containing at least ``k - 1`` elements;
                never mutated.

        Raises:
            ValueError: If ``k < 1`` (checked first, even if ``nums`` is empty) or if
                ``len(nums) + 1 < k``.
        """
        if k < 1:
            raise ValueError("k must be greater than or equal to 1.")
        if len(nums) + 1 < k:
            raise ValueError("nums must contain at least k - 1 elements.")
        self._k: int = k
        self._min_heap: list[int] = sorted(nums)[-k:]

    @property
    def k(self) -> int:
        """The 1-based order-statistic rank ``k`` tracked by the stream."""
        return self._k

    def __len__(self) -> int:
        """Returns the number of retained elements (at most ``k``)."""
        return len(self._min_heap)

    def add(
        self,
        val: int,
    ) -> int:
        """Inserts ``val`` into the stream and returns the current ``k``th largest value.

        Args:
            val: New integer score arriving on the stream.

        Returns:
            The ``k``th largest integer among all initial and added elements so far.
        """
        heapq.heappush(self._min_heap, val)
        if len(self._min_heap) > self._k:
            heapq.heappop(self._min_heap)
        return self.peek()

    def peek(self) -> int:
        """Returns the ``k``th largest element without removing it."""
        return self._min_heap[0]


class KthLargest:
    """LeetCode compatibility wrapper matching the official Python 3 signature."""

    def __init__(self, k: int, nums: list[int]) -> None:
        """Initializes ``KthLargest`` by delegating to ``KthLargestStream``.

        Propagates any ``ValueError`` from ``KthLargestStream`` without mutating
        ``nums``.

        Args:
            k: 1-based rank from largest (``k >= 1``).
            nums: Initial list of integers with ``len(nums) >= k - 1`` (never mutated).
        """
        self._stream = KthLargestStream(k, nums)

    def add(self, val: int) -> int:
        """Adds ``val`` to the stream and returns the ``k``th largest element so far.

        Args:
            val: New integer score arriving on the stream.

        Returns:
            The ``k``th largest integer in the pool so far.
        """
        return self._stream.add(val)
