#pragma once

#include <span>
#include <vector>

namespace leet_code {

/**
 * @file shortest_path_in_binary_matrix.hpp
 * @brief 1091. Shortest Path in Binary Matrix (Medium)
 *
 * URL: https://leetcode.com/problems/shortest-path-in-binary-matrix/
 *
 * Target Complexity:
 * Time: $O(N^2)$ where $N$ is the number of rows/cols.
 * Space: $O(N^2)$ for the BFS queue.
 *
 * Constraints:
 * - n == grid.length
 * - n == grid[i].length
 * - 1 <= n <= 100
 * - grid[i][j] is 0 or 1
 */

[[nodiscard]] auto shortest_path_in_binary_matrix(
    std::span<const std::vector<int>> grid) -> int;

class Solution {
 public:
  auto static shortestPathBinaryMatrix(std::span<const std::vector<int>> grid)
      -> int;
};

}  // namespace leet_code
