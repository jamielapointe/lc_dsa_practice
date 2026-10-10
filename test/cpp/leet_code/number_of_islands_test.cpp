#include "leet_code/number_of_islands.hpp"

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
  leet_code::Solution sol;
  EXPECT_EQ(sol.numIslands(grid), 1);  // TDD Red State
}

TEST(NumberOfIslandsTest, Example2) {
  std::vector<std::vector<char>> grid = {
      {'1', '1', '0', '0', '0'},
      {'1', '1', '0', '0', '0'},
      {'0', '0', '1', '0', '0'},
      {'0', '0', '0', '1', '1'},
  };
  EXPECT_EQ(leet_code::number_of_islands(grid), 3);  // TDD Red State
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(NumberOfIslandsTest, SingleNode) {
  std::vector<std::vector<char>> grid = {{'1'}};
  EXPECT_EQ(leet_code::number_of_islands(grid), 1);
}

TEST(NumberOfIslandsTest, EmptyGridRow) {
  std::vector<std::vector<char>> grid = {{}};
  EXPECT_EQ(leet_code::number_of_islands(grid), 0);
}

// Tier 3: Sanitizer & Memory Invariants
// Ensure AddressSanitizer doesn't complain (implicit in run).

// Tier 4: Stress Workloads & OJ Simulation
TEST(NumberOfIslandsTest, MaxConstraintsGrid) {
  // 300x300 grid filled with '1's and '0's in checkerboard
  std::vector<std::vector<char>> grid(300, std::vector<char>(300, '0'));
  for (std::size_t i = 0; i < 300; ++i) {
    for (std::size_t j = 0; j < 300; ++j) {
      if ((i + j) % 2 == 0) {
        grid[i][j] = '1';
      }
    }
  }

  auto start = std::chrono::high_resolution_clock::now();
  int result = leet_code::number_of_islands(grid);
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Generous bound: sanitizer builds are several times slower than release, and
  // this only needs to catch asymptotically slow solutions.
  EXPECT_LT(duration, std::chrono::milliseconds(1000));
  EXPECT_EQ(result, 45000);  // 300 * 300 / 2 independent islands
}

}  // namespace
