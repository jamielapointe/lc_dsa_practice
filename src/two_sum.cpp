/**
 * @file two_sum.cpp
 * @brief Implementation of the C++23 Two Sum algorithm and LeetCode wrapper.
 */

#include "lc_dsa/two_sum.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lc_dsa {

[[nodiscard]] auto two_sum(std::span<const int> nums, int target)
    -> std::expected<std::pair<std::size_t, std::size_t>, std::string> {
  if (nums.size() < 2) {
    return std::unexpected{"Input sequence must contain at least two elements"};
  }

  // Value -> Index map. Reserve capacity upfront to prevent rehashing during
  // single-pass.
  std::unordered_map<int, std::size_t> seen;
  seen.reserve(nums.size());

  for (std::size_t i = 0; i < nums.size(); ++i) {
    const int current = nums[i];

    // Compute complement in 64-bit signed integer space to prevent signed
    // overflow UB.
    const auto complement_64 =
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(current);

    // If complement is outside [INT_MIN, INT_MAX], it cannot match any 32-bit
    // int in nums.
    if (complement_64 >= std::numeric_limits<int>::min() &&
        complement_64 <= std::numeric_limits<int>::max()) {
      const auto complement = static_cast<int>(complement_64);
      const auto match = seen.find(complement);
      if (match != seen.end()) {
        return std::pair{match->second, i};
      }
    }

    seen.try_emplace(current, i);
  }

  return std::unexpected{"No two sum solution found"};
}

[[nodiscard]] auto solve_leetcode_two_sum(const std::vector<int>& nums,
                                          int target) -> std::vector<int> {
  const auto result = two_sum(nums, target);
  if (!result.has_value()) {
    throw std::invalid_argument(result.error());
  }

  return {static_cast<int>(result->first), static_cast<int>(result->second)};
}

}  // namespace lc_dsa
