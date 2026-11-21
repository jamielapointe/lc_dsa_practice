"""Tests for ``leet_code.merge_sorted_array`` (4-tier layout)."""

import itertools
import random
import time
from collections import Counter

import pytest
from hypothesis import given
from hypothesis import strategies as st

from leet_code.merge_sorted_array import Solution, merge_sorted_array


_LC_MIN = -(10**9)
_LC_MAX = 10**9
_INT32_MIN = -(2**31)
_INT32_MAX = 2**31 - 1
_POS_2_31 = 2**31

_NEGATIVE_COUNT_PATTERN = (
    r"(?i)negat|non-?negat|>= ?0|< ?0|less than|invalid|must be|count|length|size"
    r"|\bm\b|\bn\b"
)
_LENGTH_MISMATCH_PATTERN = (
    r"(?i)len|length|size|match|equal|expect|invalid|nums1|nums2|m \+ n"
)
_UNSORTED_PATTERN = r"(?i)sort|order|decreas|increas|monoton|invalid|nums1|nums2"


# ---------------------------------------------------------------------------
# Tier 1: Canonical feature coverage (official LeetCode examples)
# ---------------------------------------------------------------------------


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n", "expected"),
    [
        ((1, 2, 3, 0, 0, 0), 3, (2, 5, 6), 3, [1, 2, 2, 3, 5, 6]),
        ((1,), 1, (), 0, [1]),
        ((0,), 0, (1,), 1, [1]),
    ],
)
def test_canonical_examples(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
    expected: list[int],
) -> None:
    """Official LeetCode examples merge in-place and leave nums2 untouched."""
    nums1 = list(raw_nums1)
    nums2 = list(raw_nums2)
    nums2_snapshot = list(nums2)
    original_id = id(nums1)

    result = merge_sorted_array(nums1, m, nums2, n)

    assert result is None
    assert id(nums1) == original_id
    assert nums1 == expected
    assert nums2 == nums2_snapshot


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n", "expected"),
    [
        ((1, 2, 3, 0, 0, 0), 3, (2, 5, 6), 3, [1, 2, 2, 3, 5, 6]),
        ((1,), 1, (), 0, [1]),
        ((0,), 0, (1,), 1, [1]),
    ],
)
def test_solution_wrapper_canonical_examples(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
    expected: list[int],
) -> None:
    """The LeetCode wrapper merges official examples in-place and returns None."""
    nums1 = list(raw_nums1)
    nums2 = list(raw_nums2)
    nums2_snapshot = list(nums2)
    original_id = id(nums1)

    result = Solution().merge(nums1, m, nums2, n)

    assert result is None
    assert id(nums1) == original_id
    assert nums1 == expected
    assert nums2 == nums2_snapshot


# ---------------------------------------------------------------------------
# Tier 2: Boundary, corner, edge, and precondition cases
# ---------------------------------------------------------------------------


@pytest.mark.unit
def test_empty_m_and_n_zero_is_vacuously_valid() -> None:
    """Empty arrays with m=0 and n=0 succeed as a no-op on both interfaces."""
    nums1_fn: list[int] = []
    nums2_fn: list[int] = []
    fn_id = id(nums1_fn)
    assert merge_sorted_array(nums1_fn, 0, nums2_fn, 0) is None
    assert id(nums1_fn) == fn_id
    assert nums1_fn == []
    assert nums2_fn == []

    nums1_tuple: list[int] = []
    tuple_id = id(nums1_tuple)
    assert merge_sorted_array(nums1_tuple, 0, (), 0) is None
    assert id(nums1_tuple) == tuple_id
    assert nums1_tuple == []

    nums1_sol: list[int] = []
    nums2_sol: list[int] = []
    sol_id = id(nums1_sol)
    assert Solution().merge(nums1_sol, 0, nums2_sol, 0) is None
    assert id(nums1_sol) == sol_id
    assert nums1_sol == []
    assert nums2_sol == []


@pytest.mark.unit
def test_m_zero_copies_all_of_nums2_into_nums1() -> None:
    """When m=0 and n>0, nums2 overwrites nums1 in-place on both interfaces."""
    nums1_fn = [0, 0, 0, 0]
    nums2_fn = [-5, 0, 3, 9]
    fn_id = id(nums1_fn)
    assert merge_sorted_array(nums1_fn, 0, nums2_fn, 4) is None
    assert id(nums1_fn) == fn_id
    assert nums1_fn == [-5, 0, 3, 9]
    assert nums2_fn == [-5, 0, 3, 9]

    nums1_sol = [0, 0, 0, 0]
    nums2_sol = [-5, 0, 3, 9]
    sol_id = id(nums1_sol)
    assert Solution().merge(nums1_sol, 0, nums2_sol, 4) is None
    assert id(nums1_sol) == sol_id
    assert nums1_sol == [-5, 0, 3, 9]
    assert nums2_sol == [-5, 0, 3, 9]


@pytest.mark.unit
def test_n_zero_leaves_nums1_unchanged() -> None:
    """When m>0 and n=0, nums1 stays identical in-place on both interfaces."""
    nums1_fn = [-10, -2, 0, 7]
    fn_id = id(nums1_fn)
    assert merge_sorted_array(nums1_fn, 4, [], 0) is None
    assert id(nums1_fn) == fn_id
    assert nums1_fn == [-10, -2, 0, 7]

    nums1_sol = [-10, -2, 0, 7]
    sol_id = id(nums1_sol)
    assert Solution().merge(nums1_sol, 4, [], 0) is None
    assert id(nums1_sol) == sol_id
    assert nums1_sol == [-10, -2, 0, 7]


@pytest.mark.unit
def test_accepts_immutable_sequences_for_nums2() -> None:
    """Idiomatic merge_sorted_array accepts tuples and ranges as nums2."""
    nums1_tuple = [1, 3, 5, 0, 0, 0]
    tuple_id = id(nums1_tuple)
    assert merge_sorted_array(nums1_tuple, 3, (2, 4, 6), 3) is None
    assert id(nums1_tuple) == tuple_id
    assert nums1_tuple == [1, 2, 3, 4, 5, 6]

    nums1_range = [1, 5, 0, 0, 0]
    range_id = id(nums1_range)
    assert merge_sorted_array(nums1_range, 2, range(0, 6, 2), 3) is None
    assert id(nums1_range) == range_id
    assert nums1_range == [0, 1, 2, 4, 5]


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "raw_nums2", "expected"),
    [
        ((1, 0), (2,), [1, 2]),
        ((5, 0), (5,), [5, 5]),
        ((9, 0), (2,), [2, 9]),
    ],
)
def test_single_element_pairs(
    raw_nums1: tuple[int, ...],
    raw_nums2: tuple[int, ...],
    expected: list[int],
) -> None:
    """Single-element pairs cover a < b, a == b, and a > b on both interfaces."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    fn_id = id(nums1_fn)
    assert merge_sorted_array(nums1_fn, 1, nums2_fn, 1) is None
    assert id(nums1_fn) == fn_id
    assert nums1_fn == expected
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    sol_id = id(nums1_sol)
    assert Solution().merge(nums1_sol, 1, nums2_sol, 1) is None
    assert id(nums1_sol) == sol_id
    assert nums1_sol == expected
    assert nums2_sol == list(raw_nums2)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n", "expected"),
    [
        ((10, 20, 30, 0, 0, 0), 3, (1, 2, 3), 3, [1, 2, 3, 10, 20, 30]),
        ((1, 2, 3, 0, 0, 0), 3, (10, 20, 30), 3, [1, 2, 3, 10, 20, 30]),
        (
            (1, 3, 5, 7, 9, 0, 0, 0, 0, 0),
            5,
            (2, 4, 6, 8, 10),
            5,
            [1, 2, 3, 4, 5, 6, 7, 8, 9, 10],
        ),
        ((7, 7, 7, 0, 0), 3, (7, 7), 2, [7, 7, 7, 7, 7]),
        ((1, 1, 3, 3, 0, 0, 0), 4, (1, 2, 3), 3, [1, 1, 1, 2, 3, 3, 3]),
        ((5, 0, 0, 0, 0, 0), 1, (1, 2, 3, 4, 6), 5, [1, 2, 3, 4, 5, 6]),
        ((1, 2, 4, 5, 6, 0), 5, (3,), 1, [1, 2, 3, 4, 5, 6]),
    ],
)
def test_ordering_patterns_and_duplicates(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
    expected: list[int],
) -> None:
    """Disjoint, interleaved, duplicate, and asymmetric merges produce exact order."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    assert merge_sorted_array(nums1_fn, m, nums2_fn, n) is None
    assert nums1_fn == expected
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    assert Solution().merge(nums1_sol, m, nums2_sol, n) is None
    assert nums1_sol == expected
    assert nums2_sol == list(raw_nums2)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n", "expected"),
    [
        ((-100, -50, -1, 0, 0), 3, (-75, -2), 2, [-100, -75, -50, -2, -1]),
        (
            (_LC_MIN, -1, 0, _LC_MAX, 0, 0, 0),
            4,
            (_LC_MIN, 0, _LC_MAX),
            3,
            [_LC_MIN, _LC_MIN, -1, 0, 0, _LC_MAX, _LC_MAX],
        ),
        (
            (_INT32_MIN, 0, _INT32_MAX, 0, 0, 0),
            3,
            (_INT32_MIN, -1, _INT32_MAX),
            3,
            [_INT32_MIN, _INT32_MIN, -1, 0, _INT32_MAX, _INT32_MAX],
        ),
        (
            (_INT32_MIN, _POS_2_31, 0, 0),
            2,
            (_INT32_MIN, _POS_2_31),
            2,
            [_INT32_MIN, _INT32_MIN, _POS_2_31, _POS_2_31],
        ),
    ],
)
def test_negative_and_extreme_integer_values(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
    expected: list[int],
) -> None:
    """Negative values, +-10^9, and +-2^31 extremes merge accurately."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    assert merge_sorted_array(nums1_fn, m, nums2_fn, n) is None
    assert nums1_fn == expected
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    assert Solution().merge(nums1_sol, m, nums2_sol, n) is None
    assert nums1_sol == expected
    assert nums2_sol == list(raw_nums2)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n", "expected"),
    [
        ((1, 4, 7, 999, -999, 42), 3, (2, 5, 8), 3, [1, 2, 4, 5, 7, 8]),
        ((100, -50, 0), 0, (-10, 0, 10), 3, [-10, 0, 10]),
        ((5, _INT32_MAX, _INT32_MIN), 1, (1, 9), 2, [1, 5, 9]),
        ((-5, -1, -999_999, 999_999), 2, (-10, 0), 2, [-10, -5, -1, 0]),
    ],
)
def test_non_zero_unsorted_trailing_garbage_is_ignored_and_overwritten(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
    expected: list[int],
) -> None:
    """Unsorted sentinel/garbage values in nums1[m:] do not fail and are overwritten."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    fn_id = id(nums1_fn)
    assert merge_sorted_array(nums1_fn, m, nums2_fn, n) is None
    assert id(nums1_fn) == fn_id
    assert nums1_fn == expected
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    sol_id = id(nums1_sol)
    assert Solution().merge(nums1_sol, m, nums2_sol, n) is None
    assert id(nums1_sol) == sol_id
    assert nums1_sol == expected
    assert nums2_sol == list(raw_nums2)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n"),
    [
        ((), -1, (), 0),
        ((), 0, (), -1),
        ((), -2, (), -3),
        ((1,), -1, (2, 3), 2),
        ((1, 2), 3, (), -1),
    ],
)
def test_negative_m_or_n_raises_value_error_atomically(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
) -> None:
    """Negative m or n raises ValueError without mutating nums1 or nums2."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    fn_id = id(nums1_fn)
    with pytest.raises(ValueError, match=_NEGATIVE_COUNT_PATTERN):
        merge_sorted_array(nums1_fn, m, nums2_fn, n)
    assert id(nums1_fn) == fn_id
    assert nums1_fn == list(raw_nums1)
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    sol_id = id(nums1_sol)
    with pytest.raises(ValueError, match=_NEGATIVE_COUNT_PATTERN):
        Solution().merge(nums1_sol, m, nums2_sol, n)
    assert id(nums1_sol) == sol_id
    assert nums1_sol == list(raw_nums1)
    assert nums2_sol == list(raw_nums2)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n"),
    [
        ((1, 2), 2, (3,), 1),
        ((1, 2, 0, 0), 2, (3,), 1),
        ((1, 0, 0), 1, (2,), 2),
        ((1, 0), 1, (2, 3), 1),
        ((1,), 0, (), 0),
        ((), 1, (), 0),
        ((3, 1), 2, (5, 4), 1),
    ],
)
def test_length_mismatch_raises_value_error_atomically(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
) -> None:
    """Length mismatches on nums1 or nums2 raise ValueError without mutation."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    fn_id = id(nums1_fn)
    with pytest.raises(ValueError, match=_LENGTH_MISMATCH_PATTERN):
        merge_sorted_array(nums1_fn, m, nums2_fn, n)
    assert id(nums1_fn) == fn_id
    assert nums1_fn == list(raw_nums1)
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    sol_id = id(nums1_sol)
    with pytest.raises(ValueError, match=_LENGTH_MISMATCH_PATTERN):
        Solution().merge(nums1_sol, m, nums2_sol, n)
    assert id(nums1_sol) == sol_id
    assert nums1_sol == list(raw_nums1)
    assert nums2_sol == list(raw_nums2)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("raw_nums1", "m", "raw_nums2", "n"),
    [
        ((3, 1, 2, 0), 3, (4,), 1),
        ((1, 3, 2, 0), 3, (4,), 1),
        ((2, 1), 2, (), 0),
        ((1, 2, 0, 0), 2, (5, 3), 2),
        ((0, 0, 0), 0, (1, 3, 2), 3),
        ((4, 2, 0, 0), 2, (9, 1), 2),
    ],
)
def test_unsorted_inputs_raise_value_error_atomically(
    raw_nums1: tuple[int, ...],
    m: int,
    raw_nums2: tuple[int, ...],
    n: int,
) -> None:
    """Unsorted nums1[:m] or nums2[:n] raises ValueError without mutating inputs."""
    nums1_fn = list(raw_nums1)
    nums2_fn = list(raw_nums2)
    fn_id = id(nums1_fn)
    with pytest.raises(ValueError, match=_UNSORTED_PATTERN):
        merge_sorted_array(nums1_fn, m, nums2_fn, n)
    assert id(nums1_fn) == fn_id
    assert nums1_fn == list(raw_nums1)
    assert nums2_fn == list(raw_nums2)

    nums1_sol = list(raw_nums1)
    nums2_sol = list(raw_nums2)
    sol_id = id(nums1_sol)
    with pytest.raises(ValueError, match=_UNSORTED_PATTERN):
        Solution().merge(nums1_sol, m, nums2_sol, n)
    assert id(nums1_sol) == sol_id
    assert nums1_sol == list(raw_nums1)
    assert nums2_sol == list(raw_nums2)


# ---------------------------------------------------------------------------
# Tier 3: Invariants (Hypothesis property-based tests & differential oracle)
# ---------------------------------------------------------------------------


@st.composite
def _valid_merge_inputs(
    draw: st.DrawFn,
) -> tuple[list[int], list[int], list[int]]:
    """Generates sorted ``a`` and ``b`` plus random trailing garbage of length ``len(b)``.

    Args:
        draw: Hypothesis draw callback.

    Returns:
        A triple ``(a, b, garbage)`` where ``a`` and ``b`` are sorted in
        non-decreasing order and ``len(garbage) == len(b)``.
    """
    a = sorted(
        draw(
            st.lists(
                st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
                max_size=40,
            )
        )
    )
    b = sorted(
        draw(
            st.lists(
                st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
                max_size=40,
            )
        )
    )
    garbage = draw(
        st.lists(
            st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
            min_size=len(b),
            max_size=len(b),
        )
    )
    return a, b, garbage


@st.composite
def _unsorted_list(draw: st.DrawFn) -> list[int]:
    """Generates a list of integers guaranteed not to be in non-decreasing order.

    Args:
        draw: Hypothesis draw callback.

    Returns:
        A list of at least two integers containing at least one strict descent.
    """
    values = draw(
        st.lists(
            st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
            min_size=2,
            max_size=25,
        )
    )
    if all(left <= right for left, right in itertools.pairwise(values)):
        idx = draw(st.integers(min_value=0, max_value=len(values) - 2))
        if values[idx] == values[idx + 1]:
            if values[idx] < _INT32_MAX:
                values[idx] += 1
            else:
                values[idx + 1] -= 1
        else:
            values[idx], values[idx + 1] = values[idx + 1], values[idx]
    return values


@pytest.mark.unit
@given(data=_valid_merge_inputs())
def test_hypothesis_differential_oracle_and_in_place_invariants(
    data: tuple[list[int], list[int], list[int]],
) -> None:
    """Output matches sorted(a + b), preserves id(nums1), and leaves nums2 intact."""
    a, b, garbage = data
    m = len(a)
    n = len(b)
    expected = sorted(a + b)

    nums1 = a + garbage
    nums2 = list(b)
    original_id = id(nums1)

    result = merge_sorted_array(nums1, m, nums2, n)

    assert result is None
    assert id(nums1) == original_id
    assert nums2 == b
    assert len(nums1) == m + n
    assert all(left <= right for left, right in itertools.pairwise(nums1))
    assert Counter(nums1) == Counter(a) + Counter(b)
    assert nums1 == expected

    nums1_tuple = a + garbage
    tuple_id = id(nums1_tuple)
    assert merge_sorted_array(nums1_tuple, m, tuple(b), n) is None
    assert id(nums1_tuple) == tuple_id
    assert nums1_tuple == expected

    nums1_sol = a + garbage
    nums2_sol = list(b)
    sol_id = id(nums1_sol)
    assert Solution().merge(nums1_sol, m, nums2_sol, n) is None
    assert id(nums1_sol) == sol_id
    assert nums2_sol == b
    assert nums1_sol == expected


@pytest.mark.unit
@given(data=_valid_merge_inputs())
def test_hypothesis_commutativity_of_merge(
    data: tuple[list[int], list[int], list[int]],
) -> None:
    """Merging (a, b) yields the same final sequence as merging (b, a)."""
    a, b, garbage_b = data
    garbage_a = [0] * len(a)

    ab = a + garbage_b
    ba = b + garbage_a

    merge_sorted_array(ab, len(a), b, len(b))
    merge_sorted_array(ba, len(b), a, len(a))

    assert ab == ba == sorted(a + b)


@pytest.mark.unit
@given(
    unsorted=_unsorted_list(),
    other=st.lists(
        st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
        max_size=20,
    ).map(sorted),
    corrupt_first=st.booleans(),
)
def test_hypothesis_unsorted_input_raises_atomically(
    unsorted: list[int],
    other: list[int],
    corrupt_first: bool,
) -> None:
    """Any unsorted active slice in nums1[:m] or nums2[:n] fails without mutation."""
    if corrupt_first:
        nums1 = unsorted + [0] * len(other)
        m = len(unsorted)
        nums2 = list(other)
        n = len(other)
    else:
        nums1 = other + [0] * len(unsorted)
        m = len(other)
        nums2 = list(unsorted)
        n = len(unsorted)

    snapshot1 = list(nums1)
    snapshot2 = list(nums2)
    original_id = id(nums1)

    with pytest.raises(ValueError, match=_UNSORTED_PATTERN):
        merge_sorted_array(nums1, m, nums2, n)

    assert id(nums1) == original_id
    assert nums1 == snapshot1
    assert nums2 == snapshot2


@pytest.mark.unit
def test_deterministic_seeded_differential_checks() -> None:
    """Seeded PRNG cases with duplicates, wide ranges, and garbage match sorted(a + b)."""
    rng = random.Random(88)
    for case_idx in range(50):
        m = rng.randint(0, 40)
        n = rng.randint(0, 40)
        low, high = (-5, 5) if case_idx % 2 == 0 else (_LC_MIN, _LC_MAX)
        a = sorted(rng.randint(low, high) for _ in range(m))
        b = sorted(rng.randint(low, high) for _ in range(n))
        garbage = [rng.randint(_LC_MIN, _LC_MAX) for _ in range(n)]
        expected = sorted(a + b)

        nums1_fn = a + garbage
        nums2_fn = list(b)
        fn_id = id(nums1_fn)
        assert merge_sorted_array(nums1_fn, m, nums2_fn, n) is None
        assert id(nums1_fn) == fn_id
        assert nums1_fn == expected
        assert nums2_fn == b

        nums1_sol = a + garbage
        nums2_sol = list(b)
        assert Solution().merge(nums1_sol, m, nums2_sol, n) is None
        assert nums1_sol == expected
        assert nums2_sol == b


# ---------------------------------------------------------------------------
# Tier 4: Stress workloads and online-judge simulation
# ---------------------------------------------------------------------------


@pytest.mark.stress
def test_max_leetcode_constraint_workloads() -> None:
    """Max LeetCode constraint m=200, n=200 workloads stay fast and exact."""
    m = 200
    n = 200
    workloads = [
        (list(range(n, m + n)), list(range(n))),
        (list(range(0, 2 * m, 2)), list(range(1, 2 * n, 2))),
        ([value // 10 for value in range(m)], [value // 10 for value in range(n)]),
    ]
    garbage = [-(idx * 17) for idx in range(n)]

    for a, b in workloads:
        expected = sorted(a + b)
        nums1_fn = a + garbage
        nums2_fn = list(b)
        fn_id = id(nums1_fn)

        start = time.perf_counter()
        result = merge_sorted_array(nums1_fn, m, nums2_fn, n)
        elapsed = time.perf_counter() - start

        assert result is None
        assert id(nums1_fn) == fn_id
        assert nums1_fn == expected
        assert nums2_fn == b
        assert elapsed < 0.5

        nums1_sol = a + garbage
        nums2_sol = list(b)
        assert Solution().merge(nums1_sol, m, nums2_sol, n) is None
        assert nums1_sol == expected
        assert nums2_sol == b


@pytest.mark.stress
def test_large_linear_time_stress_workloads() -> None:
    """Large m=50,000, n=50,000 workloads complete within a generous O(m + n) budget."""
    size = 50_000
    workloads = [
        (list(range(size, 2 * size)), list(range(size))),
        (list(range(0, 2 * size, 2)), list(range(1, 2 * size, 2))),
        ([i // 500 for i in range(size)], [i // 500 for i in range(size)]),
    ]
    garbage = [-1] * size

    for a, b in workloads:
        expected = sorted(a + b)
        nums1 = a + garbage
        nums2 = list(b)
        original_id = id(nums1)

        start = time.perf_counter()
        result = merge_sorted_array(nums1, size, nums2, size)
        elapsed = time.perf_counter() - start

        assert result is None
        assert id(nums1) == original_id
        assert nums1 == expected
        assert nums2 == b
        assert elapsed < 1.0


@pytest.mark.unit
def test_oj_simulation_table() -> None:
    """LeetCode wrapper evaluates a representative table of online-judge cases."""
    oj_cases: list[tuple[list[int], int, list[int], int, list[int]]] = [
        ([1, 2, 3, 0, 0, 0], 3, [2, 5, 6], 3, [1, 2, 2, 3, 5, 6]),
        ([1], 1, [], 0, [1]),
        ([0], 0, [1], 1, [1]),
        ([2, 0], 1, [1], 1, [1, 2]),
        ([4, 5, 6, 0, 0, 0], 3, [1, 2, 3], 3, [1, 2, 3, 4, 5, 6]),
        ([-1, 0, 0, 3, 3, 3, 0, 0, 0], 6, [1, 2, 2], 3, [-1, 0, 0, 1, 2, 2, 3, 3, 3]),
        (
            [_LC_MIN, _LC_MAX, 0, 0],
            2,
            [_LC_MIN, _LC_MAX],
            2,
            [_LC_MIN, _LC_MIN, _LC_MAX, _LC_MAX],
        ),
    ]

    for nums1, m, nums2, n, expected in oj_cases:
        nums2_snapshot = list(nums2)
        original_id = id(nums1)
        assert Solution().merge(nums1, m, nums2, n) is None
        assert id(nums1) == original_id
        assert nums1 == expected
        assert nums2 == nums2_snapshot
