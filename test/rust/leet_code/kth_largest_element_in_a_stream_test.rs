//! 4-Tier integration test suite for LeetCode #703: Kth Largest Element in a Stream.

use std::time::{Duration, Instant};

use leet_code::kth_largest_element_in_a_stream::{KthLargest, KthLargestError, KthLargestStream};

fn assert_idiomatic_sequence(
    k: usize,
    init: &[i32],
    steps: &[(i32, i32)],
) -> Result<(), KthLargestError> {
    let mut stream = KthLargestStream::new(k, init)?;
    assert_eq!(stream.k(), k, "configured k mismatch");
    assert_eq!(
        stream.len(),
        init.len().min(k),
        "initial retained length must be min(init.len(), k)"
    );
    assert_eq!(
        stream.is_empty(),
        init.is_empty(),
        "is_empty mismatch after construction"
    );

    for (idx, &(val, expected)) in steps.iter().enumerate() {
        let got = stream.add(val);
        assert_eq!(
            got, expected,
            "KthLargestStream::add({val}) mismatch at step {idx} (k={k}, init={init:?})"
        );
        assert_eq!(
            stream.len(),
            k,
            "retained length must equal k after add at step {idx}"
        );
        assert!(
            !stream.is_empty(),
            "stream cannot be empty after add at step {idx}"
        );
    }
    Ok(())
}

fn assert_wrapper_sequence(k: i32, init: &[i32], steps: &[(i32, i32)]) {
    let mut kth = KthLargest::new(k, init.to_vec());
    for (idx, &(val, expected)) in steps.iter().enumerate() {
        let got = kth.add(val);
        assert_eq!(
            got, expected,
            "KthLargest::add({val}) mismatch at step {idx} (k={k}, init={init:?})"
        );
    }
}

// ===========================================================================
// Tier 1: Canonical Examples
// ===========================================================================

#[test]
fn test_canonical_example_1_idiomatic() -> Result<(), KthLargestError> {
    let init = [4, 5, 8, 2];
    let steps = [(3, 4), (5, 5), (10, 5), (9, 8), (4, 8)];
    assert_idiomatic_sequence(3, &init, &steps)
}

#[test]
fn test_canonical_example_1_leetcode_wrapper() {
    let init = [4, 5, 8, 2];
    let steps = [(3, 4), (5, 5), (10, 5), (9, 8), (4, 8)];
    assert_wrapper_sequence(3, &init, &steps);
}

#[test]
fn test_canonical_example_2_idiomatic() -> Result<(), KthLargestError> {
    let init = [7, 7, 7, 7, 8, 3];
    let steps = [(2, 7), (10, 7), (9, 7), (9, 8)];
    assert_idiomatic_sequence(4, &init, &steps)
}

#[test]
fn test_canonical_example_2_leetcode_wrapper() {
    let init = [7, 7, 7, 7, 8, 3];
    let steps = [(2, 7), (10, 7), (9, 7), (9, 8)];
    assert_wrapper_sequence(4, &init, &steps);
}

// ===========================================================================
// Tier 2: Boundary, Starved Init, Extremes & Preconditions
// ===========================================================================

#[test]
fn test_starved_initial_stream_k1_empty_and_k3_two_elements() -> Result<(), KthLargestError> {
    // k = 1, nums = &[] (len == k - 1 == 0: initially empty, populated on first add).
    let k1_steps = [(-42, -42), (-10, -10), (-50, -10), (7, 7), (7, 7)];
    assert_idiomatic_sequence(1, &[], &k1_steps)?;

    // k = 3, nums = &[10, 20] (len == k - 1 == 2: reaches k elements on first add).
    let k3_steps = [(5, 5), (15, 10), (25, 15), (15, 15), (30, 20)];
    assert_idiomatic_sequence(3, &[10, 20], &k3_steps)
}

#[test]
fn test_starved_initial_stream_leetcode_wrapper() {
    let k1_steps = [(-42, -42), (-10, -10), (-50, -10), (7, 7)];
    assert_wrapper_sequence(1, &[], &k1_steps);

    let k3_steps = [(5, 5), (15, 10), (25, 15), (15, 15), (30, 20)];
    assert_wrapper_sequence(3, &[10, 20], &k3_steps);
}

#[test]
fn test_k_equals_one_running_maximum_and_k_equals_n_plus_one_running_minimum()
-> Result<(), KthLargestError> {
    // k = 1 tracks running maximum across initial + added scores.
    let running_max_init = [-5, 3, -1, 8, 2];
    let running_max_steps = [(4, 8), (8, 8), (12, 12), (-100, 12), (100, 100)];
    assert_idiomatic_sequence(1, &running_max_init, &running_max_steps)?;

    // k = n + 1 returns overall minimum on the first add, then shifts upward.
    let full_pool_init = [40, 10, 30, 20];
    let full_pool_steps = [(25, 10), (5, 10), (35, 20), (50, 25), (15, 25)];
    assert_idiomatic_sequence(5, &full_pool_init, &full_pool_steps)
}

#[test]
fn test_k1_and_kn_plus_1_leetcode_wrapper() {
    assert_wrapper_sequence(
        1,
        &[-5, 3, -1, 8, 2],
        &[(4, 8), (8, 8), (12, 12), (-100, 12), (100, 100)],
    );
    assert_wrapper_sequence(
        5,
        &[40, 10, 30, 20],
        &[(25, 10), (5, 10), (35, 20), (50, 25), (15, 25)],
    );
}

#[test]
fn test_all_negative_and_all_identical_duplicates() -> Result<(), KthLargestError> {
    let neg_init = [-100, -50, -200, -300];
    let neg_steps = [(-150, -100), (-75, -75), (-50, -50), (-10, -50), (-5, -10)];
    assert_idiomatic_sequence(2, &neg_init, &neg_steps)?;

    let dup_pos_init = [7, 7, 7, 7];
    let dup_pos_steps = [(7, 7), (6, 7), (8, 7), (8, 7), (8, 8)];
    assert_idiomatic_sequence(3, &dup_pos_init, &dup_pos_steps)?;

    let dup_neg_init = [-9, -9];
    let dup_neg_steps = [(-9, -9), (-10, -9), (-8, -9), (-8, -8)];
    assert_idiomatic_sequence(2, &dup_neg_init, &dup_neg_steps)
}

#[test]
fn test_all_negative_and_duplicates_leetcode_wrapper() {
    assert_wrapper_sequence(
        2,
        &[-100, -50, -200, -300],
        &[(-150, -100), (-75, -75), (-50, -50), (-10, -50), (-5, -10)],
    );
    assert_wrapper_sequence(3, &[7, 7, 7, 7], &[(7, 7), (6, 7), (8, 7), (8, 7), (8, 8)]);
}

#[test]
fn test_i32_min_and_max_extremes() -> Result<(), KthLargestError> {
    // Detects comparator subtraction overflow traps (i32::MAX - i32::MIN).
    let init = [i32::MIN, i32::MAX];
    let steps = [
        (i32::MIN, i32::MIN),
        (-1, -1),
        (0, 0),
        (-500, 0),
        (i32::MAX, i32::MAX),
    ];
    assert_idiomatic_sequence(2, &init, &steps)?;

    let starved_steps = [(i32::MIN, i32::MIN), (0, 0), (i32::MAX, i32::MAX)];
    assert_idiomatic_sequence(1, &[], &starved_steps)
}

#[test]
fn test_i32_min_and_max_extremes_leetcode_wrapper() {
    assert_wrapper_sequence(
        2,
        &[i32::MIN, i32::MAX],
        &[
            (i32::MIN, i32::MIN),
            (-1, -1),
            (0, 0),
            (-500, 0),
            (i32::MAX, i32::MAX),
        ],
    );
}

#[test]
fn test_precondition_errors_and_validation_priority() {
    // LeetCode wrapper must never panic on non-positive k or insufficient initial elements.
    for bad_k in [0, -1, -100, i32::MIN] {
        let mut wrapper = KthLargest::new(bad_k, vec![1, 2, 3]);
        assert_eq!(wrapper.add(10), 0);
    }
    let mut starved_wrapper = KthLargest::new(5, vec![1, 2]);
    assert_eq!(starved_wrapper.add(10), 0);

    // Error display formatting checks.
    assert_eq!(
        KthLargestError::InvalidK.to_string(),
        "k must be at least 1"
    );
    assert_eq!(
        KthLargestError::InsufficientInitialElements.to_string(),
        "initial slice must contain at least k - 1 elements"
    );

    // Priority 1: k == 0 MUST return InvalidK even when nums is empty.
    assert_eq!(
        KthLargestStream::new(0, &[]).err(),
        Some(KthLargestError::InvalidK)
    );
    assert_eq!(
        KthLargestStream::new(0, &[1, 2, 3]).err(),
        Some(KthLargestError::InvalidK)
    );

    // Priority 2: nums.len() + 1 < k MUST return InsufficientInitialElements.
    assert_eq!(
        KthLargestStream::new(2, &[]).err(),
        Some(KthLargestError::InsufficientInitialElements)
    );
    assert_eq!(
        KthLargestStream::new(5, &[1, 2, 3]).err(),
        Some(KthLargestError::InsufficientInitialElements)
    );
    assert_eq!(
        KthLargestStream::new(usize::MAX, &[1, 2, 3]).err(),
        Some(KthLargestError::InsufficientInitialElements)
    );
}

// ===========================================================================
// Tier 3: Invariants & Differential Sorted-Oracle Verification
// ===========================================================================

struct XorShift64 {
    state: u64,
}

impl XorShift64 {
    const fn new(seed: u64) -> Self {
        Self {
            state: if seed == 0 {
                0x9e37_79b9_7f4a_7c15
            } else {
                seed
            },
        }
    }

    const fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }

    fn next_usize(&mut self, upper_exclusive: usize) -> usize {
        let span = u64::try_from(upper_exclusive).unwrap_or(1).max(1);
        usize::try_from(self.next_u64() % span).unwrap_or(0)
    }

    fn next_i32_range(&mut self, low: i32, high_inclusive: i32) -> i32 {
        let low64 = i64::from(low);
        let high64 = i64::from(high_inclusive);
        let span = u64::try_from(high64 - low64 + 1).unwrap_or(1);
        let offset = i64::try_from(self.next_u64() % span).unwrap_or(0);
        i32::try_from(low64 + offset).unwrap_or(low)
    }

    const fn next_i32_full(&mut self) -> i32 {
        let bits = self.next_u64().to_le_bytes();
        i32::from_le_bytes([bits[0], bits[1], bits[2], bits[3]])
    }
}

fn sample_score(rng: &mut XorShift64, mode: usize) -> i32 {
    match mode % 4 {
        0 => rng.next_i32_range(-5, 5),
        1 => rng.next_i32_range(-10_000, 10_000),
        2 => rng.next_i32_range(-1_000_000_000, 1_000_000_000),
        _ => rng.next_i32_full(),
    }
}

#[test]
fn test_input_immutability_and_clone_independence() -> Result<(), KthLargestError> {
    let original = vec![8, 2, 5, 4];
    let snapshot = original.clone();
    let ptr_before = original.as_ptr();

    let mut stream = KthLargestStream::new(3, &original)?;
    assert_eq!(
        original, snapshot,
        "constructor must not mutate input slice"
    );
    assert_eq!(original.as_ptr(), ptr_before);

    let mut cloned_stream = stream.clone();
    assert_eq!(cloned_stream.add(100), 5);
    assert_eq!(cloned_stream.add(200), 8);

    // Original stream must remain unaffected by mutations on its clone.
    assert_eq!(stream.add(3), 4);
    assert_eq!(stream.add(5), 5);
    Ok(())
}

#[test]
fn test_differential_sorted_oracle_monotonicity_and_bounded_len() -> Result<(), KthLargestError> {
    let mut rng = XorShift64::new(0x0703_cafe_babe_d00d);

    for trial in 0_usize..150 {
        let k = 1 + rng.next_usize(32);
        let extra = match trial % 5 {
            0 => 0, // Starved boundary: init.len() == k - 1
            1 => 1, // Exact k initial elements
            _ => rng.next_usize(48),
        };
        let init_len = (k - 1) + extra;

        let mut init = Vec::with_capacity(init_len);
        for _ in 0..init_len {
            init.push(sample_score(&mut rng, trial));
        }

        let mut oracle_pool = init.clone();
        let mut stream = KthLargestStream::new(k, &init)?;
        let k_i32 = i32::try_from(k).unwrap_or(1);
        let mut wrapper = KthLargest::new(k_i32, init.clone());

        assert_eq!(stream.k(), k);
        assert_eq!(stream.len(), init_len.min(k));

        let mut prev_kth: Option<i32> = None;
        for step in 0_usize..40 {
            let val = sample_score(&mut rng, trial + step);
            oracle_pool.push(val);
            oracle_pool.sort_unstable();
            let expected = oracle_pool[oracle_pool.len() - k];

            let got_stream = stream.add(val);
            let got_wrapper = wrapper.add(val);

            assert_eq!(
                got_stream, expected,
                "trial {trial} step {step}: stream mismatch (k={k})"
            );
            assert_eq!(
                got_wrapper, expected,
                "trial {trial} step {step}: wrapper mismatch (k={k})"
            );
            assert_eq!(
                stream.len(),
                k,
                "trial {trial} step {step}: bounded len() <= k violated"
            );
            if let Some(prev) = prev_kth {
                assert!(
                    got_stream >= prev,
                    "trial {trial} step {step}: non-decreasing invariant violated ({got_stream} < {prev})"
                );
            }
            prev_kth = Some(got_stream);
        }
    }
    Ok(())
}

// ===========================================================================
// Tier 4: Programmatic Max-Constraint Stress (n = 10_000, k = 5_000, q = 10_000)
// ===========================================================================

#[test]
fn test_stress_max_constraints_10k_init_5k_k_10k_adds() -> Result<(), KthLargestError> {
    const N: usize = 10_000;
    const K: usize = 5_000;
    const PAIRS: i32 = 5_000; // 2 * PAIRS == 10_000 add calls
    const BUDGET: Duration = Duration::from_secs(1);

    // Deterministic permutation of -9_999..=0 (gcd(7_919, 10_000) == 1).
    // Initial top-5_000 elements are exactly -4_999..=0 (minimum = -4_999).
    let init: Vec<i32> = (0_i32..10_000)
        .map(|i| ((i * 7_919) % 10_000) - 9_999)
        .collect();
    assert_eq!(init.len(), N);

    let start = Instant::now();
    let mut stream = KthLargestStream::new(K, &init)?;
    assert_eq!(stream.len(), K);

    for m in 0..PAIRS {
        // Even step: add(-10_000) is rejected in O(1); kth largest stays -4_999 + m.
        assert_eq!(stream.add(-10_000), -4_999 + m);
        assert_eq!(stream.len(), K);

        // Odd step: add(m + 1) evicts -4_999 + m; top-5_000 becomes (-4_998 + m)..=(m + 1).
        assert_eq!(stream.add(m + 1), -4_998 + m);
        assert_eq!(stream.len(), K);
    }
    let elapsed = start.elapsed();

    assert!(
        elapsed < BUDGET,
        "max-constraint 10k init + 10k add stream took {elapsed:?}, exceeding {BUDGET:?}"
    );
    Ok(())
}

#[test]
fn test_stress_max_constraints_leetcode_wrapper() {
    const K: i32 = 5_000;
    const PAIRS: i32 = 5_000;
    const BUDGET: Duration = Duration::from_secs(1);

    let init: Vec<i32> = (0_i32..10_000)
        .map(|i| ((i * 7_919) % 10_000) - 9_999)
        .collect();

    let start = Instant::now();
    let mut kth = KthLargest::new(K, init);
    for m in 0..PAIRS {
        assert_eq!(kth.add(-10_000), -4_999 + m);
        assert_eq!(kth.add(m + 1), -4_998 + m);
    }
    let elapsed = start.elapsed();

    assert!(
        elapsed < BUDGET,
        "wrapper max-constraint stress took {elapsed:?}, exceeding {BUDGET:?}"
    );
}
