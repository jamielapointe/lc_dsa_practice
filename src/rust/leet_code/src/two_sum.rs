//! LeetCode #1: Two Sum.
//!
//! URL: <https://leetcode.com/problems/two-sum/>

use std::collections::HashMap;

/// Errors that may occur during the evaluation of `two_sum`.
#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum TwoSumError {
    /// No two distinct elements sum to the specified target.
    #[error("no two sum solution found")]
    NoSolution,
    /// The input slice is invalid (e.g. fewer than 2 elements).
    #[error("invalid input: {0}")]
    InvalidInput(String),
}

/// Finds two distinct indices whose elements sum to `target`.
///
/// Computes complements using 64-bit integer arithmetic to prevent 32-bit signed
/// overflow on boundary values.
///
/// # Errors
///
/// Returns [`TwoSumError::InvalidInput`] if `nums` contains fewer than 2 elements.
/// Returns [`TwoSumError::NoSolution`] if no pair in `nums` sums to `target`.
///
/// # Examples
///
/// ```
/// use leet_code::two_sum::two_sum;
///
/// let nums = [2, 7, 11, 15];
/// let (i, j) = two_sum(&nums, 9).unwrap();
/// assert_eq!((i, j), (0, 1));
/// ```
#[expect(
    clippy::cast_lossless,
    reason = "Explicit 64-bit widening guard prevents arithmetic overflow"
)]
pub fn two_sum(nums: &[i32], target: i32) -> Result<(usize, usize), TwoSumError> {
    if nums.len() < 2 {
        return Err(TwoSumError::InvalidInput(
            "nums must have at least 2 elements".to_string(),
        ));
    }

    let mut seen: HashMap<i64, usize> = HashMap::with_capacity(nums.len());
    for (idx, &num) in nums.iter().enumerate() {
        let complement = (target as i64) - (num as i64);
        if let Some(&prev_idx) = seen.get(&complement) {
            return Ok((prev_idx, idx));
        }
        seen.insert(num as i64, idx);
    }

    Err(TwoSumError::NoSolution)
}

/// LeetCode compatibility wrapper matching the official method signature.
pub struct Solution;

impl Solution {
    /// LeetCode submission entrypoint.
    ///
    /// # Panics
    ///
    /// This function never panics; invalid inputs return an empty vector.
    #[must_use]
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::needless_pass_by_value,
        reason = "LeetCode constraint bounds index within valid i32 range; API signature mandate"
    )]
    pub fn two_sum(nums: Vec<i32>, target: i32) -> Vec<i32> {
        match two_sum(&nums, target) {
            Ok((i, j)) => vec![i as i32, j as i32],
            Err(_) => Vec::new(),
        }
    }
}
