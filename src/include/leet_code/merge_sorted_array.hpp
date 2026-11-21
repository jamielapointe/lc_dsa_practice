/// @file merge_sorted_array.hpp
/// @brief 88. Merge Sorted Array (Easy)
///
/// https://leetcode.com/problems/merge-sorted-array/
///
/// You are given two integer arrays `nums1` and `nums2`, sorted in
/// **non-decreasing order**, and two integers `m` and `n`, representing the
/// number of elements in `nums1` and `nums2` respectively.
///
/// Merge `nums1` and `nums2` into a single array sorted in **non-decreasing
/// order**.
///
/// The final sorted array should not be returned by the function, but instead
/// be stored inside the array `nums1`. To accommodate this, `nums1` has a
/// length of `m + n`, where the first `m` elements denote the elements that
/// should be merged, and the last `n` elements are set to `0` and should be
/// ignored. `nums2` has a length of `n`.
///
/// **Example 1:**
///
/// Input: `nums1 = [1,2,3,0,0,0], m = 3, nums2 = [2,5,6], n = 3`
/// Output: `[1,2,2,3,5,6]`
/// Explanation: The arrays we are merging are `[1,2,3]` and `[2,5,6]`.
/// The result of the merge is `[1,2,2,3,5,6]` with the underlined elements
/// coming from `nums1`.
///
/// **Example 2:**
///
/// Input: `nums1 = [1], m = 1, nums2 = [], n = 0`
/// Output: `[1]`
/// Explanation: The arrays we are merging are `[1]` and `[]`.
/// The result of the merge is `[1]`.
///
/// **Example 3:**
///
/// Input: `nums1 = [0], m = 0, nums2 = [1], n = 1`
/// Output: `[1]`
/// Explanation: The arrays we are merging are `[]` and `[1]`.
/// The result of the merge is `[1]`.
/// Note that because `m = 0`, there are no elements in `nums1`. The `0` is only
/// there to ensure the merge result can fit in `nums1`.
///
/// **Constraints:**
///
/// * `nums1.length == m + n`
/// * `nums2.length == n`
/// * `0 <= m, n <= 200`
/// * `1 <= m + n <= 200`
/// * `-10^9 <= nums1[i], nums2[j] <= 10^9`
///
/// **Target Complexity:**
/// * Time: `O(m + n)`
/// * Auxiliary Space: `O(1)` (in-place, zero heap allocation)

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace leet_code {

/// @brief Error codes returned by `merge_sorted_array`.
enum class MergeSortedArrayError : uint8_t {
  /// Length or overflow precondition violated (`m > nums1.size()`,
  /// `nums1.size() - m != n`, or `nums2.size() != n`).
  kInvalidLength,
  /// Input sortedness precondition violated (`nums1.first(m)` or `nums2` is not
  /// sorted in non-decreasing order).
  kUnsortedInput,
  /// Function stub has not yet been implemented.
  kNotImplemented,
};

/// @brief Merges two non-decreasing integer sequences in place into `nums1`.
///
/// Contract and preconditions (validated deterministically in order):
/// 1. Length/overflow check: if `m > nums1.size()`, `nums1.size() - m != n`, or
///    `nums2.size() != n`, returns
///    `std::unexpected{MergeSortedArrayError::kInvalidLength}`.
/// 2. Sortedness check: if `nums1.first(m)` or `nums2` is not sorted in
///    non-decreasing order, returns
///    `std::unexpected{MergeSortedArrayError::kUnsortedInput}`. Trailing slack
///    slots `nums1.subspan(m)` are never checked for sortedness.
///
/// Additional guarantees:
/// - `m == 0uz && n == 0uz` with empty `nums1` and `nums2` is vacuously valid
///   and succeeds as a no-op.
/// - Trailing buffer slots `nums1[m .. m + n)` may hold `0` or arbitrary
///   sentinel/garbage `int` values; they are ignored as data and overwritten.
/// - `nums2` is read-only (`std::span<const int>`) and never mutated.
/// - Supports the full `int` range without signed overflow.
/// - Atomicity on error: if an error is returned, `nums1` remains unmodified.
///
/// @param nums1 Mutable destination view of length `m + n`, whose first `m`
///              elements are sorted in non-decreasing order.
/// @param m Number of initialized elements in `nums1`.
/// @param nums2 Read-only view of length `n`, sorted in non-decreasing order.
/// @param n Number of elements in `nums2`.
/// @return Empty expected on success, or a `MergeSortedArrayError` on
///         precondition violation or unimplemented stub.
[[nodiscard]] auto merge_sorted_array(std::span<int> nums1, size_t m,
                                      std::span<const int> nums2, size_t n)
    -> std::expected<void, MergeSortedArrayError>;

/// @brief Official LeetCode #88 Solution API wrapper.
class Solution {
 public:
  /// @brief Merges `nums2` into `nums1` in place in non-decreasing order.
  ///
  /// Delegates to `leet_code::merge_sorted_array`: if `m < 0 || n < 0`, returns
  /// immediately without mutating `nums1` or `nums2`; otherwise calls
  /// `merge_sorted_array(nums1, static_cast<size_t>(m), nums2,
  /// static_cast<size_t>(n))` and ignores any returned error without throwing,
  /// leaving `nums1` and `nums2` unmodified on error.
  ///
  /// @param nums1 Destination vector of length `m + n` whose first `m` elements
  ///              are sorted in non-decreasing order.
  /// @param m Number of initialized elements in `nums1`.
  /// @param nums2 Source vector of length `n` sorted in non-decreasing order
  ///              (never mutated).
  /// @param n Number of elements in `nums2`.
  auto merge(std::vector<int>& nums1, int m, std::vector<int>& nums2, int n)
      -> void;
};

}  // namespace leet_code
