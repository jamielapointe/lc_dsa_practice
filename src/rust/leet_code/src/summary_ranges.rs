//! LeetCode #228: Summary Ranges (Easy).
//!
//! URL: <https://leetcode.com/problems/summary-ranges/>
//!
//! # Problem Description
//!
//! You are given a **sorted unique** integer array `nums`.
//!
//! A **range** `[a, b]` is the set of all integers from `a` to `b` (inclusive).
//!
//! Return the smallest sorted list of ranges that cover all the numbers in the
//! array exactly. That is, each element of `nums` is covered by exactly one of
//! the ranges, and there is no integer `x` such that `x` is in one of the
//! ranges but not in `nums`.
//!
//! Each range `[a, b]` in the list should be output as:
//! - `"a->b"` if `a != b`
//! - `"a"` if `a == b`
//!
//! # Constraints
//!
//! - `0 <= nums.length <= 20`
//! - `-2^31 <= nums[i] <= 2^31 - 1`
//! - All the values of `nums` are **unique**.
//! - `nums` is sorted in ascending order.
//!
//! # Complexity Target
//!
//! - Time: `O(n)` single linear pass over `nums`
//! - Auxiliary space: `O(1)` excluding the returned `Vec<String>`

/// Errors that may occur during the evaluation of [`summary_ranges`].
#[must_use]
#[non_exhaustive]
#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum SummaryRangesError {
    /// Input slice is not strictly ascending (contains duplicates or unsorted values).
    #[error("input slice must be strictly ascending without duplicates")]
    NotStrictlyAscending,
}

#[must_use]
fn format_range(start: i32, end: i32) -> String {
    if start == end {
        start.to_string()
    } else {
        format!("{start}->{end}")
    }
}

/// Returns the smallest sorted list of ranges covering all integers in `nums`.
///
/// Each contiguous sequence `[a, b]` of consecutive integers is formatted as
/// `"a->b"` when `a != b`, or `"a"` when `a == b`.
///
/// # Errors
///
/// Returns [`SummaryRangesError::NotStrictlyAscending`] if `nums` is not
/// strictly ascending (contains duplicates or out-of-order elements).
/// Returns [`SummaryRangesError::NotImplemented`] while this function remains a stub.
///
/// # Examples
///
/// ```no_run
/// # fn main() -> Result<(), Box<dyn std::error::Error>> {
/// use leet_code::summary_ranges::summary_ranges;
///
/// let nums = [0, 1, 2, 4, 5, 7];
/// let ranges = summary_ranges(&nums)?;
/// assert_eq!(ranges, vec!["0->2", "4->5", "7"]);
/// # Ok(())
/// # }
/// ```
pub fn summary_ranges(nums: &[i32]) -> Result<Vec<String>, SummaryRangesError> {
    let Some((&first, rest)) = nums.split_first() else {
        return Ok(Vec::new());
    };
    let mut ranges = Vec::new();
    let mut start_num_seq = first;
    let mut prev_num = first;
    for &next_num in rest {
        if next_num <= prev_num {
            return Err(SummaryRangesError::NotStrictlyAscending);
        }
        if prev_num.checked_add(1) != Some(next_num) {
            ranges.push(format_range(start_num_seq, prev_num));
            start_num_seq = next_num;
        }
        prev_num = next_num;
    }
    ranges.push(format_range(start_num_seq, prev_num));
    Ok(ranges)
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
        clippy::needless_pass_by_value,
        reason = "Matches official LeetCode Vec<i32> signature"
    )]
    pub fn summary_ranges(nums: Vec<i32>) -> Vec<String> {
        summary_ranges(&nums).unwrap_or_default()
    }
}
