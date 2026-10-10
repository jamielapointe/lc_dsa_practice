/**
 * @file two_sum.hpp
 * @brief Modern C++23 interface and LeetCode wrapper for the Two Sum algorithm.
 *
 * Provides a high-performance, overflow-safe implementation of LeetCode #1 (Two
 * Sum). Supports non-owning contiguous sequences via std::span and monadic
 * error handling via std::expected.
 */

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace leet_code {

/**
 * @brief Finds two distinct indices such that the elements at these indices sum
 * to the target.
 *
 * Implements the single-pass hash map algorithm with O(n) average time
 * complexity and O(n) space complexity. Operates over a non-owning std::span to
 * eliminate copying overhead.
 *
 * Signed integer arithmetic overflow during complement computation (target -
 * nums[i]) is guarded using 64-bit integer arithmetic (std::int64_t).
 *
 * @param nums Non-owning contiguous view of input integers.
 * @param target The target integer sum.
 * @return std::expected containing a std::pair<std::size_t, std::size_t>
 * representing 0-based indices on success (first < second), or a std::string
 * error message on failure.
 *
 * @note Error cases:
 *       - Input sequence has fewer than 2 elements:
 *         returns "Input sequence must contain at least two elements".
 *       - No valid pair sums to target:
 *         returns "No two sum solution found".
 */
[[nodiscard]] auto two_sum(std::span<const int> nums, int target)
    -> std::expected<std::pair<std::size_t, std::size_t>, std::string>;

/**
 * @brief Classic LeetCode #1 compatibility wrapper.
 *
 * Adapts two_sum to conform to the standard LeetCode interface signature.
 *
 * @param nums Input vector of integers.
 * @param target The target integer sum.
 * @return std::vector<int> Containing exactly two 0-based indices {i, j}.
 * @throws std::invalid_argument If nums contains fewer than 2 elements or if no
 * solution exists.
 */
[[nodiscard]] auto solve_leetcode_two_sum(const std::vector<int>& nums,
                                          int target) -> std::vector<int>;

}  // namespace leet_code
