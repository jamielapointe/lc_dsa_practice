"""Tests for ``leet_code.lru_cache`` (4-tier layout mirroring the C++ test suites)."""

from __future__ import annotations

import random
import time
from collections import OrderedDict

import pytest

from leet_code.lru_cache import LRUCache
from leet_code.lru_cache import LruCache


# ---------------------------------------------------------------------------
# Tier 1: Canonical feature coverage (official LeetCode example)
# ---------------------------------------------------------------------------


def test_official_example() -> None:
    """Replays the LeetCode example sequence."""
    cache = LruCache(2)
    cache.put(1, 1)
    cache.put(2, 2)
    assert cache.get(1) == 1
    cache.put(3, 3)  # Evicts key 2.
    assert cache.get(2) == -1
    cache.put(4, 4)  # Evicts key 1.
    assert cache.get(1) == -1
    assert cache.get(3) == 3
    assert cache.get(4) == 4


def test_leetcode_alias_is_same_class() -> None:
    """The LeetCode-named alias refers to the real implementation."""
    assert LRUCache is LruCache


# ---------------------------------------------------------------------------
# Tier 2: Boundary, corner and edge cases
# ---------------------------------------------------------------------------


def test_capacity_one_evicts_on_every_new_key() -> None:
    """With capacity one each new key evicts the previous one."""
    cache = LruCache(1)
    cache.put(1, 10)
    cache.put(2, 20)
    assert cache.get(1) == -1
    assert cache.get(2) == 20
    assert len(cache) == 1


def test_update_existing_key_does_not_evict() -> None:
    """Updating a key changes its value and refreshes recency without evicting."""
    cache = LruCache(2)
    cache.put(1, 1)
    cache.put(2, 2)
    cache.put(1, 100)
    cache.put(3, 3)  # Key 2 is now least recently used.
    assert cache.get(1) == 100
    assert cache.get(2) == -1
    assert cache.get(3) == 3


def test_get_refreshes_recency() -> None:
    """A successful get makes the key most recently used."""
    cache = LruCache(2)
    cache.put(1, 1)
    cache.put(2, 2)
    assert cache.get(1) == 1
    cache.put(3, 3)  # Evicts key 2, not key 1.
    assert 1 in cache
    assert 2 not in cache


def test_missing_key_returns_minus_one() -> None:
    """Looking up an absent key returns the sentinel -1."""
    assert LruCache(3).get(42) == -1


def test_zero_value_is_distinguishable_from_missing() -> None:
    """A stored value of zero is returned as zero."""
    cache = LruCache(2)
    cache.put(7, 0)
    assert cache.get(7) == 0


@pytest.mark.parametrize('capacity', [0, -1])
def test_non_positive_capacity_is_rejected(capacity: int) -> None:
    """Capacity must be positive."""
    with pytest.raises(ValueError, match='positive'):
        LruCache(capacity)


# ---------------------------------------------------------------------------
# Tier 3: Invariants (differential test against OrderedDict)
# ---------------------------------------------------------------------------


def test_matches_reference_model_on_random_operations() -> None:
    """Random operations agree with a trivially-correct OrderedDict model and never exceed capacity."""
    rng = random.Random(2024)
    capacity = 5
    cache = LruCache(capacity)
    model: OrderedDict[int, int] = OrderedDict()
    for _ in range(5000):
        key = rng.randint(0, 12)
        if rng.random() < 0.5:
            value = rng.randint(0, 99)
            cache.put(key, value)
            model[key] = value
            model.move_to_end(key)
            if len(model) > capacity:
                model.popitem(last=False)
        else:
            expected = model.get(key, -1)
            if key in model:
                model.move_to_end(key)
            assert cache.get(key) == expected
        assert len(cache) == len(model) <= capacity
    assert cache.capacity == capacity


# ---------------------------------------------------------------------------
# Tier 4: Stress workloads and online-judge simulation
# ---------------------------------------------------------------------------


@pytest.mark.stress
def test_maximum_constraint_workload_runs_quickly() -> None:
    """2 * 10^5 operations on a 3000-entry cache finish well under the time limit."""
    rng = random.Random(7)
    cache = LruCache(3000)
    start = time.perf_counter()
    for _ in range(100_000):
        cache.put(rng.randint(0, 10_000), rng.randint(0, 100_000))
        cache.get(rng.randint(0, 10_000))
    elapsed = time.perf_counter() - start
    assert len(cache) <= 3000
    assert elapsed < 2.0
