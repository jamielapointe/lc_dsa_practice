#include "lc_dsa/number_of_islands.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace {

// Tier 1: Canonical Feature Coverage
TEST(NumberOfIslandsTest, Example1) {
  std::vector<std::vector<char>> grid = {
      {'1', '1', '1', '1', '0'},
      {'1', '1', '0', '1', '0'},
      {'1', '1', '0', '0', '0'},
      {'0', '0', '0', '0', '0'},
  };
  lc_dsa::Solution sol;
  EXPECT_EQ(sol.numIslands(grid), 1);  // TDD Red State
}

TEST(NumberOfIslandsTest, Example2) {
  std::vector<std::vector<char>> grid = {
      {'1', '1', '0', '0', '0'},
      {'1', '1', '0', '0', '0'},
      {'0', '0', '1', '0', '0'},
      {'0', '0', '0', '1', '1'},
  };
  EXPECT_EQ(lc_dsa::number_of_islands(grid), 3);  // TDD Red State
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(NumberOfIslandsTest, SingleNode) {
  std::vector<std::vector<char>> grid = {{'1'}};
  EXPECT_EQ(lc_dsa::number_of_islands(grid), 1);
}

TEST(NumberOfIslandsTest, EmptyGridRow) {
  std::vector<std::vector<char>> grid = {{}};
  EXPECT_EQ(lc_dsa::number_of_islands(grid), 0);
}

// Tier 3: Sanitizer & Memory Invariants
// Ensure AddressSanitizer doesn't complain (implicit in run).

// Tier 4: Stress Workloads & OJ Simulation
TEST(NumberOfIslandsTest, MaxConstraintsGrid) {
  // 300x300 grid filled with '1's and '0's in checkerboard
  std::vector<std::vector<char>> grid(300, std::vector<char>(300, '0'));
  for (size_t i = 0; i < 300; ++i) {
    for (size_t j = 0; j < 300; ++j) {
      if ((i + j) % 2 == 0) {
        grid[i][j] = '1';
      }
    }
  }

  auto start = std::chrono::high_resolution_clock::now();
  int result = lc_dsa::number_of_islands(grid);
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_LT(duration, std::chrono::milliseconds(200));
  EXPECT_EQ(result, 45000);  // 300 * 300 / 2 independent islands
}

}  // namespace
