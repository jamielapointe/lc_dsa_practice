/**
 * @file two_sum_test.cpp
 * @brief Comprehensive 4-Tier Google Test suite for C++23 Two Sum practice
 * library.
 *
 * Conforms to TEST_INFRA.md and PROJECT.md specifications:
 * - Tier 1: Canonical Feature Coverage (Standard pairs, spans, wrapper)
 * - Tier 2: Boundary, Corner & Arithmetic Overflow Edge Cases
 * - Tier 3: Cross-Feature and Sanitizer Invariants
 * - Tier 4: Large-Scale Performance Stress Tests and Online Judge Simulation
 */

#include "leet_code/two_sum.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// ============================================================================
// TIER 1: CANONICAL FEATURE COVERAGE
// ============================================================================

// T1-LIB-01: Standard canonical pair (LeetCode Example 1)
TEST(TwoSumTest, StandardCanonicalPair) {
  const std::vector<int> nums = {2, 7, 11, 15};
  constexpr int kTarget = 9;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
}

// T1-LIB-02: Pair situated at the very beginning of the vector
TEST(TwoSumTest, PairAtBeginning) {
  const std::vector<int> nums = {10, 20, 30, 40, 50};
  constexpr int kTarget = 30;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T1-LIB-03: Pair situated strictly in the middle of the vector
TEST(TwoSumTest, PairInMiddle) {
  const std::vector<int> nums = {1, 50, 60, 2, 9};
  constexpr int kTarget = 110;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 1U);
  EXPECT_EQ(result->second, 2U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T1-LIB-04: Pair situated at the very end of the vector
TEST(TwoSumTest, PairAtEnd) {
  const std::vector<int> nums = {5, 10, 15, 20, 25};
  constexpr int kTarget = 45;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 3U);
  EXPECT_EQ(result->second, 4U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T1-LIB-05: Pair situated at extreme endpoints (first and last elements)
TEST(TwoSumTest, PairAtEndpoints) {
  const std::vector<int> nums = {10, 1, 2, 3, 4, 5, 20};
  constexpr int kTarget = 30;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 6U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T1-LIB-06: Non-adjacent indices (LeetCode Example 2)
TEST(TwoSumTest, NonAdjacentIndices) {
  const std::vector<int> nums = {3, 2, 4};
  constexpr int kTarget = 6;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 1U);
  EXPECT_EQ(result->second, 2U);
}

// T1-LIB-07: Non-owning span compatibility with stack-allocated std::array
TEST(TwoSumTest, NonOwningSpanCompatibilityStdArray) {
  constexpr std::array<int, 4> arr = {10, 20, 35, 40};
  constexpr int kTarget = 50;
  const auto result = leet_code::two_sum(arr, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 3U);
}

// T1-LIB-08: Non-owning span compatibility with raw C-style array view
TEST(TwoSumTest, NonOwningSpanCompatibilityRawArray) {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  const int raw_nums[] = {100, 200, 300};
  const std::span<const int> span_view(raw_nums);
  constexpr int kTarget = 400;
  const auto result = leet_code::two_sum(span_view, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 2U);
}

// T1-LIB-09: LeetCode compatibility wrapper success (returns std::vector<int>)
TEST(TwoSumTest, LeetCodeCompatibilityWrapperSuccess) {
  const std::vector<int> nums = {2, 7, 11, 15};
  constexpr int kTarget = 9;
  const auto res = leet_code::solve_leetcode_two_sum(nums, kTarget);

  ASSERT_EQ(res.size(), 2U);
  EXPECT_EQ(res[0], 0);
  EXPECT_EQ(res[1], 1);
}

// ============================================================================
// TIER 2: BOUNDARY, CORNER & ARITHMETIC OVERFLOW EDGE CASES
// ============================================================================

// T2-ALG-01: Additive identity with two zeros summing to zero
TEST(TwoSumTest, ZeroElementsPair) {
  const std::vector<int> nums = {0, 4, 3, 0};
  constexpr int kTarget = 0;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 3U);
}

// T2-ALG-02: Single zero paired with positive element
TEST(TwoSumTest, SingleZeroWithPositive) {
  const std::vector<int> nums = {0, 7, 11, 15};
  constexpr int kTarget = 7;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
}

// T2-ALG-03: Single zero paired with negative element
TEST(TwoSumTest, SingleZeroWithNegative) {
  const std::vector<int> nums = {15, -7, 0, 20};
  constexpr int kTarget = -7;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 1U);
  EXPECT_EQ(result->second, 2U);
}

// T2-ALG-04: INT_MIN paired with zero
TEST(TwoSumTest, IntMinAndZeroPair) {
  const std::vector<int> nums = {std::numeric_limits<int>::min(), 0, 5};
  constexpr int kTarget = std::numeric_limits<int>::min();
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
}

// T2-ALG-05: INT_MAX paired with zero
TEST(TwoSumTest, IntMaxAndZeroPair) {
  const std::vector<int> nums = {std::numeric_limits<int>::max(), 0, 10};
  constexpr int kTarget = std::numeric_limits<int>::max();
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
}

// T2-ALG-06: Negative numbers and negative target
TEST(TwoSumTest, NegativeNumbersAndNegativeTarget) {
  const std::vector<int> nums = {-5, -4, -3, -2, -1};
  constexpr int kTarget = -8;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 2U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-07: Mixed signs summing to zero
TEST(TwoSumTest, MixedSignsWithZeroSum) {
  const std::vector<int> nums = {-100, 50, 20, 100};
  constexpr int kTarget = 0;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 3U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-08: Mixed signs yielding a positive target
TEST(TwoSumTest, MixedSignsWithPositiveTarget) {
  const std::vector<int> nums = {-10, 20, 35, -5};
  constexpr int kTarget = 10;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-09: Mixed signs yielding a negative target
TEST(TwoSumTest, MixedSignsWithNegativeTarget) {
  const std::vector<int> nums = {-20, 10, -5, 30};
  constexpr int kTarget = -25;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 2U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-10: Duplicate elements summing to target (LeetCode Example 3)
TEST(TwoSumTest, DuplicateElementsSummingToTarget) {
  const std::vector<int> nums = {3, 3};
  constexpr int kTarget = 6;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
}

// T2-ALG-11: Array with multiple duplicate candidates
TEST(TwoSumTest, MultipleDuplicateCandidates) {
  const std::vector<int> nums = {5, 5, 5, 5};
  constexpr int kTarget = 10;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 1U);
}

// T2-ALG-12: Duplicate values present but do not sum to target
TEST(TwoSumTest, DuplicatesPresentButNotSummingToTarget) {
  const std::vector<int> nums = {3, 3, 4, 5};
  constexpr int kTarget = 8;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 3U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-13: Duplicates present but no solution exists
TEST(TwoSumTest, DuplicatesWithoutSolution) {
  const std::vector<int> nums = {2, 2, 7};
  constexpr int kTarget = 6;
  const auto result = leet_code::two_sum(nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No two sum solution found");
}

// T2-ALG-14: Large positive values near INT_MAX
TEST(TwoSumTest, LargePositiveValuesNearIntMax) {
  constexpr int kMax = std::numeric_limits<int>::max();
  const std::vector<int> nums = {kMax - 10, 5, 10};
  constexpr int kTarget = kMax;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 2U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-15: Large negative values near INT_MIN
TEST(TwoSumTest, LargeNegativeValuesNearIntMin) {
  constexpr int kMin = std::numeric_limits<int>::min();
  const std::vector<int> nums = {kMin + 10, 20, -10};
  constexpr int kTarget = kMin;
  const auto result = leet_code::two_sum(nums, kTarget);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->first, 0U);
  EXPECT_EQ(result->second, 2U);
  EXPECT_EQ(nums[result->first] + nums[result->second], kTarget);
}

// T2-ALG-16: 64-bit integer overflow protection: INT_MAX - INT_MIN > INT_MAX
TEST(TwoSumTest, SignedIntegerOverflowGuardMaxMin) {
  const std::vector<int> nums = {std::numeric_limits<int>::min(), 1};
  constexpr int kTarget = std::numeric_limits<int>::max();
  const auto result = leet_code::two_sum(nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No two sum solution found");
}

// T2-ALG-17: 64-bit integer underflow protection: INT_MIN - INT_MAX < INT_MIN
TEST(TwoSumTest, SignedIntegerUnderflowGuardMinPositive) {
  const std::vector<int> nums = {std::numeric_limits<int>::max(), -1};
  constexpr int kTarget = std::numeric_limits<int>::min();
  const auto result = leet_code::two_sum(nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No two sum solution found");
}

// T2-ALG-18: Adversarial overflow test: prevents naive 32-bit wrap to -1 from
// matching
TEST(TwoSumTest, SignedIntegerOverflowDoesNotMatchWrappedNegativeOne) {
  // If complement were computed as 32-bit int: INT_MAX - INT_MIN = 2147483647 -
  // (-2147483648) wraps to -1 in 2's complement. If -1 is present, a naive
  // implementation would erroneously match!
  const std::vector<int> nums = {std::numeric_limits<int>::min(), -1, 10};
  constexpr int kTarget = std::numeric_limits<int>::max();
  const auto result = leet_code::two_sum(nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No two sum solution found");
}

// T2-ALG-19: Adversarial underflow test: prevents naive 32-bit wrap from
// matching
TEST(TwoSumTest, SignedIntegerUnderflowDoesNotMatchWrappedZero) {
  const std::vector<int> nums = {std::numeric_limits<int>::max(), 1, 10};
  constexpr int kTarget = std::numeric_limits<int>::min();
  const auto result = leet_code::two_sum(nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No two sum solution found");
}

// T2-ALG-20: Empty span returns std::unexpected with informative error
TEST(TwoSumTest, EmptySpanReturnsError) {
  const std::vector<int> empty_nums = {};
  constexpr int kTarget = 5;
  const auto result = leet_code::two_sum(empty_nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(),
            "Input sequence must contain at least two elements");
}

// T2-ALG-21: Single element span returns std::unexpected with informative error
TEST(TwoSumTest, SingleElementSpanReturnsError) {
  const std::vector<int> single_num = {42};
  constexpr int kTarget = 42;
  const auto result = leet_code::two_sum(single_num, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(),
            "Input sequence must contain at least two elements");
}

// T2-ALG-22: No solution exists in non-trivial array
TEST(TwoSumTest, NoSolutionExistsInNonTrivialArray) {
  const std::vector<int> nums = {1, 3, 5, 7, 9, 11};
  constexpr int kTarget = 100;
  const auto result = leet_code::two_sum(nums, kTarget);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), "No two sum solution found");
}

// T2-ALG-23: LeetCode wrapper throws std::invalid_argument on empty vector
TEST(TwoSumTest, LeetCodeWrapperEmptyThrows) {
  const std::vector<int> empty_nums = {};
  constexpr int kTarget = 5;

  EXPECT_THROW(
      {
        static_cast<void>(
            leet_code::solve_leetcode_two_sum(empty_nums, kTarget));
      },
      std::invalid_argument);
}

// T2-ALG-24: LeetCode wrapper throws std::invalid_argument on single-element
// vector
TEST(TwoSumTest, LeetCodeWrapperSingleElementThrows) {
  const std::vector<int> single_num = {42};
  constexpr int kTarget = 42;

  EXPECT_THROW(
      {
        static_cast<void>(
            leet_code::solve_leetcode_two_sum(single_num, kTarget));
      },
      std::invalid_argument);
}

// T2-ALG-25: LeetCode wrapper throws std::invalid_argument when no solution
// exists
TEST(TwoSumTest, LeetCodeWrapperNoSolutionThrows) {
  const std::vector<int> nums = {1, 3, 5};
  constexpr int kTarget = 100;

  EXPECT_THROW(
      { static_cast<void>(leet_code::solve_leetcode_two_sum(nums, kTarget)); },
      std::invalid_argument);
}

// T2-ALG-26: LeetCode wrapper preserves explanatory error string in exception
// message
TEST(TwoSumTest, LeetCodeWrapperExceptionMessageMatches) {
  try {
    static_cast<void>(leet_code::solve_leetcode_two_sum({}, 10));
    FAIL() << "Expected std::invalid_argument not thrown for empty vector";
  } catch (const std::invalid_argument& caught_exception) {
    EXPECT_STREQ(caught_exception.what(),
                 "Input sequence must contain at least two elements");
  }

  try {
    static_cast<void>(leet_code::solve_leetcode_two_sum({1, 2}, 99));
    FAIL() << "Expected std::invalid_argument not thrown for unsolved query";
  } catch (const std::invalid_argument& caught_exception) {
    EXPECT_STREQ(caught_exception.what(), "No two sum solution found");
  }
}

// ============================================================================
// TIER 4: REAL-WORLD STRESS WORKLOADS & ONLINE JUDGE SIMULATION
// ============================================================================

// T4-STRESS-01: 10,000 Elements Large-Scale Performance Workload Test
TEST(TwoSumTest, LargeScale10kElementsPerformance) {
  constexpr std::size_t kSize = 10'000;
  std::vector<int> nums(kSize);
  std::ranges::iota(nums, 1);  // 1, 2, 3, ..., 10000

  // Target requires first element (1) and last element (10000)
  const int target = nums[0] + nums[kSize - 1];

  const auto start = std::chrono::steady_clock::now();
  const auto result = leet_code::two_sum(nums, target);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->first, result->second);
  EXPECT_EQ(nums[result->first] + nums[result->second], target);
  EXPECT_LT(elapsed.count(),
            100);  // Expected < 100ms even under ASan/UBSan Debug
}

// T4-STRESS-02: 100,000 Elements Extreme-Scale Performance Workload Test
TEST(TwoSumTest, LargeScale100kElementsPerformance) {
  constexpr std::size_t kSize = 100'000;
  std::vector<int> nums(kSize);
  for (std::size_t i = 0; i < kSize; ++i) {
    nums[i] = static_cast<int>(i * 2);  // Even numbers: 0, 2, 4, ...
  }

  // Target requires elements at index 49999 and index 99999
  const int target = nums[49'999] + nums[99'999];

  const auto start = std::chrono::steady_clock::now();
  const auto result = leet_code::two_sum(nums, target);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);

  ASSERT_TRUE(result.has_value());
  EXPECT_NE(result->first, result->second);
  EXPECT_EQ(nums[result->first] + nums[result->second], target);
  EXPECT_LT(elapsed.count(),
            500);  // Expected < 500ms even under ASan/UBSan Debug
}

struct TestCase {
  std::vector<int> nums;
  int target;
};

void VerifyOnlineJudgeSolution(const std::vector<int>& solution,
                               const TestCase& test_case) {
  ASSERT_EQ(solution.size(), 2U);
  const int idx1 = solution[0];
  const int idx2 = solution[1];

  EXPECT_NE(idx1, idx2);
  const bool bounds_valid =
      idx1 >= 0 && static_cast<std::size_t>(idx1) < test_case.nums.size() &&
      idx2 >= 0 && static_cast<std::size_t>(idx2) < test_case.nums.size();
  EXPECT_TRUE(bounds_valid);
  if (bounds_valid) {
    EXPECT_EQ(test_case.nums[static_cast<std::size_t>(idx1)] +
                  test_case.nums[static_cast<std::size_t>(idx2)],
              test_case.target);
  }
}

// T4-LC-01: LeetCode Online Judge Simulation Harness
TEST(TwoSumTest, LeetCodeOnlineJudgeSimulationHarness) {
  const std::vector<TestCase> test_cases = {
      {.nums = {2, 7, 11, 15}, .target = 9},
      {.nums = {3, 2, 4}, .target = 6},
      {.nums = {3, 3}, .target = 6},
      {.nums = {-1, -2, -3, -4, -5}, .target = -8},
      {.nums = {0, 4, 3, 0}, .target = 0},
      {.nums = {1, 5, 8, 12, 19}, .target = 20},
      {.nums = {100, 200, 300, 400}, .target = 500},
      {.nums = {-50, 0, 50}, .target = 0},
  };

  for (const auto& test_case : test_cases) {
    const auto solution =
        leet_code::solve_leetcode_two_sum(test_case.nums, test_case.target);
    VerifyOnlineJudgeSolution(solution, test_case);
  }
}

}  // namespace
