"""Tests for ``leet_code.two_sum`` (4-tier layout mirroring the C++ test suites)."""

from __future__ import annotations

import random
import time

import pytest

from leet_code.two_sum import Solution
from leet_code.two_sum import two_sum


# ---------------------------------------------------------------------------
# Tier 1: Canonical feature coverage (official LeetCode examples)
# ---------------------------------------------------------------------------


@pytest.mark.parametrize(
    ('nums', 'target', 'expected'),
    [
        ([2, 7, 11, 15], 9, (0, 1)),
        ([3, 2, 4], 6, (1, 2)),
        ([3, 3], 6, (0, 1)),
    ],
)
def test_canonical_examples(nums: list[int], target: int, expected: tuple[int, int]) -> None:
    """Official examples return the expected index pair."""
    assert two_sum(nums, target) == expected


def test_solution_wrapper_matches_official_signature() -> None:
    """The LeetCode wrapper returns a two-element list."""
    assert Solution().twoSum([2, 7, 11, 15], 9) == [0, 1]


# ---------------------------------------------------------------------------
# Tier 2: Boundary, corner and edge cases
# ---------------------------------------------------------------------------


def test_negative_numbers_and_zero_target() -> None:
    """Negative values and a zero target are handled."""
    assert two_sum([-3, 4, 3, 90], 0) == (0, 2)


def test_same_element_is_not_reused() -> None:
    """A single element equal to half the target must not pair with itself."""
    with pytest.raises(ValueError, match='No two sum solution found'):
        two_sum([5, 1, 2], 10)


@pytest.mark.parametrize('nums', [[], [1]])
def test_too_few_elements_raise(nums: list[int]) -> None:
    """Fewer than two elements is rejected."""
    with pytest.raises(ValueError, match='at least two elements'):
        two_sum(nums, 2)


def test_extreme_values_do_not_overflow() -> None:
    """Python integers are arbitrary precision, but the bounds of the problem must still work."""
    low, high = -(10**9), 10**9
    assert two_sum([low, 0, high], 0) == (0, 2)


def test_accepts_any_sequence() -> None:
    """Tuples and ranges work, not only lists."""
    assert two_sum((1, 2, 3), 5) == (1, 2)
    assert two_sum(range(10), 17) == (8, 9)


# ---------------------------------------------------------------------------
# Tier 3: Invariants
# ---------------------------------------------------------------------------


def test_returned_indices_are_ordered_and_sum_to_target() -> None:
    """For random solvable inputs the result is a valid, ordered, distinct pair."""
    rng = random.Random(1234)
    for _ in range(200):
        nums = [rng.randint(-1000, 1000) for _ in range(rng.randint(2, 50))]
        first, second = sorted(rng.sample(range(len(nums)), 2))
        target = nums[first] + nums[second]
        i, j = two_sum(nums, target)
        assert i < j
        assert nums[i] + nums[j] == target


def test_input_is_not_mutated() -> None:
    """The algorithm never mutates its input."""
    nums = [2, 7, 11, 15]
    snapshot = list(nums)
    two_sum(nums, 9)
    assert nums == snapshot


# ---------------------------------------------------------------------------
# Tier 4: Stress workloads and online-judge simulation
# ---------------------------------------------------------------------------


@pytest.mark.stress
def test_maximum_constraint_input_runs_in_linear_time() -> None:
    """A 10^4-element worst case (answer at the very end) finishes well under the time limit."""
    size = 10_000
    nums = list(range(size))
    target = (size - 1) + (size - 2)
    start = time.perf_counter()
    result = two_sum(nums, target)
    elapsed = time.perf_counter() - start
    assert result == (size - 2, size - 1)
    assert elapsed < 0.2
