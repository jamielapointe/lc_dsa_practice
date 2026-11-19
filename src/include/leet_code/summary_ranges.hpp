/// @file summary_ranges.hpp
/// @brief 228. Summary Ranges (Easy)
///
/// https://leetcode.com/problems/summary-ranges/
///
/// You are given a sorted unique integer array `nums`.
///
/// A range `[a,b]` is the set of all integers from `a` to `b` (inclusive).
///
/// Return the smallest sorted list of ranges that cover all the numbers in the
/// array exactly. That is, each element of `nums` is covered by exactly one of
/// the ranges, and there is no integer `x` such that `x` is in one of the
/// ranges but not in `nums`.
///
/// Each range `[a,b]` in the list should be output as:
/// * `"a->b"` if `a != b`
/// * `"a"` if `a == b`
///
/// **Example 1:**
///
/// Input: `nums = [0,1,2,4,5,7]`
/// Output: `["0->2","4->5","7"]`
/// Explanation: The ranges are:
/// `[0,2] --> "0->2"`
/// `[4,5] --> "4->5"`
/// `[7,7] --> "7"`
///
/// **Example 2:**
///
/// Input: `nums = [0,2,3,4,6,8,9]`
/// Output: `["0","2->4","6","8->9"]`
/// Explanation: The ranges are:
/// `[0,0] --> "0"`
/// `[2,4] --> "2->4"`
/// `[6,6] --> "6"`
/// `[8,9] --> "8->9"`
///
/// **Constraints:**
///
/// * `0 <= nums.length <= 20`
/// * `-2^31 <= nums[i] <= 2^31 - 1`
/// * All the values of `nums` are unique.
/// * `nums` is sorted in ascending order.
///
/// **Target Complexity:**
/// * Time: O(n)
/// * Space: O(1) extra space beyond the output list.

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace leet_code {

/// @brief Error codes returned by `summary_ranges`.
enum class SummaryRangesError : uint8_t {
  /// Input contains duplicates or is not strictly ascending
  /// (`nums[i] <= nums[i - 1]`).
  kUnsortedOrDuplicateInput,
  /// Function stub has not yet been implemented.
  kNotImplemented,
};

/// @brief Summarizes a sorted unique integer sequence into minimal range
/// strings.
///
/// Returns an empty vector `{}` when `nums` is empty.
/// Returns `SummaryRangesError::kUnsortedOrDuplicateInput` when `nums` contains
/// duplicates or is not strictly ascending (`nums[i] <= nums[i - 1]`).
///
/// @param nums Non-owning contiguous view of sorted unique 32-bit integers.
/// @return Smallest sorted list of formatted range strings (`"a->b"` or `"a"`),
///         or a `SummaryRangesError` if input preconditions are violated.
[[nodiscard]] auto summary_ranges(std::span<const int> nums)
    -> std::expected<std::vector<std::string>, SummaryRangesError>;

/// @brief Official LeetCode #228 Solution API wrapper.
class Solution {
 public:
  /// @brief Summarizes contiguous integer ranges in `nums`.
  ///
  /// Delegates to `leet_code::summary_ranges(nums)` and returns `{}` if an
  /// error occurs. Does not mutate `nums`.
  ///
  /// @param nums Sorted unique vector of 32-bit signed integers.
  /// @return Sorted list of range strings (`"a->b"` or `"a"`).
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static): LeetCode
  // API.
  [[nodiscard]] auto summaryRanges(std::vector<int>& nums)
      -> std::vector<std::string>;
};

}  // namespace leet_code
