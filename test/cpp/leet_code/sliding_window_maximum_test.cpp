#include "leet_code/sliding_window_maximum.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace {

// ============================================================================
// Tier 1: Canonical Feature Coverage
// ============================================================================

TEST(SlidingWindowMaximumTest, Example1) {
  std::vector<int> nums = {1, 3, -1, -3, 5, 3, 6, 7};
  int k = 3;
  std::vector<int> expected = {3, 3, 5, 5, 6, 7};

  // Modern Interface
  EXPECT_EQ(leet_code::sliding_window_maximum(nums, k), expected);

  // LeetCode Wrapper
  leet_code::Solution solution;
  EXPECT_EQ(solution.maxSlidingWindow(nums, k), expected);
}

TEST(SlidingWindowMaximumTest, Example2) {
  std::vector<int> nums = {1};
  int k = 1;
  std::vector<int> expected = {1};

  EXPECT_EQ(leet_code::sliding_window_maximum(nums, k), expected);
}

// ============================================================================
// Tier 2: Boundary, Corner & Edge Cases
// ============================================================================

TEST(SlidingWindowMaximumTest, AllSameElements) {
  std::vector<int> nums(10, 5);
  int k = 3;
  std::vector<int> expected(8, 5);

  EXPECT_EQ(leet_code::sliding_window_maximum(nums, k), expected);
}

TEST(SlidingWindowMaximumTest, StrictlyIncreasing) {
  std::vector<int> nums = {1, 2, 3, 4, 5, 6};
  int k = 3;
  std::vector<int> expected = {3, 4, 5, 6};

  EXPECT_EQ(leet_code::sliding_window_maximum(nums, k), expected);
}

TEST(SlidingWindowMaximumTest, StrictlyDecreasing) {
  std::vector<int> nums = {6, 5, 4, 3, 2, 1};
  int k = 3;
  std::vector<int> expected = {6, 5, 4, 3};

  EXPECT_EQ(leet_code::sliding_window_maximum(nums, k), expected);
}

TEST(SlidingWindowMaximumTest, WindowSizeEqualsArrayLength) {
  std::vector<int> nums = {1, -1, 4, 2, 5};
  int k = 5;
  std::vector<int> expected = {5};

  EXPECT_EQ(leet_code::sliding_window_maximum(nums, k), expected);
}

// ============================================================================
// Tier 3: Sanitizer & Memory Invariants
// ============================================================================

// Memory management is automatically handled by std::vector.
// AddressSanitizer and LeakSanitizer will verify correctness during CI runs.

// ============================================================================
// Tier 4: Stress Workloads & OJ Simulation
// ============================================================================

TEST(SlidingWindowMaximumTest, StressTestMaxConstraints) {
  constexpr int kNumElements = 100'000;
  constexpr int kWindowSize = 50'000;

  std::vector<int> nums(kNumElements);
  // Fill with alternating values to prevent trivial branch prediction
  for (size_t i = 0; i < nums.size(); ++i) {
    nums[i] = (i % 2 == 0) ? static_cast<int>(i) : -static_cast<int>(i);
  }

  // Pre-calculate expected result for the first window to ensure we don't
  // test the stub trivially without it failing. In a real scenario, we might
  // verify only properties of the result (e.g., size), but TDD Red State
  // expects the assertions to fail cleanly, which they will.

  auto start = std::chrono::steady_clock::now();
  auto result = leet_code::sliding_window_maximum(nums, kWindowSize);
  auto end = std::chrono::steady_clock::now();

  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // Modern solutions should be O(N) and easily process 10^5 elements within
  // 500ms
  EXPECT_LT(duration.count(), 500)
      << "Algorithm is likely O(N*k) instead of O(N)";

  // The stub returns an empty vector, so this size check will fail cleanly.
  EXPECT_EQ(result.size(), kNumElements - kWindowSize + 1);
}

}  // namespace
