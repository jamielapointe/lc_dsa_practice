"""Tests for ``leet_code.summary_ranges`` (4-tier layout mirroring the C++ suites)."""

import itertools
import random
import time
from typing import TYPE_CHECKING

import pytest
from hypothesis import given
from hypothesis import strategies as st

from leet_code.summary_ranges import Solution, summary_ranges


if TYPE_CHECKING:
    from collections.abc import Sequence


_INT32_MIN = -(2**31)
_INT32_MAX = 2**31 - 1
_INVALID_ORDER_PATTERN = r"(?i)sorted|ascending|unique|duplicate"


def _parse_ranges(ranges: Sequence[str]) -> list[tuple[int, int]]:
    """Parses formatted range strings into ``(start, end)`` integer bounds.

    Args:
        ranges: Formatted summary range strings produced by ``summary_ranges``.

    Returns:
        List of ``(start, end)`` integer pairs corresponding to each formatted token.
    """
    parsed: list[tuple[int, int]] = []
    for token in ranges:
        if "->" in token:
            start_text, end_text = token.split("->", maxsplit=1)
            start = int(start_text)
            end = int(end_text)
            assert str(start) == start_text
            assert str(end) == end_text
            assert start < end
            parsed.append((start, end))
        else:
            value = int(token)
            assert str(value) == token
            parsed.append((value, value))
    return parsed


# ---------------------------------------------------------------------------
# Tier 1: Canonical feature coverage (official LeetCode examples)
# ---------------------------------------------------------------------------


@pytest.mark.unit
@pytest.mark.parametrize(
    ("nums", "expected"),
    [
        ([0, 1, 2, 4, 5, 7], ["0->2", "4->5", "7"]),
        ([0, 2, 3, 4, 6, 8, 9], ["0", "2->4", "6", "8->9"]),
    ],
)
def test_canonical_examples(nums: list[int], expected: list[str]) -> None:
    """Official examples return the expected summary range strings."""
    assert summary_ranges(nums) == expected


@pytest.mark.unit
@pytest.mark.parametrize(
    ("nums", "expected"),
    [
        ([0, 1, 2, 4, 5, 7], ["0->2", "4->5", "7"]),
        ([0, 2, 3, 4, 6, 8, 9], ["0", "2->4", "6", "8->9"]),
    ],
)
def test_solution_wrapper_canonical_examples(
    nums: list[int], expected: list[str]
) -> None:
    """The LeetCode wrapper returns the expected summary ranges for official cases."""
    assert Solution().summary_ranges(nums) == expected


# ---------------------------------------------------------------------------
# Tier 2: Boundary, corner and edge cases
# ---------------------------------------------------------------------------


@pytest.mark.unit
def test_empty_input_returns_empty_ranges() -> None:
    """An empty sequence produces an empty range list on both interfaces."""
    assert summary_ranges([]) == []
    assert Solution().summary_ranges([]) == []


@pytest.mark.unit
@pytest.mark.parametrize(
    ("nums", "expected"),
    [
        ([0], ["0"]),
        ([-42], ["-42"]),
        ([_INT32_MIN], [str(_INT32_MIN)]),
        ([_INT32_MAX], [str(_INT32_MAX)]),
    ],
)
def test_singleton_inputs(nums: list[int], expected: list[str]) -> None:
    """Single-element inputs format as a bare integer string without an arrow."""
    assert summary_ranges(nums) == expected
    assert Solution().summary_ranges(nums) == expected


@pytest.mark.unit
def test_contiguous_full_span_merges_into_single_range() -> None:
    """A fully contiguous run collapses into a single range."""
    assert summary_ranges([-3, -2, -1, 0, 1, 2, 3]) == ["-3->3"]


@pytest.mark.unit
def test_all_isolated_elements_emit_singletons() -> None:
    """Non-adjacent elements emit individual singleton strings."""
    assert summary_ranges([-10, -5, 0, 5, 10]) == ["-10", "-5", "0", "5", "10"]


@pytest.mark.unit
def test_negative_to_positive_zero_crossing() -> None:
    """Ranges crossing zero and separating negative/positive runs format accurately."""
    assert summary_ranges([-3, -2, 0, 1, 2, 10]) == ["-3->-2", "0->2", "10"]


@pytest.mark.unit
@pytest.mark.parametrize(
    ("nums", "expected"),
    [
        (
            [_INT32_MIN, _INT32_MIN + 1, 0, _INT32_MAX - 1, _INT32_MAX],
            [f"{_INT32_MIN}->{_INT32_MIN + 1}", "0", f"{_INT32_MAX - 1}->{_INT32_MAX}"],
        ),
        ([_INT32_MIN, _INT32_MAX], [str(_INT32_MIN), str(_INT32_MAX)]),
        ([_INT32_MIN, 0], [str(_INT32_MIN), "0"]),
        ([-1, _INT32_MAX], ["-1", str(_INT32_MAX)]),
    ],
)
def test_32bit_extreme_spans(nums: list[int], expected: list[str]) -> None:
    """Signed 32-bit integer extremes do not overflow or coalesce spuriously."""
    assert summary_ranges(nums) == expected
    assert Solution().summary_ranges(nums) == expected


@pytest.mark.unit
def test_accepts_any_sequence() -> None:
    """Tuples and ranges are accepted alongside lists."""
    assert summary_ranges((0, 1, 2, 4, 5, 7)) == ["0->2", "4->5", "7"]
    assert summary_ranges(range(-2, 3)) == ["-2->2"]
    assert summary_ranges(range(0, 10, 2)) == ["0", "2", "4", "6", "8"]


@pytest.mark.unit
@pytest.mark.parametrize(
    "invalid_nums",
    [
        [1, 2, 2, 3],
        [0, 0],
        [-5, -5, -4],
        [_INT32_MIN, _INT32_MIN],
        [_INT32_MAX, _INT32_MAX],
        [3, 1, 2],
        [_INT32_MAX, _INT32_MIN],
        [1, 0],
    ],
)
def test_duplicates_or_unsorted_inputs_raise_value_error(
    invalid_nums: list[int],
) -> None:
    """Duplicate or non-ascending elements raise ValueError on both interfaces."""
    with pytest.raises(ValueError, match=_INVALID_ORDER_PATTERN):
        summary_ranges(invalid_nums)
    with pytest.raises(ValueError, match=_INVALID_ORDER_PATTERN):
        Solution().summary_ranges(invalid_nums)


# ---------------------------------------------------------------------------
# Tier 3: Invariants (deterministic & Hypothesis property-based tests)
# ---------------------------------------------------------------------------


@st.composite
def _non_strictly_ascending_inputs(draw: st.DrawFn) -> list[int]:
    """Generates a list containing at least one duplicate or descending step.

    Args:
        draw: Hypothesis draw callback.

    Returns:
        A list of 32-bit signed integers that is not strictly ascending.
    """
    nums = draw(
        st.lists(
            st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
            min_size=2,
            max_size=40,
        )
    )
    if all(left < right for left, right in itertools.pairwise(nums)):
        idx = draw(st.integers(min_value=0, max_value=len(nums) - 2))
        if draw(st.booleans()):
            nums[idx + 1] = nums[idx]
        else:
            nums[idx], nums[idx + 1] = nums[idx + 1], nums[idx]
    return nums


@pytest.mark.unit
@given(
    nums=st.lists(
        st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
        min_size=1,
        max_size=40,
        unique=True,
    ).map(sorted)
)
def test_hypothesis_range_invariants_and_wrapper_parity(nums: list[int]) -> None:
    """Verify exact reconstruction, minimality, immutability, and Solution parity."""
    snapshot = list(nums)

    result = summary_ranges(nums)

    assert nums == snapshot
    assert Solution().summary_ranges(nums) == result
    assert nums == snapshot

    parsed = _parse_ranges(result)
    assert parsed
    for start, end in parsed:
        assert start <= end
    for (_, prev_end), (next_start, _) in itertools.pairwise(parsed):
        assert prev_end + 1 < next_start

    total_covered = sum(end - start + 1 for start, end in parsed)
    assert total_covered == len(nums)
    reconstructed = [value for start, end in parsed for value in range(start, end + 1)]
    assert reconstructed == nums


@pytest.mark.unit
@given(nums=_non_strictly_ascending_inputs())
def test_hypothesis_non_strictly_ascending_raises_value_error(
    nums: list[int],
) -> None:
    """Any list with duplicates or out-of-order elements raises ValueError."""
    with pytest.raises(ValueError, match=_INVALID_ORDER_PATTERN):
        summary_ranges(nums)


@pytest.mark.unit
def test_max_leetcode_length_deterministic_edges() -> None:
    """Max-constraint length-20 arrays preserve exact coverage and range counts."""
    contiguous = list(range(-10, 10))
    assert summary_ranges(contiguous) == ["-10->9"]
    assert Solution().summary_ranges(contiguous) == ["-10->9"]

    isolated = list(range(0, 40, 2))
    expected_isolated = [str(value) for value in isolated]
    assert summary_ranges(isolated) == expected_isolated
    assert Solution().summary_ranges(isolated) == expected_isolated

    rng = random.Random(228)
    for _ in range(50):
        sample = sorted(rng.sample(range(-50, 51), 20))
        result = summary_ranges(sample)
        assert 1 <= len(result) <= len(sample)
        assert Solution().summary_ranges(sample) == result
        parsed = _parse_ranges(result)
        assert [v for start, end in parsed for v in range(start, end + 1)] == sample


# ---------------------------------------------------------------------------
# Tier 4: Stress workloads and online-judge simulation
# ---------------------------------------------------------------------------


@pytest.mark.stress
def test_large_disjoint_and_contiguous_workloads_run_in_linear_time() -> None:
    """100,000-element workloads complete well within the linear-time threshold."""
    disjoint_count = 50_000
    disjoint_nums = [
        value for index in range(disjoint_count) for value in (3 * index, 3 * index + 1)
    ]
    expected_disjoint = [
        f"{3 * index}->{3 * index + 1}" for index in range(disjoint_count)
    ]

    start = time.perf_counter()
    disjoint_result = summary_ranges(disjoint_nums)
    disjoint_elapsed = time.perf_counter() - start
    assert disjoint_result == expected_disjoint
    assert disjoint_elapsed < 0.5

    contiguous_nums = range(100_000)
    start = time.perf_counter()
    contiguous_result = summary_ranges(contiguous_nums)
    contiguous_elapsed = time.perf_counter() - start
    assert contiguous_result == ["0->99999"]
    assert contiguous_elapsed < 0.5
