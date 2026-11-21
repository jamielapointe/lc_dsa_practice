//! 4-Tier integration test suite for LeetCode #88: Merge Sorted Array.

use std::collections::BTreeMap;
use std::time::{Duration, Instant};

use leet_code::merge_sorted_array::{MergeSortedArrayError, Solution, merge_sorted_array};

fn usize_to_i32(value: usize) -> i32 {
    let converted = i32::try_from(value);
    assert!(
        converted.is_ok(),
        "test length {value} does not fit into i32"
    );
    converted.unwrap_or(0)
}

fn assert_merge_case(nums1_init: &[i32], m: usize, nums2_init: &[i32], n: usize, expected: &[i32]) {
    let mut buf1 = nums1_init.to_vec();
    let res = merge_sorted_array(&mut buf1, m, nums2_init, n);
    assert_eq!(
        res,
        Ok(()),
        "merge_sorted_array failed for nums1={nums1_init:?}, m={m}, nums2={nums2_init:?}, n={n}"
    );
    assert_eq!(
        buf1, expected,
        "merge_sorted_array output mismatch for nums1={nums1_init:?}, m={m}, nums2={nums2_init:?}, n={n}"
    );

    let mut vec1 = nums1_init.to_vec();
    let mut vec2 = nums2_init.to_vec();
    Solution::merge(&mut vec1, usize_to_i32(m), &mut vec2, usize_to_i32(n));
    assert_eq!(
        vec1, expected,
        "Solution::merge output mismatch for nums1={nums1_init:?}, m={m}, nums2={nums2_init:?}, n={n}"
    );
    assert_eq!(
        vec2, nums2_init,
        "Solution::merge must not mutate nums2 for nums2={nums2_init:?}"
    );
}

fn assert_merge_error(
    nums1_init: &[i32],
    m: usize,
    nums2_init: &[i32],
    n: usize,
    expected_err: &MergeSortedArrayError,
) {
    let mut buf1 = nums1_init.to_vec();
    let res = merge_sorted_array(&mut buf1, m, nums2_init, n);
    assert_eq!(
        res.as_ref(),
        Err(expected_err),
        "expected {expected_err:?} for nums1={nums1_init:?}, m={m}, nums2={nums2_init:?}, n={n}"
    );
    assert_eq!(
        buf1, nums1_init,
        "merge_sorted_array must leave nums1 unmodified on error"
    );

    if let (Ok(m_i32), Ok(n_i32)) = (i32::try_from(m), i32::try_from(n)) {
        let mut vec1 = nums1_init.to_vec();
        let mut vec2 = nums2_init.to_vec();
        Solution::merge(&mut vec1, m_i32, &mut vec2, n_i32);
        assert_eq!(
            vec1, nums1_init,
            "Solution::merge must leave nums1 unmodified on invalid input"
        );
        assert_eq!(
            vec2, nums2_init,
            "Solution::merge must leave nums2 unmodified on invalid input"
        );
    }
}

// ===========================================================================
// Tier 1: Canonical Examples
// ===========================================================================

#[test]
fn test_canonical_examples() {
    assert_merge_case(&[1, 2, 3, 0, 0, 0], 3, &[2, 5, 6], 3, &[1, 2, 2, 3, 5, 6]);
    assert_merge_case(&[1], 1, &[], 0, &[1]);
    assert_merge_case(&[0], 0, &[1], 1, &[1]);
}

#[test]
fn test_canonical_subslice_in_place_no_reallocation() {
    let mut backing = [999, -777, 1, 2, 3, 0, 0, 0, -888, 555];
    let ptr_before = backing.as_ptr();
    let nums2 = [2, 5, 6];

    let res = merge_sorted_array(&mut backing[2..8], 3, &nums2, 3);
    assert_eq!(res, Ok(()));
    assert_eq!(backing.as_ptr(), ptr_before);
    assert_eq!(backing, [999, -777, 1, 2, 2, 3, 5, 6, -888, 555]);

    // Empty sub-slice inside a non-empty buffer must be a clean no-op.
    let empty_res = merge_sorted_array(&mut backing[4..4], 0, &[], 0);
    assert_eq!(empty_res, Ok(()));
    assert_eq!(backing, [999, -777, 1, 2, 2, 3, 5, 6, -888, 555]);
}

// ===========================================================================
// Tier 2: Boundary, Edge Cases & Preconditions
// ===========================================================================

#[test]
fn test_empty_and_one_sided_inputs() {
    assert_merge_case(&[], 0, &[], 0, &[]);
    assert_merge_case(&[0, 0, 0, 0], 0, &[-5, 0, 2, 9], 4, &[-5, 0, 2, 9]);
    assert_merge_case(&[-10, -2, 0, 7], 4, &[], 0, &[-10, -2, 0, 7]);
}

#[test]
fn test_single_element_pairs() {
    assert_merge_case(&[1, 0], 1, &[5], 1, &[1, 5]);
    assert_merge_case(&[3, 0], 1, &[3], 1, &[3, 3]);
    assert_merge_case(&[5, 0], 1, &[1], 1, &[1, 5]);
}

#[test]
fn test_disjoint_exhaustion_order_and_underflow_trap() {
    // All nums2 < nums1: exhausts nums1 first (classic usize decrement underflow trap).
    assert_merge_case(
        &[10, 20, 30, 0, 0, 0],
        3,
        &[1, 2, 3],
        3,
        &[1, 2, 3, 10, 20, 30],
    );
    assert_merge_case(
        &[100, 0, 0, 0, 0],
        1,
        &[-5, -4, -3, -2],
        4,
        &[-5, -4, -3, -2, 100],
    );
    assert_merge_case(&[10, 20, 30, 40, 0], 4, &[1], 1, &[1, 10, 20, 30, 40]);

    // All nums2 > nums1: exhausts nums2 first.
    assert_merge_case(
        &[1, 2, 3, 0, 0, 0],
        3,
        &[10, 20, 30],
        3,
        &[1, 2, 3, 10, 20, 30],
    );
    assert_merge_case(&[1, 2, 3, 4, 0], 4, &[100], 1, &[1, 2, 3, 4, 100]);
    assert_merge_case(
        &[-100, 0, 0, 0, 0],
        1,
        &[1, 2, 3, 4],
        4,
        &[-100, 1, 2, 3, 4],
    );
}

#[test]
fn test_interleaved_and_duplicate_inputs() {
    assert_merge_case(
        &[0, 2, 4, 6, 8, 0, 0, 0, 0, 0],
        5,
        &[1, 3, 5, 7, 9],
        5,
        &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    );
    assert_merge_case(
        &[1, 3, 5, 7, 9, 0, 0, 0, 0, 0],
        5,
        &[0, 2, 4, 6, 8],
        5,
        &[0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    );
    assert_merge_case(&[7, 7, 7, 0, 0], 3, &[7, 7], 2, &[7, 7, 7, 7, 7]);
    assert_merge_case(&[0, 0, 0, 0], 2, &[0, 0], 2, &[0, 0, 0, 0]);
    assert_merge_case(
        &[-2, -2, 0, 3, 3, 0, 0, 0, 0],
        5,
        &[-2, 0, 0, 3],
        4,
        &[-2, -2, -2, 0, 0, 0, 3, 3, 3],
    );
}

#[test]
fn test_negative_and_i32_extremes() {
    assert_merge_case(
        &[-1_000_000_000, -500, -1, 0, 0, 0],
        3,
        &[-1_000_000_000, -2, 1_000_000_000],
        3,
        &[-1_000_000_000, -1_000_000_000, -500, -2, -1, 1_000_000_000],
    );
    assert_merge_case(
        &[i32::MIN, 0, i32::MAX, 0, 0, 0],
        3,
        &[i32::MIN, -1, i32::MAX],
        3,
        &[i32::MIN, i32::MIN, -1, 0, i32::MAX, i32::MAX],
    );
    assert_merge_case(&[i32::MAX, 0], 1, &[i32::MIN], 1, &[i32::MIN, i32::MAX]);
    assert_merge_case(&[i32::MIN, 0], 1, &[i32::MAX], 1, &[i32::MIN, i32::MAX]);
}

#[test]
fn test_trailing_garbage_overwritten_without_unsorted_error() {
    assert_merge_case(
        &[1, 4, 9, 999, -999, i32::MAX, i32::MIN],
        3,
        &[2, 3, 5, 10],
        4,
        &[1, 2, 3, 4, 5, 9, 10],
    );
    assert_merge_case(
        &[i32::MAX, i32::MIN, 42],
        0,
        &[-10, 0, 10],
        3,
        &[-10, 0, 10],
    );
    assert_merge_case(&[10, 20, -1, -2, -3], 2, &[1, 2, 3], 3, &[1, 2, 3, 10, 20]);
}

#[test]
fn test_precondition_invalid_length_overflow_and_negative_wrapper_counts() {
    // Verify Solution::merge handles negative counts without panicking or mutating inputs.
    for (m_neg, n_neg) in [(-1, 0), (0, -1), (-1, -1), (i32::MIN, 1), (1, i32::MIN)] {
        let mut vec1 = vec![1, 2, 0];
        let mut vec2 = vec![3];
        Solution::merge(&mut vec1, m_neg, &mut vec2, n_neg);
        assert_eq!(vec1, vec![1, 2, 0]);
        assert_eq!(vec2, vec![3]);
    }

    // nums1 too short / too long / unexpected non-empty or empty.
    assert_merge_error(
        &[1, 2, 0],
        2,
        &[3, 4],
        2,
        &MergeSortedArrayError::InvalidLength,
    );
    assert_merge_error(
        &[1, 2, 0, 0, 0],
        2,
        &[3, 4],
        2,
        &MergeSortedArrayError::InvalidLength,
    );
    assert_merge_error(&[], 1, &[], 0, &MergeSortedArrayError::InvalidLength);
    assert_merge_error(&[], 0, &[1], 1, &MergeSortedArrayError::InvalidLength);
    assert_merge_error(&[0], 0, &[], 0, &MergeSortedArrayError::InvalidLength);

    // nums2 too short / too long / non-empty when n == 0.
    assert_merge_error(
        &[1, 0, 0],
        1,
        &[2],
        2,
        &MergeSortedArrayError::InvalidLength,
    );
    assert_merge_error(
        &[1, 0],
        1,
        &[2, 3],
        1,
        &MergeSortedArrayError::InvalidLength,
    );
    assert_merge_error(&[1], 1, &[2], 0, &MergeSortedArrayError::InvalidLength);

    // m + n usize::MAX overflow.
    assert_merge_error(
        &[1, 2],
        usize::MAX,
        &[3],
        1,
        &MergeSortedArrayError::InvalidLength,
    );
    assert_merge_error(
        &[1, 2],
        1,
        &[3],
        usize::MAX,
        &MergeSortedArrayError::InvalidLength,
    );
    assert_merge_error(
        &[],
        usize::MAX,
        &[],
        usize::MAX,
        &MergeSortedArrayError::InvalidLength,
    );
}

#[test]
fn test_precondition_unsorted_input_and_atomicity() {
    // Unsorted active prefix of nums1.
    assert_merge_error(
        &[3, 1, 2, 0, 0],
        3,
        &[4, 5],
        2,
        &MergeSortedArrayError::UnsortedInput,
    );
    assert_merge_error(&[2, 1], 2, &[], 0, &MergeSortedArrayError::UnsortedInput);
    assert_merge_error(
        &[i32::MAX, i32::MIN, 0],
        2,
        &[0],
        1,
        &MergeSortedArrayError::UnsortedInput,
    );

    // Unsorted nums2.
    assert_merge_error(
        &[1, 2, 0, 0],
        2,
        &[5, 3],
        2,
        &MergeSortedArrayError::UnsortedInput,
    );
    assert_merge_error(
        &[0, 0, 0],
        0,
        &[1, 3, 2],
        3,
        &MergeSortedArrayError::UnsortedInput,
    );
    assert_merge_error(
        &[0, 0],
        0,
        &[i32::MAX, i32::MIN],
        2,
        &MergeSortedArrayError::UnsortedInput,
    );

    // Both unsorted.
    assert_merge_error(
        &[3, 1, 0, 0],
        2,
        &[4, 2],
        2,
        &MergeSortedArrayError::UnsortedInput,
    );
}

#[test]
fn test_precondition_check_order_priority() {
    // Both invalid nums1 length AND unsorted inputs must report InvalidLength first.
    assert_merge_error(
        &[3, 1, 0],
        2,
        &[4, 2],
        2,
        &MergeSortedArrayError::InvalidLength,
    );
    // Both invalid nums2 length AND unsorted inputs must report InvalidLength first.
    assert_merge_error(
        &[3, 1, 0, 0],
        2,
        &[4, 2, 1],
        2,
        &MergeSortedArrayError::InvalidLength,
    );
    // Both m + n overflow AND unsorted inputs must report InvalidLength first.
    assert_merge_error(
        &[5, 1],
        usize::MAX,
        &[4, 2],
        1,
        &MergeSortedArrayError::InvalidLength,
    );
}

// ===========================================================================
// Tier 3: Algebraic Invariants & Differential Oracle
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

fn generate_sorted_vec(rng: &mut XorShift64, len: usize, mode: usize) -> Vec<i32> {
    let mut values = Vec::with_capacity(len);
    for _ in 0..len {
        let val = match mode % 4 {
            0 => rng.next_i32_range(-5, 5),
            1 => rng.next_i32_range(-10_000, 10_000),
            2 => rng.next_i32_range(-1_000_000_000, 1_000_000_000),
            _ => rng.next_i32_full(),
        };
        values.push(val);
    }
    values.sort_unstable();
    values
}

fn make_padded_buffer(rng: &mut XorShift64, active: &[i32], slack: usize) -> Vec<i32> {
    let mut buf = Vec::with_capacity(active.len() + slack);
    buf.extend_from_slice(active);
    for _ in 0..slack {
        buf.push(rng.next_i32_full());
    }
    buf
}

fn naive_merge_oracle(active1: &[i32], active2: &[i32]) -> Vec<i32> {
    let mut merged = Vec::with_capacity(active1.len() + active2.len());
    merged.extend_from_slice(active1);
    merged.extend_from_slice(active2);
    merged.sort_unstable();
    merged
}

fn multiset_counts(slice: &[i32]) -> BTreeMap<i32, usize> {
    let mut counts = BTreeMap::new();
    for &val in slice {
        *counts.entry(val).or_insert(0) += 1;
    }
    counts
}

#[test]
fn test_differential_oracle_and_multiset_invariants() {
    let mut rng = XorShift64::new(0x5eed_0088_cafe_babe);

    for trial in 0_usize..300 {
        let m = match trial % 6 {
            0 => 0,
            1 => 1,
            _ => rng.next_usize(65),
        };
        let n = match trial % 7 {
            0 => 0,
            1 => 1,
            _ => rng.next_usize(65),
        };

        let active1 = generate_sorted_vec(&mut rng, m, trial);
        let nums2 = generate_sorted_vec(&mut rng, n, trial + 1);
        let nums1_init = make_padded_buffer(&mut rng, &active1, n);

        let expected = naive_merge_oracle(&active1, &nums2);
        let expected_counts = multiset_counts(&expected);

        let mut buf1 = nums1_init.clone();
        let res = merge_sorted_array(&mut buf1, m, &nums2, n);
        assert_eq!(res, Ok(()), "trial {trial} failed (m={m}, n={n})");
        assert!(buf1.is_sorted(), "trial {trial} output is not sorted");
        assert_eq!(buf1, expected, "trial {trial} oracle mismatch");
        assert_eq!(
            multiset_counts(&buf1),
            expected_counts,
            "trial {trial} multiset mismatch"
        );

        let mut vec1 = nums1_init;
        let mut vec2 = nums2.clone();
        Solution::merge(&mut vec1, usize_to_i32(m), &mut vec2, usize_to_i32(n));
        assert_eq!(vec1, expected, "trial {trial} Solution::merge mismatch");
        assert_eq!(vec2, nums2, "trial {trial} Solution::merge mutated nums2");
    }
}

#[test]
fn test_algebraic_invariants_commutativity_idempotence_associativity() {
    let mut rng = XorShift64::new(0x0088_dead_beef_4321);

    for trial in 0_usize..80 {
        let len_a = rng.next_usize(24);
        let len_b = rng.next_usize(24);
        let len_c = rng.next_usize(24);

        let a = generate_sorted_vec(&mut rng, len_a, trial);
        let b = generate_sorted_vec(&mut rng, len_b, trial + 1);
        let c = generate_sorted_vec(&mut rng, len_c, trial + 2);

        // Commutativity: merge(A, B) == merge(B, A).
        let mut ab = make_padded_buffer(&mut rng, &a, len_b);
        let mut ba = make_padded_buffer(&mut rng, &b, len_a);
        assert_eq!(merge_sorted_array(&mut ab, len_a, &b, len_b), Ok(()));
        assert_eq!(merge_sorted_array(&mut ba, len_b, &a, len_a), Ok(()));
        assert_eq!(ab, ba, "commutativity failed at trial {trial}");

        // Idempotence with empty nums2: merge(merge(A, B), []) == merge(A, B).
        let snapshot = ab.clone();
        assert_eq!(merge_sorted_array(&mut ab, snapshot.len(), &[], 0), Ok(()));
        assert_eq!(ab, snapshot, "idempotence failed at trial {trial}");

        // Associativity: merge(merge(A, B), C) == merge(A, merge(B, C)).
        let mut lhs = make_padded_buffer(&mut rng, &ab, len_c);
        assert_eq!(merge_sorted_array(&mut lhs, ab.len(), &c, len_c), Ok(()));

        let mut bc = make_padded_buffer(&mut rng, &b, len_c);
        assert_eq!(merge_sorted_array(&mut bc, len_b, &c, len_c), Ok(()));
        let mut rhs = make_padded_buffer(&mut rng, &a, bc.len());
        assert_eq!(merge_sorted_array(&mut rhs, len_a, &bc, bc.len()), Ok(()));

        assert_eq!(lhs, rhs, "associativity failed at trial {trial}");
    }
}

// ===========================================================================
// Tier 4: Stress & OJ Simulation
// ===========================================================================

#[test]
fn test_stress_max_leetcode_constraints_200_200() {
    const M: usize = 200;
    const N: usize = 200;

    // 1. Disjoint reversed across [-10^9, 10^9].
    let active1: Vec<i32> = (0_i32..200).map(|i| 1_000_000_000 - 200 + i).collect();
    let nums2: Vec<i32> = (0_i32..200).map(|i| -1_000_000_000 + i).collect();
    let mut nums1 = active1.clone();
    nums1.resize(M + N, 0);
    let expected = naive_merge_oracle(&active1, &nums2);
    assert_merge_case(&nums1, M, &nums2, N, &expected);

    // 2. Interleaved max-constraint workload.
    let active1_even: Vec<i32> = (0_i32..200).map(|i| i * 2).collect();
    let nums2_odd: Vec<i32> = (0_i32..200).map(|i| i * 2 + 1).collect();
    let mut nums1_interleaved = active1_even;
    nums1_interleaved.resize(M + N, -1);
    let expected_interleaved: Vec<i32> = (0_i32..400).collect();
    assert_merge_case(&nums1_interleaved, M, &nums2_odd, N, &expected_interleaved);

    // 3. Dense duplicates max-constraint workload.
    let active1_dup: Vec<i32> = (0_i32..200).map(|i| (i / 20) - 5).collect();
    let nums2_dup: Vec<i32> = (0_i32..200).map(|i| (i / 20) - 5).collect();
    let mut nums1_dup = active1_dup.clone();
    nums1_dup.resize(M + N, 99);
    let expected_dup = naive_merge_oracle(&active1_dup, &nums2_dup);
    assert_merge_case(&nums1_dup, M, &nums2_dup, N, &expected_dup);
}

#[test]
fn test_stress_large_linear_50k_50k() {
    const M: usize = 50_000;
    const N: usize = 50_000;
    const BUDGET: Duration = Duration::from_millis(500);

    // Workload 1: Reversed disjoint (nums2 < nums1).
    let mut rev_nums1: Vec<i32> = (50_000_i32..100_000).collect();
    rev_nums1.resize(M + N, 0);
    let rev_nums2: Vec<i32> = (0_i32..50_000).collect();
    let expected_seq: Vec<i32> = (0_i32..100_000).collect();

    let start_rev = Instant::now();
    let res_rev = merge_sorted_array(&mut rev_nums1, M, &rev_nums2, N);
    let elapsed_rev = start_rev.elapsed();
    assert_eq!(res_rev, Ok(()));
    assert_eq!(rev_nums1, expected_seq);
    assert!(
        elapsed_rev < BUDGET,
        "reversed 50k+50k merge took {elapsed_rev:?}, exceeding {BUDGET:?}"
    );

    // Workload 2: Heavily interleaved evens and odds.
    let mut inter_nums1: Vec<i32> = (0_i32..50_000).map(|i| i * 2).collect();
    inter_nums1.resize(M + N, -42);
    let inter_nums2: Vec<i32> = (0_i32..50_000).map(|i| i * 2 + 1).collect();

    let start_inter = Instant::now();
    let res_inter = merge_sorted_array(&mut inter_nums1, M, &inter_nums2, N);
    let elapsed_inter = start_inter.elapsed();
    assert_eq!(res_inter, Ok(()));
    assert_eq!(inter_nums1, expected_seq);
    assert!(
        elapsed_inter < BUDGET,
        "interleaved 50k+50k merge took {elapsed_inter:?}, exceeding {BUDGET:?}"
    );

    // Workload 3: Dense duplicates across 20 buckets.
    let mut dup_nums1: Vec<i32> = (0_i32..50_000).map(|i| (i / 2_500) - 10).collect();
    dup_nums1.resize(M + N, 777);
    let dup_nums2: Vec<i32> = (0_i32..50_000).map(|i| (i / 2_500) - 10).collect();
    let expected_dup: Vec<i32> = (0_i32..100_000).map(|i| (i / 5_000) - 10).collect();

    let start_dup = Instant::now();
    let res_dup = merge_sorted_array(&mut dup_nums1, M, &dup_nums2, N);
    let elapsed_dup = start_dup.elapsed();
    assert_eq!(res_dup, Ok(()));
    assert_eq!(dup_nums1, expected_dup);
    assert!(
        elapsed_dup < BUDGET,
        "duplicate 50k+50k merge took {elapsed_dup:?}, exceeding {BUDGET:?}"
    );
}

struct OjCase<'a> {
    nums1: &'a [i32],
    m: i32,
    nums2: &'a [i32],
    n: i32,
    expected: &'a [i32],
}

#[test]
fn test_oj_simulation_table() {
    let oj_cases = [
        OjCase {
            nums1: &[1, 2, 3, 0, 0, 0],
            m: 3,
            nums2: &[2, 5, 6],
            n: 3,
            expected: &[1, 2, 2, 3, 5, 6],
        },
        OjCase {
            nums1: &[1],
            m: 1,
            nums2: &[],
            n: 0,
            expected: &[1],
        },
        OjCase {
            nums1: &[0],
            m: 0,
            nums2: &[1],
            n: 1,
            expected: &[1],
        },
        OjCase {
            nums1: &[2, 0],
            m: 1,
            nums2: &[1],
            n: 1,
            expected: &[1, 2],
        },
        OjCase {
            nums1: &[4, 5, 6, 0, 0, 0],
            m: 3,
            nums2: &[1, 2, 3],
            n: 3,
            expected: &[1, 2, 3, 4, 5, 6],
        },
        OjCase {
            nums1: &[-1, 0, 0, 3, 3, 3, 0, 0, 0],
            m: 6,
            nums2: &[1, 2, 2],
            n: 3,
            expected: &[-1, 0, 0, 1, 2, 2, 3, 3, 3],
        },
        OjCase {
            nums1: &[-1, -1, 0, 0, 0, 0],
            m: 4,
            nums2: &[-1, 0],
            n: 2,
            expected: &[-1, -1, -1, 0, 0, 0],
        },
        OjCase {
            nums1: &[0, 0, 3, 0, 0, 0, 0, 0, 0],
            m: 3,
            nums2: &[-1, 1, 1, 1, 2, 3],
            n: 6,
            expected: &[-1, 0, 0, 1, 1, 1, 2, 3, 3],
        },
    ];

    for case in oj_cases {
        let mut vec1 = case.nums1.to_vec();
        let mut vec2 = case.nums2.to_vec();
        Solution::merge(&mut vec1, case.m, &mut vec2, case.n);
        assert_eq!(
            vec1, case.expected,
            "OJ simulation mismatch for nums1={:?}, m={}, nums2={:?}, n={}",
            case.nums1, case.m, case.nums2, case.n
        );
        assert_eq!(vec2, case.nums2, "OJ simulation mutated nums2");
    }
}
