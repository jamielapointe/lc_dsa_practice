#include "lc_dsa/sliding_window_maximum.hpp"

#include <span>
#include <vector>

namespace lc_dsa {

auto sliding_window_maximum(std::span<const int> nums, int k)
    -> std::vector<int> {
  static_cast<void>(nums);
  static_cast<void>(k);
  return {};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto Solution::maxSlidingWindow(std::vector<int>& nums, int k)
    -> std::vector<int> {
  return sliding_window_maximum(nums, k);
}

}  // namespace lc_dsa
