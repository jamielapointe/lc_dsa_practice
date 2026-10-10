/**
 * @file add_two_numbers_test.cpp
 * @brief Comprehensive 4-Tier Google Test suite for C++23 Add Two Numbers
 * practice library.
 *
 * Conforms to TEST_INFRA.md and PROJECT.md specifications:
 * - Tier 1: Canonical Feature Coverage (Official LeetCode Examples 1, 2, 3 &
 * Solution wrapper)
 * - Tier 2: Boundary, Corner & Carry Propagation Edge Cases
 * - Tier 3: Memory Safety and AddressSanitizer Invariants (ScopedLinkedList
 * RAII)
 * - Tier 4: Stress Workloads (100 nodes maximum constraint) & Parameterized OJ
 * Simulation
 *
 * All tests utilize non-crashing assertions (ASSERT_NE or safe vector
 * conversion) so that running against the initial stub (which returns nullptr)
 * produces a clean, non-crashing Google Test failure (TDD Red state).
 */

#include "leet_code/add_two_numbers.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "leet_code/list_node.hpp"

namespace {

// ============================================================================
// TIER 1: CANONICAL FEATURE COVERAGE (OFFICIAL LEETCODE EXAMPLES)
// ============================================================================

// T1-ADD-01: LeetCode Example 1: l1 = [2,4,3], l2 = [5,6,4] -> [7,0,8] (342 +
// 465 = 807)
TEST(AddTwoNumbersTest, CanonicalExample1) {
  const leet_code::ScopedLinkedList list1({2, 4, 3});
  const leet_code::ScopedLinkedList list2({5, 6, 4});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  // Assert non-null to prevent segfault when running against TDD dummy stub
  ASSERT_NE(result, nullptr);

  const auto actual = leet_code::linked_list_to_vector(result);
  const std::vector<int> expected = {7, 0, 8};
  EXPECT_EQ(actual, expected);
}

// T1-ADD-02: LeetCode Example 2: l1 = [0], l2 = [0] -> [0] (0 + 0 = 0)
TEST(AddTwoNumbersTest, CanonicalExample2Zeros) {
  const leet_code::ScopedLinkedList list1({0});
  const leet_code::ScopedLinkedList list2({0});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);

  const auto actual = leet_code::linked_list_to_vector(result);
  const std::vector<int> expected = {0};
  EXPECT_EQ(actual, expected);
}

// T1-ADD-03: LeetCode Example 3: Different lengths and chain carry
// l1 = [9,9,9,9,9,9,9], l2 = [9,9,9,9] -> [8,9,9,9,0,0,0,1]
TEST(AddTwoNumbersTest, CanonicalExample3ChainCarry) {
  const leet_code::ScopedLinkedList list1({9, 9, 9, 9, 9, 9, 9});
  const leet_code::ScopedLinkedList list2({9, 9, 9, 9});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);

  const auto actual = leet_code::linked_list_to_vector(result);
  const std::vector<int> expected = {8, 9, 9, 9, 0, 0, 0, 1};
  EXPECT_EQ(actual, expected);
}

// T1-ADD-04: Solution class wrapper conformance
TEST(AddTwoNumbersTest, SolutionWrapperCompatibility) {
  leet_code::ScopedLinkedList list1({2, 4, 3});
  leet_code::ScopedLinkedList list2({5, 6, 4});

  auto* result = leet_code::Solution::addTwoNumbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);

  const auto actual = leet_code::linked_list_to_vector(result);
  const std::vector<int> expected = {7, 0, 8};
  EXPECT_EQ(actual, expected);
}

// ============================================================================
// TIER 2: BOUNDARY, CORNER & CARRY PROPAGATION EDGE CASES
// ============================================================================

// T2-ADD-01: Single digit addition without carry: [1] + [2] -> [3]
TEST(AddTwoNumbersTest, SingleDigitNoCarry) {
  const leet_code::ScopedLinkedList list1({1});
  const leet_code::ScopedLinkedList list2({2});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(leet_code::linked_list_to_vector(result), std::vector<int>({3}));
}

// T2-ADD-02: Single digit addition generating a carry: [5] + [5] -> [0, 1]
TEST(AddTwoNumbersTest, SingleDigitWithCarry) {
  const leet_code::ScopedLinkedList list1({5});
  const leet_code::ScopedLinkedList list2({5});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(leet_code::linked_list_to_vector(result), std::vector<int>({0, 1}));
}

// T2-ADD-03: Asymmetric lengths: l1 is longer than l2
TEST(AddTwoNumbersTest, AsymmetricLengthsL1Longer) {
  const leet_code::ScopedLinkedList list1({1, 2, 3, 4});
  const leet_code::ScopedLinkedList list2({9});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);
  // 4321 + 9 = 4330 -> [0, 3, 3, 4]
  EXPECT_EQ(leet_code::linked_list_to_vector(result),
            (std::vector<int>{0, 3, 3, 4}));
}

// T2-ADD-04: Asymmetric lengths: l2 is longer than l1
TEST(AddTwoNumbersTest, AsymmetricLengthsL2Longer) {
  const leet_code::ScopedLinkedList list1({9});
  const leet_code::ScopedLinkedList list2({1, 2, 3, 4});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);
  // 9 + 4321 = 4330 -> [0, 3, 3, 4]
  EXPECT_EQ(leet_code::linked_list_to_vector(result),
            (std::vector<int>{0, 3, 3, 4}));
}

// T2-ADD-05: Carry propagation to an additional most significant node: [9,9] +
// [1] -> [0,0,1]
TEST(AddTwoNumbersTest, CarryExtendsLength) {
  const leet_code::ScopedLinkedList list1({9, 9});
  const leet_code::ScopedLinkedList list2({1});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);
  // 99 + 1 = 100 -> [0, 0, 1]
  EXPECT_EQ(leet_code::linked_list_to_vector(result),
            (std::vector<int>{0, 0, 1}));
}

// ============================================================================
// TIER 3: MEMORY SAFETY & SANITIZER INVARIANTS (AddressSanitizer)
// ============================================================================

// T3-ADD-01: Verify ScopedLinkedList correctly frees returned nodes without
// ASan leaks
TEST(AddTwoNumbersTest, AddressSanitizerMemoryLeakFree) {
  const leet_code::ScopedLinkedList list1({1, 8});
  const leet_code::ScopedLinkedList list2({0});

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);
  EXPECT_EQ(leet_code::linked_list_to_vector(result), (std::vector<int>{1, 8}));
}

// ============================================================================
// TIER 4: STRESS WORKLOADS & OJ SIMULATION
// ============================================================================

// T4-ADD-01: Maximum LeetCode constraint (100 nodes each)
TEST(AddTwoNumbersTest, MaximumConstraint100Nodes) {
  const std::vector<int> digits_9(100, 9);
  const std::vector<int> digits_1 = {1};

  const leet_code::ScopedLinkedList list1(digits_9);
  const leet_code::ScopedLinkedList list2(digits_1);

  auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
  const leet_code::ScopedLinkedList managed_result(result);

  ASSERT_NE(result, nullptr);

  // 99...99 (100 digits) + 1 = 100...00 (1 followed by 100 zeros)
  // Reversed: [0, 0, ..., 0 (100 zeros), 1]
  std::vector<int> expected(100, 0);
  expected.push_back(1);

  EXPECT_EQ(leet_code::linked_list_to_vector(result), expected);
}

// T4-ADD-02: Parameterized OJ Simulation Table
struct OJTestCase {
  std::vector<int> l1;
  std::vector<int> l2;
  std::vector<int> expected;
};

TEST(AddTwoNumbersTest, OnlineJudgeSimulationSuite) {
  const std::vector<OJTestCase> test_cases = {
      {.l1 = {2, 4, 3}, .l2 = {5, 6, 4}, .expected = {7, 0, 8}},
      {.l1 = {0}, .l2 = {0}, .expected = {0}},
      {
          .l1 = {9, 9, 9, 9, 9, 9, 9},
          .l2 = {9, 9, 9, 9},
          .expected = {8, 9, 9, 9, 0, 0, 0, 1},
      },
      {
          .l1 =
              {
                  1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
              },
          .l2 = {5, 6, 4},
          .expected =
              {
                  6, 6, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
              },
      },
  };

  for (const auto& test_case : test_cases) {
    const leet_code::ScopedLinkedList list1(test_case.l1);
    const leet_code::ScopedLinkedList list2(test_case.l2);

    auto* result = leet_code::add_two_numbers(list1.get(), list2.get());
    const leet_code::ScopedLinkedList managed_result(result);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(leet_code::linked_list_to_vector(result), test_case.expected);
  }
}

}  // namespace
