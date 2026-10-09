#include "lc_dsa/sliding_window_maximum.hpp"

#include <cstddef>
#include <deque>
#include <span>
#include <vector>

namespace lc_dsa {

auto sliding_window_maximum(std::span<const int> nums, int k)
    -> std::vector<int> {
  std::vector<int> result{};
  if (nums.empty() || k <= 0 || nums.size() < static_cast<size_t>(k)) {
    return result;
  }

  result.reserve(nums.size() - static_cast<size_t>(k) + 1U);
  std::deque<size_t> dq{};

  for (size_t i = 0; i < nums.size(); ++i) {
    // Remove indices that have fallen out of the current window
    if (!dq.empty() && dq.front() + static_cast<size_t>(k) <= i) {
      dq.pop_front();
    }

    // Remove indices of elements that are smaller than or equal to the current
    // element because they will never be the maximum in any window starting
    // now.
    while (!dq.empty() && nums[dq.back()] <= nums[i]) {
      dq.pop_back();
    }

    dq.push_back(i);

    // The window is fully formed at i >= k - 1
    if (i + 1 >= static_cast<size_t>(k)) {
      result.push_back(nums[dq.front()]);
    }
  }
  return result;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto Solution::maxSlidingWindow(std::vector<int>& nums, int k)
    -> std::vector<int> {
  return sliding_window_maximum(nums, k);
}

}  // namespace lc_dsa
