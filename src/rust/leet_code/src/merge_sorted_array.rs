//! LeetCode #88: Merge Sorted Array (Easy).
//!
//! URL: <https://leetcode.com/problems/merge-sorted-array/>
//!
//! # Problem Description
//!
//! You are given two integer arrays `nums1` and `nums2`, sorted in
//! **non-decreasing order**, and two integers `m` and `n`, representing the
//! number of elements in `nums1` and `nums2` respectively.
//!
//! Merge `nums1` and `nums2` into a single array sorted in **non-decreasing
//! order**.
//!
//! The final sorted array should not be returned by the function, but instead
//! be stored inside `nums1`. To accommodate this, `nums1` has length `m + n`,
//! where the first `m` elements denote the elements that should be merged, and
//! the last `n` elements are set to `0` and should be ignored. `nums2` has
//! length `n`.
//!
//! # Examples
//!
//! 1. `nums1 = [1, 2, 3, 0, 0, 0], m = 3, nums2 = [2, 5, 6], n = 3`
//!    -> `[1, 2, 2, 3, 5, 6]`
//! 2. `nums1 = [1], m = 1, nums2 = [], n = 0` -> `[1]`
//! 3. `nums1 = [0], m = 0, nums2 = [1], n = 1` -> `[1]`
//!
//! # Constraints
//!
//! - `nums1.length == m + n`
//! - `nums2.length == n`
//! - `0 <= m, n <= 200`
//! - `1 <= m + n <= 200`
//! - `-10^9 <= nums1[i], nums2[j] <= 10^9`
//!
//! # Complexity Target
//!
//! - Time: `O(m + n)` single reverse two-pointer pass
//! - Auxiliary space: `O(1)` in-place with zero heap allocation
//!
//! # Preconditions
//!
//! - `m == 0 && n == 0` with empty `nums1` and `nums2` is vacuously valid and
//!   returns `Ok(())`.
//! - Trailing buffer slots `nums1[m..m + n]` may hold `0` or arbitrary
//!   sentinel values; they are ignored as data and overwritten cleanly.
//! - `nums2` is borrowed immutably as `&[i32]` and is never mutated.
//! - Full `i32` range (`i32::MIN..=i32::MAX`) is supported without overflow.
//! - Precondition checks execute in deterministic order: length/overflow first
//!   ([`MergeSortedArrayError::InvalidLength`]), then sortedness of active
//!   prefixes `nums1[..m]` and `nums2` ([`MergeSortedArrayError::UnsortedInput`]).
//! - On any error, `nums1` remains unmodified.

/// Errors that may occur during the evaluation of [`merge_sorted_array`].
#[must_use]
#[non_exhaustive]
#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum MergeSortedArrayError {
    /// Buffer lengths do not satisfy `nums1.len() == m + n` and `nums2.len() == n`
    /// (or `m + n` overflows `usize`).
    #[error("buffer lengths must satisfy nums1.len() == m + n and nums2.len() == n")]
    InvalidLength,
    /// Active slice `nums1[..m]` or `nums2[..n]` is not sorted in non-decreasing order.
    #[error("active input slices must be sorted in non-decreasing order")]
    UnsortedInput,
    /// Function stub has not yet been implemented.
    #[error("merge_sorted_array is not yet implemented")]
    NotImplemented,
}

/// Merges two non-decreasing integer slices in place into `nums1`.
///
/// The first `m` elements of `nums1` and all `n` elements of `nums2` are merged
/// in non-decreasing order into `nums1[..m + n]`. Trailing buffer slots
/// `nums1[m..m + n]` are ignored as data and overwritten cleanly.
///
/// An empty merge (`m == 0` and `n == 0` with empty slices) is vacuously valid
/// and succeeds as a no-op. On any error, `nums1` remains unmodified.
///
/// # Errors
///
/// Preconditions are checked in deterministic order:
///
/// 1. Returns [`MergeSortedArrayError::InvalidLength`] if `m + n` overflows
///    `usize`, `nums1.len() != m + n`, or `nums2.len() != n`.
/// 2. Returns [`MergeSortedArrayError::UnsortedInput`] if `nums1[..m]` or
///    `nums2` is not sorted in non-decreasing order (trailing slack
///    `nums1[m..]` is not checked for sortedness).
/// 3. Returns [`MergeSortedArrayError::NotImplemented`] while this function
///    remains a stub.
///
/// # Examples
///
/// ```no_run
/// # fn main() -> Result<(), Box<dyn std::error::Error>> {
/// use leet_code::merge_sorted_array::merge_sorted_array;
///
/// let mut nums1 = [1, 2, 3, 0, 0, 0];
/// let nums2 = [2, 5, 6];
/// merge_sorted_array(&mut nums1, 3, &nums2, 3)?;
/// assert_eq!(nums1, [1, 2, 2, 3, 5, 6]);
/// # Ok(())
/// # }
/// ```
pub fn merge_sorted_array(
    nums1: &mut [i32],
    nums1_len: usize,
    nums2: &[i32],
    nums2_len: usize,
) -> Result<(), MergeSortedArrayError> {
    // Check for invalid length
    let total_len = nums2_len
        .checked_add(nums1_len)
        .ok_or(MergeSortedArrayError::InvalidLength)?;
    if nums1.len() != total_len || nums2.len() != nums2_len {
        return Err(MergeSortedArrayError::InvalidLength);
    }
    if !(nums1[..nums1_len].is_sorted() && nums2[..nums2_len].is_sorted()) {
        return Err(MergeSortedArrayError::UnsortedInput);
    }

    // check for trivial cases
    if nums2_len == 0 {
        return Ok(());
    }
    if nums1_len == 0 {
        nums1.copy_from_slice(nums2);
        return Ok(());
    }
    if nums1[nums1_len - 1] <= nums2[0] {
        nums1[nums1_len..].copy_from_slice(nums2);
        return Ok(());
    }
    if nums2[nums2_len - 1] <= nums1[0] {
        nums1.copy_within(..nums1_len, nums2_len);
        nums1[..nums2_len].copy_from_slice(nums2);
        return Ok(());
    }

    let mut i = nums1_len;
    let mut j = nums2_len;
    let mut write_idx = total_len;

    while i > 0 && j > 0 {
        write_idx -= 1;
        let left = nums1[i - 1];
        let right = nums2[j - 1];
        if left > right {
            nums1[write_idx] = left;
            i -= 1;
        } else {
            nums1[write_idx] = right;
            j -= 1;
        }
    }
    if j > 0 {
        nums1[..write_idx].copy_from_slice(&nums2[..j]);
    }
    Ok(())
}

/// LeetCode compatibility wrapper matching the official method signature.
pub struct Solution;

impl Solution {
    /// LeetCode submission entrypoint.
    ///
    /// Delegates to [`merge_sorted_array`]. Negative counts or invalid
    /// preconditions leave `nums1` and `nums2` unmodified.
    ///
    /// # Panics
    ///
    /// This function never panics; invalid or negative inputs leave `nums1` and
    /// `nums2` untouched.
    #[expect(
        clippy::ptr_arg,
        reason = "Matches official LeetCode &mut Vec<i32> signature"
    )]
    pub fn merge(nums1: &mut Vec<i32>, m: i32, nums2: &mut Vec<i32>, n: i32) {
        let (Ok(m_usize), Ok(n_usize)) = (usize::try_from(m), usize::try_from(n)) else {
            return;
        };
        let _ = merge_sorted_array(nums1, m_usize, nums2, n_usize);
    }
}
