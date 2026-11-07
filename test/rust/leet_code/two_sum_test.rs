#![allow(clippy::disallowed_methods, clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! 4-Tier integration test suite for LeetCode #1: Two Sum.

use leet_code::two_sum::{Solution, TwoSumError, two_sum};

// ===========================================================================
// Tier 1: Canonical Examples
// ===========================================================================

#[test]
fn test_canonical_example_1() {
    let nums = [2, 7, 11, 15];
    assert_eq!(two_sum(&nums, 9), Ok((0, 1)));
}

#[test]
fn test_canonical_example_2() {
    let nums = [3, 2, 4];
    assert_eq!(two_sum(&nums, 6), Ok((1, 2)));
}

#[test]
fn test_canonical_example_3() {
    let nums = [3, 3];
    assert_eq!(two_sum(&nums, 6), Ok((0, 1)));
}

#[test]
fn test_solution_wrapper_canonical() {
    let nums = vec![2, 7, 11, 15];
    let result = Solution::two_sum(nums, 9);
    assert_eq!(result, vec![0, 1]);
}

// ===========================================================================
// Tier 2: Boundary & Corner Cases
// ===========================================================================

#[test]
fn test_empty_input_returns_invalid_input() {
    let nums = [];
    let Err(TwoSumError::InvalidInput(_)) = two_sum(&nums, 0) else {
        panic!("expected InvalidInput");
    };
}

#[test]
fn test_single_element_returns_invalid_input() {
    let nums = [42];
    let Err(TwoSumError::InvalidInput(_)) = two_sum(&nums, 42) else {
        panic!("expected InvalidInput");
    };
}

#[test]
fn test_no_solution_found() {
    let nums = [1, 2, 3];
    assert_eq!(two_sum(&nums, 10), Err(TwoSumError::NoSolution));
}

#[test]
fn test_overflow_protection_large_negative_target() {
    // Target = i32::MIN (-2147483648), num = 1 -> complement = -2147483649 (overflows i32)
    let nums = [1, 100, 200];
    let target = i32::MIN;
    assert_eq!(two_sum(&nums, target), Err(TwoSumError::NoSolution));
}

#[test]
fn test_overflow_protection_large_positive_target() {
    // Target = i32::MAX (2147483647), num = -1 -> complement = 2147483648 (overflows i32)
    let nums = [-1, 100, 200];
    let target = i32::MAX;
    assert_eq!(two_sum(&nums, target), Err(TwoSumError::NoSolution));
}

#[test]
fn test_large_opposites_sum_to_zero() {
    let nums = [-1_000_000_000, 1_000_000_000];
    assert_eq!(two_sum(&nums, 0), Ok((0, 1)));
}

#[test]
fn test_negative_numbers() {
    let nums = [-3, 4, 3, 90];
    assert_eq!(two_sum(&nums, 0), Ok((0, 2)));
}

// ===========================================================================
// Tier 3: Invariants & Properties
// ===========================================================================

#[test]
fn test_indices_ordering_and_sum_invariant() {
    let nums = [10, 20, -5, 35, 7, 13];
    let target = 30;
    let Ok((i, j)) = two_sum(&nums, target) else {
        panic!("expected valid solution");
    };
    assert!(i < j, "Index i ({i}) must be strictly less than j ({j})");
    assert_eq!(nums[i] + nums[j], target, "Sum of elements must match target");
}

#[test]
fn test_solution_wrapper_no_solution_returns_empty_vec() {
    let nums = vec![1, 2, 3];
    let res = Solution::two_sum(nums, 100);
    assert_eq!(res, Vec::<i32>::new());
}

#[test]
fn test_solution_wrapper_invalid_input_returns_empty_vec() {
    let nums = vec![1];
    let res = Solution::two_sum(nums, 1);
    assert_eq!(res, Vec::<i32>::new());
}

// ===========================================================================
// Tier 4: Stress / High-Constraint Tests
// ===========================================================================

#[test]
fn test_stress_10k_elements() {
    const N: usize = 10_000;
    let mut nums = vec![0; N];
    nums[0] = 1_000_000;
    nums[N - 1] = 2_000_000;

    let target = 3_000_000;
    let Ok((i, j)) = two_sum(&nums, target) else {
        panic!("expected solution for stress 10k");
    };
    assert_eq!((i, j), (0, N - 1));
}

// ===========================================================================
// ListNode Canonical & Invariant Tests
// ===========================================================================

#[test]
fn test_list_node_constructors_and_helpers() {
    use leet_code::list_node::{ListNode, create_linked_list, linked_list_to_vec};

    let default_node = ListNode::default();
    assert_eq!(default_node.val, 0);
    assert!(default_node.next.is_none());

    let node = ListNode::new(42);
    assert_eq!(node.val, 42);
    assert!(node.next.is_none());

    let values = [1, 2, 3, 4, 5];
    let head = create_linked_list(&values);
    assert!(head.is_some());
    let converted = linked_list_to_vec(head.as_deref());
    assert_eq!(converted, values);
}

#[test]
fn test_list_node_empty_cases() {
    use leet_code::list_node::{create_linked_list, linked_list_to_vec};

    assert!(create_linked_list(&[]).is_none());
    assert_eq!(linked_list_to_vec(None), Vec::<i32>::new());
}

#[test]
fn test_list_node_deep_stack_safe_drop() {
    use leet_code::list_node::create_linked_list;

    // 50k nodes dropped iteratively without stack overflow
    let values = vec![1; 50_000];
    let head = create_linked_list(&values);
    assert!(head.is_some());
    drop(head);
}

// ===========================================================================
// Tier 5: Adversarial Stress & Challenger Harness
// ===========================================================================

#[test]
fn test_adversarial_two_sum_arithmetic_extremes() {
    // i32::MAX + i32::MAX
    let nums_max = [i32::MAX, i32::MAX];
    assert_eq!(two_sum(&nums_max, i32::MAX), Err(TwoSumError::NoSolution));
    assert_eq!(two_sum(&nums_max, -2), Err(TwoSumError::NoSolution));

    // Target i32::MAX with valid pair summing to i32::MAX (2147483647)
    let nums_sum_max = [1_073_741_823, 1_073_741_824];
    assert_eq!(two_sum(&nums_sum_max, i32::MAX), Ok((0, 1)));

    // i32::MIN + i32::MIN
    let nums_min = [i32::MIN, i32::MIN];
    assert_eq!(two_sum(&nums_min, i32::MIN), Err(TwoSumError::NoSolution));
    assert_eq!(two_sum(&nums_min, 0), Err(TwoSumError::NoSolution));

    // Target i32::MIN with valid pair summing to i32::MIN (-2147483648)
    let nums_sum_min = [-1_073_741_824, -1_073_741_824];
    assert_eq!(two_sum(&nums_sum_min, i32::MIN), Ok((0, 1)));

    // Target i32::MIN with value 1
    let nums_target_min_val_1 = [1, 2, 3, 4];
    assert_eq!(two_sum(&nums_target_min_val_1, i32::MIN), Err(TwoSumError::NoSolution));

    // Target i32::MAX with value -1
    let nums_target_max_val_neg1 = [-1, 2, 3, 4];
    assert_eq!(
        two_sum(&nums_target_max_val_neg1, i32::MAX),
        Err(TwoSumError::NoSolution)
    );

    // Boundary with 0
    let nums_zero_min = [0, i32::MIN];
    assert_eq!(two_sum(&nums_zero_min, i32::MIN), Ok((0, 1)));

    let nums_zero_max = [0, i32::MAX];
    assert_eq!(two_sum(&nums_zero_max, i32::MAX), Ok((0, 1)));

    // Opposite extremes summing to -1
    let nums_min_max = [i32::MIN, i32::MAX];
    assert_eq!(two_sum(&nums_min_max, -1), Ok((0, 1)));
}

#[test]
fn test_adversarial_two_sum_large_inputs_50k() {
    const N: usize = 50_000;
    let mut nums = vec![1; N];
    nums[0] = 500_000;
    nums[N - 1] = 700_000;
    assert_eq!(two_sum(&nums, 1_200_000), Ok((0, N - 1)));

    // In the middle
    let mut nums_mid = vec![0; N];
    nums_mid[24_999] = 12_345;
    nums_mid[25_000] = 54_321;
    assert_eq!(two_sum(&nums_mid, 66_666), Ok((24_999, 25_000)));

    // No solution among 50k
    let nums_no_sol = vec![2; N];
    assert_eq!(two_sum(&nums_no_sol, 5), Err(TwoSumError::NoSolution));
}

#[test]
fn test_adversarial_two_sum_rejection_and_duplicates() {
    assert_eq!(
        two_sum(&[], 0),
        Err(TwoSumError::InvalidInput(
            "nums must have at least 2 elements".to_string()
        ))
    );
    assert_eq!(
        two_sum(&[42], 42),
        Err(TwoSumError::InvalidInput(
            "nums must have at least 2 elements".to_string()
        ))
    );

    // Duplicates with no solution
    let nums_dup_no_sol = [7, 7, 7, 7];
    assert_eq!(two_sum(&nums_dup_no_sol, 15), Err(TwoSumError::NoSolution));

    // Duplicates with solution
    let nums_dup_sol = [7, 7];
    assert_eq!(two_sum(&nums_dup_sol, 14), Ok((0, 1)));

    // Multiple duplicate pairs
    let nums_multi_dup = [3, 1, 3, 3];
    assert_eq!(two_sum(&nums_multi_dup, 6), Ok((0, 2)));

    // LeetCode wrapper on rejection cases
    assert_eq!(Solution::two_sum(vec![], 0), Vec::<i32>::new());
    assert_eq!(Solution::two_sum(vec![42], 42), Vec::<i32>::new());
    assert_eq!(Solution::two_sum(vec![1, 2, 3], 100), Vec::<i32>::new());
}

#[test]
fn test_adversarial_list_node_deep_drop_100k_and_250k() {
    use leet_code::list_node::{create_linked_list, linked_list_to_vec};

    // 100k nodes iterative drop
    let head_100k = create_linked_list(&vec![1; 100_000]);
    assert!(head_100k.is_some());
    drop(head_100k);

    // 250k nodes iterative drop
    let head_250k = create_linked_list(&vec![2; 250_000]);
    assert!(head_250k.is_some());
    drop(head_250k);

    // 50k roundtrip conversion
    let original = vec![7; 50_000];
    let head_mid_scale = create_linked_list(&original);
    let converted = linked_list_to_vec(head_mid_scale.as_deref());
    assert_eq!(converted.len(), 50_000);
    assert_eq!(converted[0], 7);
    assert_eq!(converted[49_999], 7);
}
