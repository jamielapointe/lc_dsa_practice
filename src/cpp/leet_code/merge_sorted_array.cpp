#include "leet_code/merge_sorted_array.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include "common/math_extra.hpp"

namespace leet_code {

auto merge_sorted_array(std::span<int> nums1, size_t m,
                        std::span<const int> nums2, size_t n)
    -> std::expected<void, MergeSortedArrayError> {
  const auto total_len_chk = common::checked_add(m, n);
  if (!total_len_chk) {
    return std::unexpected(MergeSortedArrayError::kInvalidLength);
  }
  const size_t total_len = *total_len_chk;
  if (nums1.size() != total_len || n != nums2.size()) {
    return std::unexpected(MergeSortedArrayError::kInvalidLength);
  }
  if (!std::ranges::is_sorted(nums1.first(m)) ||
      !std::ranges::is_sorted(nums2)) {
    return std::unexpected(MergeSortedArrayError::kUnsortedInput);
  }

  // Check trivial and disjoint fast paths.
  if (n == 0uz) {
    return {};
  }
  if (m == 0uz) {
    std::ranges::copy(nums2, nums1.begin());
    return {};
  }
  if (nums1[m - 1uz] <= nums2[0]) {
    std::ranges::copy(nums2, nums1.subspan(m).begin());
    return {};
  }
  if (nums1[0] >= nums2[n - 1uz]) {
    std::ranges::copy_backward(nums1.first(m), nums1.end());
    std::ranges::copy(nums2, nums1.begin());
    return {};
  }

  size_t i = m;
  size_t j = n;
  size_t write_idx = total_len;
  while (i > 0uz && j > 0uz) {
    --write_idx;
    const int left = nums1[i - 1uz];
    const int right = nums2[j - 1uz];
    if (left > right) {
      nums1[write_idx] = left;
      --i;
    } else {
      nums1[write_idx] = right;
      --j;
    }
  }
  if (j > 0uz) {
    std::ranges::copy(nums2.first(j), nums1.begin());
  }
  return {};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): LeetCode API.
auto Solution::merge(std::vector<int>& nums1, int m, std::vector<int>& nums2,
                     int n) -> void {
  if (m < 0 || n < 0) {
    return;
  }
  static_cast<void>(merge_sorted_array(nums1, static_cast<size_t>(m), nums2,
                                       static_cast<size_t>(n)));
}

}  // namespace leet_code
