#include "lc_dsa/shortest_path_in_binary_matrix.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace {

// Tier 1: Canonical Feature Coverage
TEST(ShortestPathInBinaryMatrixTest, Example1) {
  std::vector<std::vector<int>> grid = {{0, 1}, {1, 0}};
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), 2);

  EXPECT_EQ(lc_dsa::Solution::shortestPathBinaryMatrix(grid), 2);
}

TEST(ShortestPathInBinaryMatrixTest, Example2) {
  std::vector<std::vector<int>> grid = {{0, 0, 0}, {1, 1, 0}, {1, 1, 0}};
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), 4);

  EXPECT_EQ(lc_dsa::Solution::shortestPathBinaryMatrix(grid), 4);
}

TEST(ShortestPathInBinaryMatrixTest, Example3) {
  std::vector<std::vector<int>> grid = {{1, 0, 0}, {1, 1, 0}, {1, 1, 0}};
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), -1);

  EXPECT_EQ(lc_dsa::Solution::shortestPathBinaryMatrix(grid), -1);
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(ShortestPathInBinaryMatrixTest, SingleCellClear) {
  std::vector<std::vector<int>> grid = {{0}};
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), 1);
}

TEST(ShortestPathInBinaryMatrixTest, SingleCellBlocked) {
  std::vector<std::vector<int>> grid = {{1}};
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), -1);
}

TEST(ShortestPathInBinaryMatrixTest, TargetBlocked) {
  std::vector<std::vector<int>> grid = {{0, 0}, {0, 1}};
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), -1);
}

// Tier 3: Sanitizer & Memory Invariants
TEST(ShortestPathInBinaryMatrixTest, ASanCleanliness) {
  std::vector<std::vector<int>> grid = {{0, 0}, {0, 0}};
  // Verify no leaks under AddressSanitizer
  static_cast<void>(lc_dsa::shortest_path_in_binary_matrix(grid));
}

// Tier 4: Stress Workloads & OJ Simulation
TEST(ShortestPathInBinaryMatrixTest, MaximumConstraint) {
  // 100x100 matrix of 0s
  std::vector<std::vector<int>> grid(100, std::vector<int>(100, 0));
  // The shortest path in an empty grid of 100x100 is the diagonal, which is 100
  // cells.
  EXPECT_EQ(lc_dsa::shortest_path_in_binary_matrix(grid), 100);
}

}  // namespace
