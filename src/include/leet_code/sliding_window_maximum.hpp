#pragma once

#include <span>
#include <vector>

namespace leet_code {

/// @file sliding_window_maximum.hpp
/// @brief 239. Sliding Window Maximum (Hard)
///
/// You are given an array of integers nums, there is a sliding window of size k
/// which is moving from the very left of the array to the very right. You can
/// only see the k numbers in the window. Each time the sliding window moves
/// right by one position.
///
/// Return the max sliding window.
///
/// Example 1:
/// Input: nums = [1,3,-1,-3,5,3,6,7], k = 3
/// Output: [3,3,5,5,6,7]
/// Explanation:
/// Window position                Max
/// ---------------               -----
/// [1  3  -1] -3  5  3  6  7       3
///  1 [3  -1  -3] 5  3  6  7       3
///  1  3 [-1  -3  5] 3  6  7       5
///  1  3  -1 [-3  5  3] 6  7       5
///  1  3  -1  -3 [5  3  6] 7       6
///  1  3  -1  -3  5 [3  6  7]      7
///
/// Example 2:
/// Input: nums = [1], k = 1
/// Output: [1]
///
/// Constraints:
/// - 1 <= nums.length <= 10^5
/// - -10^4 <= nums[i] <= 10^4
/// - 1 <= k <= nums.length
///
/// Complexity Targets:
/// Time Complexity: O(N)
/// Auxiliary Space: O(k)

/// @brief Modern C++23 interface for sliding window maximum
/// @param nums A span of integers
/// @param k The sliding window size
/// @return A vector containing the max sliding window
[[nodiscard]] auto sliding_window_maximum(std::span<const int> nums, int k)
    -> std::vector<int>;

/// @brief LeetCode Compatibility Wrapper
class Solution {
 public:
  auto maxSlidingWindow(std::vector<int>& nums, int k) -> std::vector<int>;
};

}  // namespace leet_code
