"""Unit tests for ``leet_code.utility`` shared helpers.

Organized by helper class (e.g., ``TestIsSorted``) so additional utilities in
``leet_code.utility`` can be added cleanly alongside dedicated unit,
property-based, and stress suites.
"""

import time
from collections.abc import Sequence
from typing import TYPE_CHECKING, overload, override

import pytest
from hypothesis import given
from hypothesis import strategies as st

from leet_code.utility import is_sorted


if TYPE_CHECKING:
    from collections.abc import Iterator


_INT32_MIN = -(2**31)
_INT32_MAX = 2**31 - 1
_BIG_INT = 2**100


class _TrackingSequence(Sequence[int]):
    """Read-only ``Sequence[int]`` that counts elements yielded during iteration."""

    def __init__(self, items: Sequence[int]) -> None:
        """Initializes the tracking sequence around a backing sequence.

        Args:
            items: Backing integer sequence stored immutably as a tuple.
        """
        self._items = tuple(items)
        self.yielded = 0

    @overload
    def __getitem__(self, index: int) -> int: ...

    @overload
    def __getitem__(self, index: slice) -> Sequence[int]: ...

    @override
    def __getitem__(self, index: int | slice) -> int | Sequence[int]:
        """Returns the element or slice at ``index``.

        Args:
            index: Element index or slice.

        Returns:
            Integer element or sliced tuple from the backing sequence.
        """
        return self._items[index]

    @override
    def __len__(self) -> int:
        """Returns the length of the wrapped sequence.

        Returns:
            Number of integers in the wrapped sequence.
        """
        return len(self._items)

    @override
    def __iter__(self) -> Iterator[int]:
        """Yields elements lazily while counting each consumed element.

        Yields:
            Elements of the backing sequence in order.
        """
        for value in self._items:
            self.yielded += 1
            yield value


# ---------------------------------------------------------------------------
# Helpers: is_sorted
# ---------------------------------------------------------------------------


class TestIsSorted:
    """Tests for ``leet_code.utility.is_sorted``."""

    @pytest.mark.unit
    @pytest.mark.parametrize(
        ("values", "expected"),
        [
            ([1, 2, 3, 4], True),
            ([1, 2, 2, 3], True),
            ([1, 3, 2, 4], False),
            ([3, 2, 1], False),
            ([42], True),
            ([], True),
        ],
    )
    def test_canonical_cases(self, values: list[int], expected: bool) -> None:
        """Canonical sorted, duplicate, unsorted, singleton, and empty cases match."""
        assert is_sorted(values) is expected

    @pytest.mark.unit
    @pytest.mark.parametrize(
        "values",
        [
            [],
            [0],
            [-42],
            [42],
            [_INT32_MIN],
            [_INT32_MAX],
        ],
    )
    def test_fewer_than_two_elements_are_vacuously_sorted(
        self,
        values: list[int],
    ) -> None:
        """Sequences of length zero or one always return True."""
        assert is_sorted(values) is True

    @pytest.mark.unit
    @pytest.mark.parametrize(
        ("values", "expected"),
        [
            ([1, 2], True),
            ([2, 2], True),
            ([2, 1], False),
        ],
    )
    def test_all_two_element_orderings(
        self,
        values: list[int],
        expected: bool,
    ) -> None:
        """Two-element sequences cover strictly increasing, equal, and decreasing."""
        assert is_sorted(values) is expected

    @pytest.mark.unit
    @pytest.mark.parametrize(
        ("values", "expected"),
        [
            ([5, 5, 5, 5], True),
            ([1, 1, 2, 2, 3, 3], True),
            ([1, 1, 2, 2, 1, 2], False),
            ([2, 1, 2, 3, 4], False),
            ([1, 2, 3, 2, 3, 4], False),
            ([1, 2, 3, 4, 3], False),
        ],
    )
    def test_duplicates_plateaus_and_localized_inversions(
        self,
        values: list[int],
        expected: bool,
    ) -> None:
        """Equal plateaus pass while single descents at any position return False."""
        assert is_sorted(values) is expected

    @pytest.mark.unit
    @pytest.mark.parametrize(
        ("values", "expected"),
        [
            ([-100, -50, -1, 0, 1, 50], True),
            ([-10, -5, -5, -1], True),
            ([-10, -5, -6, -1], False),
            ([-1, 0, -1, 0], False),
            ([_INT32_MIN, -1, 0, 1, _INT32_MAX], True),
            ([-_BIG_INT, -(2**63), 0, 2**63, _BIG_INT], True),
            ([_BIG_INT, _BIG_INT - 1], False),
            ([-_BIG_INT + 1, -_BIG_INT], False),
        ],
    )
    def test_negative_and_extreme_arbitrary_precision_integers(
        self,
        values: list[int],
        expected: bool,
    ) -> None:
        """Negative values, 32/64-bit bounds, and arbitrary-precision ints compare cleanly."""
        assert is_sorted(values) is expected

    @pytest.mark.unit
    def test_accepts_diverse_read_only_sequences(self) -> None:
        """Tuples, ranges, and custom Sequence subclasses are all accepted."""
        assert is_sorted((1, 2, 2, 3)) is True
        assert is_sorted((1, 3, 2)) is False
        assert is_sorted(range(10)) is True
        assert is_sorted(range(0)) is True
        assert is_sorted(range(-5, 5, 2)) is True
        assert is_sorted(range(5, 0, -1)) is False
        assert is_sorted(range(5, 4, -1)) is True
        assert is_sorted(_TrackingSequence([1, 3, 3, 5])) is True

    @pytest.mark.unit
    def test_short_circuits_lazily_on_first_out_of_order_pair(self) -> None:
        """Stops consuming elements immediately upon encountering an inversion."""
        early = _TrackingSequence([5, 1, 2, 3, 4, 5])
        assert is_sorted(early) is False
        assert early.yielded == 2

        middle = _TrackingSequence([1, 2, 5, 3, 4, 5, 6])
        assert is_sorted(middle) is False
        assert middle.yielded == 4

        late = _TrackingSequence([1, 2, 3, 4, 3])
        assert is_sorted(late) is False
        assert late.yielded == 5

        full = _TrackingSequence([1, 2, 2, 3, 4])
        assert is_sorted(full) is True
        assert full.yielded == 5

        empty = _TrackingSequence([])
        assert is_sorted(empty) is True
        assert empty.yielded == 0

        singleton = _TrackingSequence([7])
        assert is_sorted(singleton) is True
        assert singleton.yielded == 1

    @pytest.mark.unit
    def test_does_not_mutate_input(self) -> None:
        """Mutable list inputs are never modified on either True or False paths."""
        for original in ([1, 2, 3, 4], [3, 1, 2, 4]):
            values = list(original)
            _ = is_sorted(values)
            assert values == original

    @pytest.mark.unit
    @given(values=st.lists(st.integers(), max_size=50))
    def test_hypothesis_matches_sorted_oracle(self, values: list[int]) -> None:
        """Matches ``values == sorted(values)``, preserves input, and matches tuples."""
        snapshot = list(values)
        expected = values == sorted(values)

        result = is_sorted(values)

        assert result is expected
        assert values == snapshot
        assert is_sorted(tuple(values)) is expected

    @pytest.mark.unit
    @given(
        values=st.lists(st.integers(), min_size=2, max_size=50).map(sorted),
        data=st.data(),
    )
    def test_hypothesis_sorted_slices_and_injected_inversion(
        self,
        values: list[int],
        data: st.DataObject,
    ) -> None:
        """Every slice of a sorted list is sorted; injecting one descent yields False."""
        start = data.draw(st.integers(min_value=0, max_value=len(values)))
        end = data.draw(st.integers(min_value=start, max_value=len(values)))
        assert is_sorted(values[start:end]) is True

        idx = data.draw(st.integers(min_value=0, max_value=len(values) - 2))
        corrupted = list(values)
        corrupted[idx] = corrupted[idx + 1] + 1
        assert is_sorted(corrupted) is False

    @pytest.mark.unit
    @given(
        values=st.lists(
            st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
            max_size=40,
        ),
        offset=st.integers(min_value=_INT32_MIN, max_value=_INT32_MAX),
    )
    def test_hypothesis_translation_and_negated_reversal_invariants(
        self,
        values: list[int],
        offset: int,
    ) -> None:
        """Constant translation and negated reversal both preserve sortedness."""
        expected = is_sorted(values)
        shifted = [value + offset for value in values]
        negated_reversed = [-value for value in reversed(values)]

        assert is_sorted(shifted) is expected
        assert is_sorted(negated_reversed) is expected

    @pytest.mark.stress
    def test_large_sequences_run_in_linear_time(self) -> None:
        """200,000-element sorted lists and ranges finish well within linear budget."""
        size = 200_000
        large_list = list(range(size))

        start = time.perf_counter()
        list_result = is_sorted(large_list)
        list_elapsed = time.perf_counter() - start
        assert list_result is True
        assert list_elapsed < 0.5

        start = time.perf_counter()
        range_result = is_sorted(range(size))
        range_elapsed = time.perf_counter() - start
        assert range_result is True
        assert range_elapsed < 0.5

    @pytest.mark.stress
    def test_short_circuit_avoids_scanning_enormous_decreasing_range(self) -> None:
        """Early inversion avoids iterating or materializing a 10^12-element range."""
        huge_decreasing = range(10**12, 0, -1)

        start = time.perf_counter()
        assert is_sorted(huge_decreasing) is False
        elapsed = time.perf_counter() - start
        assert elapsed < 0.1
