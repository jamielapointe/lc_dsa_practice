"""Tests for ``leet_code.kth_largest_element_in_a_stream`` (4-tier layout)."""

import itertools
import random
import time

import pytest
from hypothesis import given
from hypothesis import strategies as st

from leet_code.kth_largest_element_in_a_stream import KthLargest, KthLargestStream


_LC_MIN = -10_000
_LC_MAX = 10_000
_INT32_MIN = -(2**31)
_INT32_MAX = 2**31 - 1
_LARGE_ABS = 10**18

_INVALID_K_PATTERN = r"(?i)\bk\b|positiv|at least 1|>= ?1|> ?0|<= ?0|< ?1|invalid"
_INSUFFICIENT_NUMS_PATTERN = (
    r"(?i)nums|element|len|size|length|insufficient|at least|fewer|k - 1|k-1"
    r"|\bk\b|invalid"
)


# ---------------------------------------------------------------------------
# Tier 1: Canonical feature coverage (official LeetCode examples)
# ---------------------------------------------------------------------------


@pytest.mark.unit
@pytest.mark.parametrize(
    ("k", "raw_nums", "additions", "expected"),
    [
        (3, (4, 5, 8, 2), (3, 5, 10, 9, 4), [4, 5, 5, 8, 8]),
        (4, (7, 7, 7, 7, 8, 3), (2, 10, 9, 9), [7, 7, 7, 8]),
    ],
)
def test_canonical_examples_idiomatic_stream(
    k: int,
    raw_nums: tuple[int, ...],
    additions: tuple[int, ...],
    expected: list[int],
) -> None:
    """Official LeetCode examples return exact k-th largest scores on tuple and list."""
    tuple_stream = KthLargestStream(k, raw_nums)
    assert tuple_stream.k == k
    assert len(tuple_stream) == min(len(raw_nums), k)
    assert [tuple_stream.add(val) for val in additions] == expected
    assert len(tuple_stream) == k

    nums_list = list(raw_nums)
    list_stream = KthLargestStream(k, nums_list)
    assert nums_list == list(raw_nums)
    assert [list_stream.add(val) for val in additions] == expected
    assert nums_list == list(raw_nums)
    assert len(list_stream) == k


@pytest.mark.unit
@pytest.mark.parametrize(
    ("k", "raw_nums", "additions", "expected"),
    [
        (3, (4, 5, 8, 2), (3, 5, 10, 9, 4), [4, 5, 5, 8, 8]),
        (4, (7, 7, 7, 7, 8, 3), (2, 10, 9, 9), [7, 7, 7, 8]),
    ],
)
def test_canonical_examples_leetcode_wrapper(
    k: int,
    raw_nums: tuple[int, ...],
    additions: tuple[int, ...],
    expected: list[int],
) -> None:
    """Official KthLargest wrapper matches LeetCode Examples 1 and 2 without mutation."""
    nums = list(raw_nums)
    tracker = KthLargest(k, nums)
    assert nums == list(raw_nums)
    assert [tracker.add(val) for val in additions] == expected
    assert nums == list(raw_nums)


# ---------------------------------------------------------------------------
# Tier 2: Boundary, corner, edge, and precondition cases
# ---------------------------------------------------------------------------


@pytest.mark.unit
def test_starved_empty_initial_stream_k_equals_one_is_valid() -> None:
    """Starved initialization k=1, nums=[] is valid and tracks the running maximum."""
    stream = KthLargestStream(1, ())
    assert stream.k == 1
    assert len(stream) == 0
    assert [stream.add(v) for v in (-5, -10, 0, 7, 3)] == [-5, -5, 0, 7, 7]
    assert len(stream) == 1

    nums: list[int] = []
    wrapper = KthLargest(1, nums)
    assert nums == []
    assert [wrapper.add(v) for v in (-5, -10, 0, 7, 3)] == [-5, -5, 0, 7, 7]


@pytest.mark.unit
def test_starved_non_empty_initial_stream_len_equals_k_minus_one() -> None:
    """Starved initialization k=3, nums=[10, 20] succeeds and reaches size k on add."""
    nums = [10, 20]
    stream = KthLargestStream(3, nums)
    assert nums == [10, 20]
    assert stream.k == 3
    assert len(stream) == 2
    assert [stream.add(v) for v in (5, 15, 25, 15)] == [5, 10, 15, 15]
    assert len(stream) == 3

    wrapper = KthLargest(3, nums)
    assert [wrapper.add(v) for v in (5, 15, 25, 15)] == [5, 10, 15, 15]
    assert nums == [10, 20]


@pytest.mark.unit
def test_k_equals_one_tracks_running_maximum() -> None:
    """When k=1 on a populated stream, every add returns the running maximum."""
    nums = [3, -2, 7, 4]
    stream = KthLargestStream(1, nums)
    assert len(stream) == 1
    assert [stream.add(v) for v in (5, 7, 10, -100, 12)] == [7, 7, 10, 10, 12]
    assert len(stream) == 1

    wrapper = KthLargest(1, nums)
    assert [wrapper.add(v) for v in (5, 7, 10, -100, 12)] == [7, 7, 10, 10, 12]


@pytest.mark.unit
def test_k_equals_len_plus_one_tracks_running_minimum_on_first_add() -> None:
    """When k=len(nums)+1, the first add yields the global minimum before evictions."""
    nums = [8, 2, 6]
    stream = KthLargestStream(4, nums)
    assert len(stream) == 3
    assert [stream.add(v) for v in (10, 1, 5, 9)] == [2, 2, 5, 6]
    assert len(stream) == 4

    wrapper = KthLargest(4, nums)
    assert [wrapper.add(v) for v in (10, 1, 5, 9)] == [2, 2, 5, 6]


@pytest.mark.unit
def test_all_negative_values() -> None:
    """Streams of strictly negative integers maintain exact order statistics."""
    nums = [_LC_MIN, -50, -300, -5]
    expected = [-50, -20, -5, -5]
    stream = KthLargestStream(2, nums)
    assert [stream.add(v) for v in (-100, -20, -5, -1)] == expected

    wrapper = KthLargest(2, nums)
    assert [wrapper.add(v) for v in (-100, -20, -5, -1)] == expected


@pytest.mark.unit
def test_all_identical_duplicate_values() -> None:
    """Duplicate values only shift the k-th largest once k strictly larger items exist."""
    nums = [7, 7, 7, 7]
    additions = (7, 6, 8, 8, 8)
    expected = [7, 7, 7, 7, 8]

    stream = KthLargestStream(3, nums)
    assert [stream.add(v) for v in additions] == expected

    wrapper = KthLargest(3, nums)
    assert [wrapper.add(v) for v in additions] == expected


@pytest.mark.unit
def test_int32_and_arbitrary_precision_boundary_values() -> None:
    """32-bit signed boundaries and 64-bit+ arbitrary-precision ints avoid overflow."""
    nums_i32 = [_INT32_MIN, 0, _INT32_MAX]
    stream_i32 = KthLargestStream(2, nums_i32)
    assert [stream_i32.add(v) for v in (_INT32_MIN, -1, _INT32_MAX)] == [
        0,
        0,
        _INT32_MAX,
    ]

    nums_big = [-_LARGE_ABS, _LARGE_ABS]
    wrapper_big = KthLargest(2, nums_big)
    assert [wrapper_big.add(v) for v in (-_LARGE_ABS, 0, _LARGE_ABS)] == [
        -_LARGE_ABS,
        0,
        _LARGE_ABS,
    ]


@pytest.mark.unit
def test_accepts_lazy_range_sequence() -> None:
    """Idiomatic KthLargestStream accepts non-list Sequence types such as range."""
    stream = KthLargestStream(3, range(2, 10, 2))
    assert len(stream) == 3
    assert [stream.add(v) for v in (5, 9)] == [5, 6]


@pytest.mark.unit
@pytest.mark.parametrize(
    ("invalid_k", "raw_nums"),
    [
        (0, ()),
        (0, (1, 2, 3)),
        (-1, ()),
        (-5, ()),
        (-5, (4, 5, 8, 2)),
    ],
)
def test_non_positive_k_raises_value_error_first(
    invalid_k: int,
    raw_nums: tuple[int, ...],
) -> None:
    """Non-positive k raises ValueError before checking nums length or mutating."""
    nums_stream = list(raw_nums)
    with pytest.raises(ValueError, match=_INVALID_K_PATTERN):
        KthLargestStream(invalid_k, nums_stream)
    assert nums_stream == list(raw_nums)

    nums_wrapper = list(raw_nums)
    with pytest.raises(ValueError, match=_INVALID_K_PATTERN):
        KthLargest(invalid_k, nums_wrapper)
    assert nums_wrapper == list(raw_nums)


@pytest.mark.unit
@pytest.mark.parametrize(
    ("k", "raw_nums"),
    [
        (2, ()),
        (3, (42,)),
        (5, (1, 2, 3)),
    ],
)
def test_insufficient_initial_elements_raises_value_error(
    k: int,
    raw_nums: tuple[int, ...],
) -> None:
    """Insufficient initial elements (len(nums) + 1 < k) raise ValueError atomically."""
    nums_stream = list(raw_nums)
    with pytest.raises(ValueError, match=_INSUFFICIENT_NUMS_PATTERN):
        KthLargestStream(k, nums_stream)
    assert nums_stream == list(raw_nums)

    nums_wrapper = list(raw_nums)
    with pytest.raises(ValueError, match=_INSUFFICIENT_NUMS_PATTERN):
        KthLargest(k, nums_wrapper)
    assert nums_wrapper == list(raw_nums)


# ---------------------------------------------------------------------------
# Tier 3: Invariants (Hypothesis property-based tests & differential oracle)
# ---------------------------------------------------------------------------


@st.composite
def _valid_stream_workloads(
    draw: st.DrawFn,
) -> tuple[int, list[int], list[int]]:
    """Generates valid ``(k, nums, additions)`` triples including starved streams.

    Args:
        draw: Hypothesis draw callback.

    Returns:
        A tuple ``(k, nums, additions)`` satisfying ``k >= 1``,
        ``len(nums) >= k - 1``, and ``len(additions) >= 1``.
    """
    k = draw(st.integers(min_value=1, max_value=25))
    nums = draw(
        st.lists(
            st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
            min_size=k - 1,
            max_size=50,
        )
    )
    additions = draw(
        st.lists(
            st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
            min_size=1,
            max_size=50,
        )
    )
    return k, nums, additions


@pytest.mark.unit
@given(workload=_valid_stream_workloads())
def test_hypothesis_invariants_and_sorted_oracle(
    workload: tuple[int, list[int], list[int]],
) -> None:
    """Checks immutability, len <= k, monotonicity, and sorted-pool oracle parity."""
    k, original_nums, additions = workload
    nums_for_stream = list(original_nums)
    nums_for_wrapper = list(original_nums)

    stream = KthLargestStream(k, nums_for_stream)
    tuple_stream = KthLargestStream(k, tuple(original_nums))
    wrapper = KthLargest(k, nums_for_wrapper)

    assert stream.k == k
    assert len(stream) == min(len(original_nums), k) <= k
    assert nums_for_stream == original_nums
    assert nums_for_wrapper == original_nums

    pool = list(original_nums)
    outputs: list[int] = []
    for val in additions:
        pool.append(val)
        expected_kth = sorted(pool, reverse=True)[k - 1]
        actual = stream.add(val)
        assert actual == expected_kth
        assert tuple_stream.add(val) == expected_kth
        assert wrapper.add(val) == expected_kth
        assert len(stream) == min(len(pool), k) == k
        outputs.append(actual)

    assert all(prev <= curr for prev, curr in itertools.pairwise(outputs))
    assert nums_for_stream == original_nums
    assert nums_for_wrapper == original_nums


@pytest.mark.unit
@given(
    invalid_k=st.integers(max_value=0),
    nums=st.lists(st.integers(min_value=_LC_MIN, max_value=_LC_MAX), max_size=20),
)
def test_hypothesis_non_positive_k_always_raises(
    invalid_k: int,
    nums: list[int],
) -> None:
    """Any zero or negative k raises ValueError without mutating caller input."""
    snapshot = list(nums)
    with pytest.raises(ValueError, match=_INVALID_K_PATTERN):
        KthLargestStream(invalid_k, nums)
    with pytest.raises(ValueError, match=_INVALID_K_PATTERN):
        KthLargest(invalid_k, nums)
    assert nums == snapshot


@pytest.mark.unit
@given(
    data=st.integers(min_value=2, max_value=30).flatmap(
        lambda k: st.tuples(
            st.just(k),
            st.lists(
                st.integers(min_value=_LC_MIN, max_value=_LC_MAX),
                max_size=k - 2,
            ),
        )
    )
)
def test_hypothesis_too_few_initial_elements_always_raises(
    data: tuple[int, list[int]],
) -> None:
    """Whenever len(nums) + 1 < k, initialization raises ValueError atomically."""
    k, nums = data
    snapshot = list(nums)
    with pytest.raises(ValueError, match=_INSUFFICIENT_NUMS_PATTERN):
        KthLargestStream(k, nums)
    with pytest.raises(ValueError, match=_INSUFFICIENT_NUMS_PATTERN):
        KthLargest(k, nums)
    assert nums == snapshot


@pytest.mark.unit
def test_deterministic_seeded_differential_checks() -> None:
    """Seeded PRNG cases with duplicates and starved starts match sorted-pool oracle."""
    rng = random.Random(703)
    for case_idx in range(40):
        k = rng.randint(1, 20)
        n = k - 1 if case_idx % 4 == 0 else rng.randint(k - 1, 45)
        low, high = (-5, 5) if case_idx % 2 == 0 else (_LC_MIN, _LC_MAX)
        nums = [rng.randint(low, high) for _ in range(n)]
        snapshot = list(nums)

        stream = KthLargestStream(k, nums)
        wrapper = KthLargest(k, nums)
        assert len(stream) == min(n, k)

        pool = list(nums)
        prev_out: int | None = None
        for _ in range(30):
            val = rng.randint(low, high)
            pool.append(val)
            expected = sorted(pool, reverse=True)[k - 1]
            out = stream.add(val)
            assert out == expected
            assert wrapper.add(val) == expected
            assert len(stream) == k
            if prev_out is not None:
                assert prev_out <= out
            prev_out = out
        assert nums == snapshot


# ---------------------------------------------------------------------------
# Tier 4: Stress workloads and online-judge simulation
# ---------------------------------------------------------------------------


@pytest.mark.stress
def test_max_leetcode_constraint_stream_workload() -> None:
    """Max constraint n=10,000, k=5,000, q=10,000 stream finishes within 2.0s."""
    rng = random.Random(2026)
    k = 5_000
    initial_nums = [rng.randint(_LC_MIN, _LC_MAX) for _ in range(10_000)]
    additions = [rng.randint(_LC_MIN, _LC_MAX) for _ in range(10_000)]
    snapshot = list(initial_nums)

    expected_first = sorted([*initial_nums, additions[0]], reverse=True)[k - 1]
    expected_final = sorted(initial_nums + additions, reverse=True)[k - 1]

    start = time.perf_counter()
    stream = KthLargestStream(k, initial_nums)
    outputs = [stream.add(val) for val in additions]
    elapsed = time.perf_counter() - start

    assert elapsed < 2.0
    assert initial_nums == snapshot
    assert len(stream) == k
    assert outputs[0] == expected_first
    assert outputs[-1] == expected_final
    assert all(prev <= curr for prev, curr in itertools.pairwise(outputs))


@pytest.mark.stress
def test_max_leetcode_constraint_wrapper_workload() -> None:
    """Max constraint n=10,000, k=5,000, q=10,000 KthLargest wrapper runs under 2.0s."""
    rng = random.Random(2026)
    k = 5_000
    initial_nums = [rng.randint(_LC_MIN, _LC_MAX) for _ in range(10_000)]
    additions = [rng.randint(_LC_MIN, _LC_MAX) for _ in range(10_000)]
    snapshot = list(initial_nums)
    expected_final = sorted(initial_nums + additions, reverse=True)[k - 1]

    start = time.perf_counter()
    wrapper = KthLargest(k, initial_nums)
    outputs = [wrapper.add(val) for val in additions]
    elapsed = time.perf_counter() - start

    assert elapsed < 2.0
    assert initial_nums == snapshot
    assert outputs[-1] == expected_final
    assert all(prev <= curr for prev, curr in itertools.pairwise(outputs))
