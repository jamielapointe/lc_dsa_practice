/// @file summary_ranges_test.cpp
/// @brief Comprehensive 4-tier Google Test suite for LeetCode #228 (Summary
/// Ranges).
///
/// Tiers:
/// - Tier 1: Canonical LeetCode examples and non-owning std::span views.
/// - Tier 2: Boundary, edge, 32-bit signed overflow hazards, and preconditions.
/// - Tier 3: Structural range-reconstruction invariants and non-mutation
/// checks.
/// - Tier 4: Large-scale performance stress workloads and OJ simulation loop.

#include "leet_code/summary_ranges.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct ParsedRange {
  int64_t start = 0;
  int64_t end = 0;
};

[[nodiscard]] auto parse_range_endpoint(std::string_view token)
    -> std::expected<int64_t, std::string> {
  if (token.empty()) {
    return std::unexpected{std::string("Empty range endpoint")};
  }
  bool negative = false;
  size_t index = 0uz;
  if (token[0uz] == '-') {
    negative = true;
    index = 1uz;
    if (index >= token.size()) {
      return std::unexpected{std::string("Dangling minus sign")};
    }
  }
  if (token[index] == '0' && (negative || index + 1uz < token.size())) {
    return std::unexpected{
        std::string("Non-canonical leading zero or negative zero")};
  }
  int64_t magnitude = 0;
  for (; index < token.size(); ++index) {
    const char ch = token[index];
    if (ch < '0' || ch > '9') {
      return std::unexpected{std::string("Non-digit character in endpoint")};
    }
    const auto digit = static_cast<int64_t>(ch - '0');
    if (magnitude > (std::numeric_limits<int64_t>::max() - digit) / 10) {
      return std::unexpected{std::string("Integer overflow parsing endpoint")};
    }
    magnitude = (magnitude * 10) + digit;
  }
  return negative ? -magnitude : magnitude;
}

[[nodiscard]] auto parse_range(std::string_view range_str)
    -> std::expected<ParsedRange, std::string> {
  constexpr std::string_view kArrow = "->";
  const size_t arrow_pos = range_str.find(kArrow);
  if (arrow_pos == std::string_view::npos) {
    const auto value = parse_range_endpoint(range_str);
    if (!value.has_value()) {
      return std::unexpected{value.error()};
    }
    return ParsedRange{.start = *value, .end = *value};
  }
  const auto start = parse_range_endpoint(range_str.substr(0uz, arrow_pos));
  if (!start.has_value()) {
    return std::unexpected{start.error()};
  }
  const auto end =
      parse_range_endpoint(range_str.substr(arrow_pos + kArrow.size()));
  if (!end.has_value()) {
    return std::unexpected{end.error()};
  }
  if (*start >= *end) {
    return std::unexpected{
        std::string("Multi-element range must satisfy start < end")};
  }
  return ParsedRange{.start = *start, .end = *end};
}

void VerifyRangeInvariants(std::span<const int> nums,
                           std::span<const std::string> ranges) {
  if (nums.empty()) {
    EXPECT_TRUE(ranges.empty());
    return;
  }
  ASSERT_FALSE(ranges.empty());

  std::vector<ParsedRange> parsed;
  parsed.reserve(ranges.size());
  for (const auto& range_str : ranges) {
    const auto parsed_range = parse_range(range_str);
    ASSERT_TRUE(parsed_range.has_value()) << parsed_range.error();
    EXPECT_GE(parsed_range->start,
              static_cast<int64_t>(std::numeric_limits<int>::min()));
    EXPECT_LE(parsed_range->end,
              static_cast<int64_t>(std::numeric_limits<int>::max()));
    EXPECT_LE(parsed_range->start, parsed_range->end);
    parsed.push_back(*parsed_range);
  }

  size_t num_idx = 0uz;
  for (size_t range_idx = 0uz; range_idx < parsed.size(); ++range_idx) {
    const ParsedRange& range = parsed[range_idx];
    if (range_idx > 0uz) {
      EXPECT_LT(parsed[range_idx - 1uz].end + 1, range.start);
    }
    const int64_t range_len = (range.end - range.start) + 1;
    ASSERT_LE(range_len, static_cast<int64_t>(nums.size() - num_idx));
    for (int64_t val = range.start; val <= range.end; ++val) {
      ASSERT_LT(num_idx, nums.size());
      EXPECT_EQ(static_cast<int64_t>(nums[num_idx]), val);
      ++num_idx;
    }
  }
  EXPECT_EQ(num_idx, nums.size());
}

// ============================================================================
// TIER 1: CANONICAL FEATURE COVERAGE
// ============================================================================

TEST(SummaryRangesTest, Example1Canonical) {
  std::vector<int> nums = {0, 1, 2, 4, 5, 7};
  const std::vector<std::string> expected = {"0->2", "4->5", "7"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, Example2Canonical) {
  std::vector<int> nums = {0, 2, 3, 4, 6, 8, 9};
  const std::vector<std::string> expected = {"0", "2->4", "6", "8->9"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, NonOwningSpanFromStdArray) {
  constexpr std::array<int, 6uz> kNums = {0, 1, 2, 4, 5, 7};
  const std::vector<std::string> expected = {"0->2", "4->5", "7"};

  const auto result = leet_code::summary_ranges(kNums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);
}

TEST(SummaryRangesTest, NonOwningSubspanView) {
  const std::vector<int> nums = {-10, 0, 1, 2, 4, 5, 7, 100};
  const std::span<const int> subspan =
      std::span<const int>(nums).subspan(1uz, 6uz);
  const std::vector<std::string> expected = {"0->2", "4->5", "7"};

  const auto result = leet_code::summary_ranges(subspan);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);
}

// ============================================================================
// TIER 2: BOUNDARY, EDGE, OVERFLOW & PRECONDITION CASES
// ============================================================================

TEST(SummaryRangesTest, EmptyInput) {
  std::vector<int> nums;
  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->empty());

  leet_code::Solution solution;
  EXPECT_TRUE(solution.summaryRanges(nums).empty());
}

TEST(SummaryRangesTest, SingletonInputs) {
  struct SingletonCase {
    int value;
    std::string expected;
  };
  const std::vector<SingletonCase> cases = {
      {.value = 0, .expected = "0"},
      {.value = -42, .expected = "-42"},
      {.value = std::numeric_limits<int>::min(), .expected = "-2147483648"},
      {.value = std::numeric_limits<int>::max(), .expected = "2147483647"},
  };

  leet_code::Solution solution;
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.expected);
    std::vector<int> nums = {test_case.value};
    const auto result = leet_code::summary_ranges(nums);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, std::vector<std::string>{test_case.expected});
    EXPECT_EQ(solution.summaryRanges(nums),
              std::vector<std::string>{test_case.expected});
  }
}

TEST(SummaryRangesTest, FullContiguousRange) {
  std::vector<int> nums = {-3, -2, -1, 0, 1, 2, 3};
  const std::vector<std::string> expected = {"-3->3"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, AllIsolatedElements) {
  std::vector<int> nums = {-10, -5, 0, 5, 10};
  const std::vector<std::string> expected = {"-10", "-5", "0", "5", "10"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, NegativeToPositiveCrossing) {
  std::vector<int> nums = {-3, -2, 0, 1, 2, 10};
  const std::vector<std::string> expected = {"-3->-2", "0->2", "10"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, OverflowHazardIntMinIntMax) {
  std::vector<int> nums = {std::numeric_limits<int>::min(),
                           std::numeric_limits<int>::max()};
  const std::vector<std::string> expected = {"-2147483648", "2147483647"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, OverflowHazardIntMinZero) {
  std::vector<int> nums = {std::numeric_limits<int>::min(), 0};
  const std::vector<std::string> expected = {"-2147483648", "0"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, OverflowHazardNegativeOneIntMax) {
  std::vector<int> nums = {-1, std::numeric_limits<int>::max()};
  const std::vector<std::string> expected = {"-1", "2147483647"};

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, OverflowHazardContiguousAtExtremes) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();
  std::vector<int> nums = {kMin, kMin + 1, 0, kMax - 1, kMax};
  const std::vector<std::string> expected = {
      "-2147483648->-2147483647",
      "0",
      "2147483646->2147483647",
  };

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.summaryRanges(nums), expected);
}

TEST(SummaryRangesTest, RejectsAdjacentDuplicates) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();
  const std::vector<std::vector<int>> duplicate_inputs = {
      {1, 2, 2, 3},
      {0, 0},
      {kMin, kMin},
      {kMax, kMax},
  };

  for (const auto& nums : duplicate_inputs) {
    const auto result = leet_code::summary_ranges(nums);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              leet_code::SummaryRangesError::kUnsortedOrDuplicateInput);
  }
}

TEST(SummaryRangesTest, RejectsUnsortedInput) {
  const std::vector<std::vector<int>> unsorted_inputs = {
      {3, 1, 2},
      {2, 1},
      {0, -1},
      {-5, -4, -6},
  };

  for (const auto& nums : unsorted_inputs) {
    const auto result = leet_code::summary_ranges(nums);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(),
              leet_code::SummaryRangesError::kUnsortedOrDuplicateInput);
  }
}

TEST(SummaryRangesTest, RejectsWrapAroundTrapIntMaxThenIntMin) {
  const std::vector<int> nums = {
      std::numeric_limits<int>::max(),
      std::numeric_limits<int>::min(),
  };

  const auto result = leet_code::summary_ranges(nums);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(),
            leet_code::SummaryRangesError::kUnsortedOrDuplicateInput);
}

// ============================================================================
// TIER 3: STRUCTURAL INVARIANTS & NON-MUTATION
// ============================================================================

TEST(SummaryRangesTest, InvariantsAcrossDeterministicPatternsAndMaxLength20) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  std::vector<int> max_len_contiguous(20uz);
  std::ranges::iota(max_len_contiguous, -10);

  std::vector<int> max_len_sparse(20uz);
  for (size_t i = 0uz; i < 20uz; ++i) {
    max_len_sparse[i] = static_cast<int>(i * 2uz);
  }

  const std::vector<std::vector<int>> patterns = {
      {},
      {42},
      {7, 8},
      {7, 9},
      {-5, -4, -2, 0, 1, 2, 5, 7, 8},
      max_len_contiguous,
      max_len_sparse,
      // Max LeetCode constraint length 20 with mixed runs and gaps.
      {-100, -99, -98, -95, -94, -90, -50, -49, -48, -47,
       -1,   0,   1,   5,   10,  11,  20,  21,  22,  1000},
      // Max LeetCode constraint length 20 spanning 32-bit signed extremes.
      {kMin,     kMin + 1, kMin + 2, kMin + 5, -100,     -1,  0,
       1,        10,       11,       20,       30,       31,  32,
       kMax - 6, kMax - 5, kMax - 3, kMax - 2, kMax - 1, kMax},
  };

  for (const auto& pattern : patterns) {
    const auto result = leet_code::summary_ranges(pattern);
    ASSERT_TRUE(result.has_value());
    VerifyRangeInvariants(pattern, *result);
  }
}

TEST(SummaryRangesTest, SolutionDoesNotMutateInput) {
  std::vector<int> nums = {0, 1, 2, 4, 5, 7};
  const std::vector<int> original = nums;
  const std::vector<std::string> expected = {"0->2", "4->5", "7"};

  leet_code::Solution solution;
  const auto actual = solution.summaryRanges(nums);
  EXPECT_EQ(actual, expected);
  EXPECT_EQ(nums, original);
}

// ============================================================================
// TIER 4: STRESS WORKLOADS & ONLINE JUDGE SIMULATION
// ============================================================================

TEST(SummaryRangesTest, LargeScale100kDisjointTwoElementRanges) {
  constexpr size_t kPairCount = 50'000uz;
  std::vector<int> nums(kPairCount * 2uz);
  for (size_t i = 0uz; i < kPairCount; ++i) {
    const auto base = static_cast<int>(i * 3uz);
    nums[2uz * i] = base;
    nums[(2uz * i) + 1uz] = base + 1;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto result = leet_code::summary_ranges(nums);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), kPairCount);
  EXPECT_EQ((*result)[0uz], "0->1");
  EXPECT_EQ((*result)[25'000uz], "75000->75001");
  EXPECT_EQ((*result)[kPairCount - 1uz], "149997->149998");
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

TEST(SummaryRangesTest, LargeScale100kContiguousElements) {
  constexpr size_t kSize = 100'000uz;
  std::vector<int> nums(kSize);
  std::ranges::iota(nums, -50'000);

  const auto start = std::chrono::steady_clock::now();
  const auto result = leet_code::summary_ranges(nums);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1uz);
  EXPECT_EQ((*result)[0uz], "-50000->49999");
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

struct TestCase {
  std::vector<int> nums;
  std::vector<std::string> expected;
};

TEST(SummaryRangesTest, LeetCodeOnlineJudgeSimulationHarness) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  const std::vector<TestCase> test_cases = {
      {.nums = {}, .expected = {}},
      {.nums = {0}, .expected = {"0"}},
      {.nums = {0, 1, 2, 4, 5, 7}, .expected = {"0->2", "4->5", "7"}},
      {.nums = {0, 2, 3, 4, 6, 8, 9}, .expected = {"0", "2->4", "6", "8->9"}},
      {.nums = {kMin, kMin + 1, kMax},
       .expected = {"-2147483648->-2147483647", "2147483647"}},
      {.nums = {-5, -4, -3, -1, 0, 2, 3, 4},
       .expected = {"-5->-3", "-1->0", "2->4"}},
      {.nums = {1, 3, 5, 7, 9}, .expected = {"1", "3", "5", "7", "9"}},
      {.nums = {1,  2,  3,  5,  6,  8,  10, 11, 12, 13,
                15, 16, 18, 20, 21, 22, 24, 25, 26, 28},
       .expected = {"1->3", "5->6", "8", "10->13", "15->16", "18", "20->22",
                    "24->26", "28"}},
  };

  leet_code::Solution solution;
  for (const auto& test_case : test_cases) {
    std::vector<int> nums_copy = test_case.nums;
    const auto actual = solution.summaryRanges(nums_copy);
    EXPECT_EQ(actual, test_case.expected);
    EXPECT_EQ(nums_copy, test_case.nums);
    VerifyRangeInvariants(test_case.nums, actual);
  }
}

}  // namespace
