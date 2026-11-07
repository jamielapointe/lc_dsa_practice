#![allow(clippy::disallowed_methods, clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! 4-Tier integration test suite for LeetCode #146: LRU Cache.

use leet_code::lru_cache::{LRUCache, LruCache};

// ===========================================================================
// Tier 1: Canonical Examples
// ===========================================================================

#[test]
fn test_canonical_example_idiomatic() {
    let mut cache = LruCache::new(2);
    cache.put(1, 1);
    cache.put(2, 2);
    assert_eq!(cache.get(1), Some(1));
    cache.put(3, 3); // Evicts key 2
    assert_eq!(cache.get(2), None);
    cache.put(4, 4); // Evicts key 1
    assert_eq!(cache.get(1), None);
    assert_eq!(cache.get(3), Some(3));
    assert_eq!(cache.get(4), Some(4));
}

#[test]
fn test_canonical_example_leetcode_wrapper() {
    let mut lru = LRUCache::new(2);
    lru.put(1, 1);
    lru.put(2, 2);
    assert_eq!(lru.get(1), 1);
    lru.put(3, 3); // Evicts key 2
    assert_eq!(lru.get(2), -1);
    lru.put(4, 4); // Evicts key 1
    assert_eq!(lru.get(1), -1);
    assert_eq!(lru.get(3), 3);
    assert_eq!(lru.get(4), 4);
}

// ===========================================================================
// Tier 2: Boundary & Corner Cases
// ===========================================================================

#[test]
fn test_capacity_one_boundary() {
    let mut cache = LruCache::new(1);
    cache.put(1, 100);
    assert_eq!(cache.get(1), Some(100));
    cache.put(2, 200); // Evicts 1
    assert_eq!(cache.get(1), None);
    assert_eq!(cache.get(2), Some(200));
}

#[test]
fn test_capacity_zero_boundary() {
    let mut cache = LruCache::new(0);
    cache.put(1, 100);
    assert_eq!(cache.get(1), None);
    assert_eq!(cache.len(), 0);
    assert!(cache.is_empty());
}

#[test]
fn test_overwrite_existing_key_updates_value_and_recency() {
    let mut cache = LruCache::new(2);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(1, 100); // Update key 1 -> now MRU
    assert_eq!(cache.len(), 2);

    cache.put(3, 30); // Evicts key 2 (not 1)
    assert_eq!(cache.get(2), None);
    assert_eq!(cache.get(1), Some(100));
    assert_eq!(cache.get(3), Some(30));
}

#[test]
fn test_get_promotes_to_most_recently_used() {
    let mut cache = LruCache::new(2);
    cache.put(1, 10);
    cache.put(2, 20);
    // Access 1 so 2 becomes the least recently used
    assert_eq!(cache.get(1), Some(10));
    cache.put(3, 30); // Evicts 2
    assert_eq!(cache.get(2), None);
    assert_eq!(cache.get(1), Some(10));
    assert_eq!(cache.get(3), Some(30));
}

// ===========================================================================
// Tier 3: Invariants & Properties
// ===========================================================================

#[test]
fn test_capacity_and_length_invariants() {
    let mut cache = LruCache::new(3);
    assert_eq!(cache.capacity(), 3);
    assert!(cache.is_empty());

    cache.put(1, 1);
    assert_eq!(cache.len(), 1);
    assert!(!cache.is_empty());

    cache.put(2, 2);
    cache.put(3, 3);
    assert_eq!(cache.len(), 3);

    cache.put(4, 4);
    assert_eq!(cache.len(), 3, "Length must never exceed capacity");

    assert!(cache.contains_key(2));
    assert!(cache.contains_key(3));
    assert!(cache.contains_key(4));
    assert!(!cache.contains_key(1));
}

// ===========================================================================
// Tier 4: Stress / High-Constraint Tests
// ===========================================================================

#[test]
fn test_stress_200k_operations() {
    const CAPACITY: usize = 100;
    const NUM_KEYS: i32 = 500;
    const TOTAL_OPS: i32 = 200_000;

    let mut cache = LruCache::new(CAPACITY);

    for i in 0..TOTAL_OPS {
        let key = i % NUM_KEYS;
        if i % 3 == 0 {
            let _ = cache.get(key);
        } else {
            cache.put(key, i);
        }
    }

    assert!(cache.len() <= CAPACITY);
}

// ===========================================================================
// Tier 5: Adversarial Stress & Challenger Harness
// ===========================================================================

#[test]
fn test_adversarial_lru_edge_capacities() {
    // Capacity 0 idiomatic
    let mut cache0 = LruCache::new(0);
    assert_eq!(cache0.capacity(), 0);
    assert_eq!(cache0.len(), 0);
    assert!(cache0.is_empty());
    assert!(!cache0.contains_key(1));
    cache0.put(1, 100);
    assert_eq!(cache0.get(1), None);
    assert_eq!(cache0.len(), 0);
    cache0.put(2, 200);
    assert_eq!(cache0.get(2), None);
    assert_eq!(cache0.len(), 0);

    // Capacity 0 wrapper
    let mut lru0 = LRUCache::new(0);
    lru0.put(1, 100);
    assert_eq!(lru0.get(1), -1);

    // Negative capacity wrapper (falls back to 0)
    let mut lru_neg = LRUCache::new(-10);
    lru_neg.put(1, 100);
    assert_eq!(lru_neg.get(1), -1);

    // Capacity 1 boundary churn
    let mut cache1 = LruCache::new(1);
    assert_eq!(cache1.capacity(), 1);
    for i in 0..1_000 {
        let key = i % 2;
        cache1.put(key, i);
        assert_eq!(cache1.len(), 1);
        assert_eq!(cache1.get(key), Some(i));
        let other_key = 1 - key;
        assert_eq!(cache1.get(other_key), None);
    }
}

#[test]
fn test_adversarial_lru_eviction_order_invariance() {
    let mut cache = LruCache::new(4);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);
    cache.put(4, 40);
    // Order MRU -> LRU: [4, 3, 2, 1]

    // Read 2: Order -> [2, 4, 3, 1]
    assert_eq!(cache.get(2), Some(20));

    // Read 1: Order -> [1, 2, 4, 3]
    assert_eq!(cache.get(1), Some(10));

    // Overwrite 3: Order -> [3, 1, 2, 4]
    cache.put(3, 300);
    assert_eq!(cache.len(), 4);

    // Overwrite 4: Order -> [4, 3, 1, 2]
    cache.put(4, 400);
    assert_eq!(cache.len(), 4);

    // Insert 5: evicts LRU (2) -> Order -> [5, 4, 3, 1]
    cache.put(5, 50);
    assert_eq!(cache.len(), 4);
    assert_eq!(cache.get(2), None, "Key 2 must have been evicted");
    assert_eq!(cache.get(1), Some(10));
    assert_eq!(cache.get(3), Some(300));
    assert_eq!(cache.get(4), Some(400));
    assert_eq!(cache.get(5), Some(50));

    // After above gets:
    // get(1) -> [1, 5, 4, 3]
    // get(3) -> [3, 1, 5, 4]
    // get(4) -> [4, 3, 1, 5]
    // get(5) -> [5, 4, 3, 1]
    // Insert 6: evicts LRU (1) -> Order -> [6, 5, 4, 3]
    cache.put(6, 60);
    assert_eq!(cache.get(1), None, "Key 1 must have been evicted");
    assert_eq!(cache.get(6), Some(60));
    assert_eq!(cache.get(5), Some(50));
    assert_eq!(cache.get(4), Some(400));
    assert_eq!(cache.get(3), Some(300));
}

#[test]
fn test_adversarial_lru_high_churn_300k() {
    use std::collections::HashMap;

    const CAPACITY: usize = 64;
    const NUM_KEYS: i32 = 256;
    const TOTAL_OPS: i32 = 300_000;

    let mut cache = LruCache::new(CAPACITY);
    let mut oracle_last_put: HashMap<i32, i32> = HashMap::new();

    // Deterministic pseudo-random sequence
    let mut state: u64 = 123_456_789;
    let mut next_rand = || {
        state = state.wrapping_mul(6_364_136_223_846_793_005).wrapping_add(1);
        (state >> 33) as u32
    };

    for i in 0..TOTAL_OPS {
        let op = next_rand() % 10;
        let key = i32::try_from(next_rand() % (NUM_KEYS as u32)).unwrap();

        if op < 4 {
            // 40% get operations
            if let Some(val) = cache.get(key) {
                // If present in cache, must match the most recently put value
                assert_eq!(
                    Some(&val),
                    oracle_last_put.get(&key),
                    "Cached value for key {key} does not match oracle"
                );
            }
        } else {
            // 60% put operations
            let val = i;
            cache.put(key, val);
            oracle_last_put.insert(key, val);
        }

        assert!(cache.len() <= CAPACITY, "Length must never exceed capacity");
    }
}
