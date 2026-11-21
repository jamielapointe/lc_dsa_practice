/// @file merge_sorted_array_test.cpp
/// @brief Comprehensive 4-tier Google Test suite for LeetCode #88 (Merge Sorted
/// Array).
///
/// Tiers:
/// - Tier 1: Canonical LeetCode examples and non-owning std::span subspan view.
/// - Tier 2: Boundary, edge, extreme int limits, trailing sentinel slack,
///   precondition validation, check ordering, and error atomicity.
/// - Tier 3: Algebraic invariants and seeded differential naive-sort oracle.
/// - Tier 4: Max-constraint (200+200), large linear-time (50k+50k) stress
///   workloads, and OJ simulation harness.

#include "leet_code/merge_sorted_array.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

/// @brief Independent naive oracle: copies `nums1_prefix` and `nums2` and sorts
/// with `std::ranges::sort`.
[[nodiscard]] auto merge_reference_oracle(std::span<const int> nums1_prefix,
                                          std::span<const int> nums2)
    -> std::vector<int> {
  std::vector<int> out;
  out.reserve(nums1_prefix.size() + nums2.size());
  out.insert(out.end(), nums1_prefix.begin(), nums1_prefix.end());
  out.insert(out.end(), nums2.begin(), nums2.end());
  std::ranges::sort(out);
  return out;
}

/// @brief Verifies both `merge_sorted_array` and `Solution::merge` succeed,
/// match `expected`, leave `nums2` untouched, and perform zero reallocation.
void ExpectMergeSuccess(const std::vector<int>& nums1, size_t m,
                        const std::vector<int>& nums2, size_t n,
                        const std::vector<int>& expected) {
  std::vector<int> modern_nums1 = nums1;
  std::vector<int> modern_nums2 = nums2;
  const auto res =
      leet_code::merge_sorted_array(modern_nums1, m, modern_nums2, n);
  EXPECT_TRUE(res.has_value());
  if (res.has_value()) {
    EXPECT_EQ(modern_nums1, expected);
  }
  EXPECT_EQ(modern_nums2, nums2);

  if (m <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
      n <= static_cast<size_t>(std::numeric_limits<int>::max())) {
    std::vector<int> sol_nums1 = nums1;
    std::vector<int> sol_nums2 = nums2;
    const int* const data_before = sol_nums1.data();
    const size_t capacity_before = sol_nums1.capacity();

    leet_code::Solution solution;
    solution.merge(sol_nums1, static_cast<int>(m), sol_nums2,
                   static_cast<int>(n));
    EXPECT_EQ(sol_nums1, expected);
    EXPECT_EQ(sol_nums2, nums2);
    EXPECT_EQ(sol_nums1.data(), data_before);
    EXPECT_EQ(sol_nums1.capacity(), capacity_before);
  }
}

/// @brief Verifies `merge_sorted_array` returns `expected_error` atomically
/// (without mutating `nums1` or `nums2`), and `Solution::merge` leaves both
/// vectors unmodified when `m` and `n` fit in `int`.
void ExpectMergeError(const std::vector<int>& nums1, size_t m,
                      const std::vector<int>& nums2, size_t n,
                      leet_code::MergeSortedArrayError expected_error) {
  std::vector<int> modern_nums1 = nums1;
  std::vector<int> modern_nums2 = nums2;
  const auto res =
      leet_code::merge_sorted_array(modern_nums1, m, modern_nums2, n);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), expected_error);
  EXPECT_EQ(modern_nums1, nums1);
  EXPECT_EQ(modern_nums2, nums2);

  if (m <= static_cast<size_t>(std::numeric_limits<int>::max()) &&
      n <= static_cast<size_t>(std::numeric_limits<int>::max())) {
    std::vector<int> sol_nums1 = nums1;
    std::vector<int> sol_nums2 = nums2;
    leet_code::Solution solution;
    solution.merge(sol_nums1, static_cast<int>(m), sol_nums2,
                   static_cast<int>(n));
    EXPECT_EQ(sol_nums1, nums1);
    EXPECT_EQ(sol_nums2, nums2);
  }
}

// ============================================================================
// TIER 1: CANONICAL FEATURE COVERAGE
// ============================================================================

TEST(MergeSortedArrayTest, Example1Canonical) {
  ExpectMergeSuccess({1, 2, 3, 0, 0, 0}, 3uz, {2, 5, 6}, 3uz,
                     {1, 2, 2, 3, 5, 6});
}

TEST(MergeSortedArrayTest, Example2Canonical) {
  ExpectMergeSuccess({1}, 1uz, {}, 0uz, {1});
}

TEST(MergeSortedArrayTest, Example3Canonical) {
  ExpectMergeSuccess({0}, 0uz, {1}, 1uz, {1});
}

TEST(MergeSortedArrayTest, NonOwningSubspanViewPreservesCanaries) {
  constexpr int kCanaryLeft = -999'999;
  constexpr int kCanaryRight = 999'999;
  std::array<int, 8uz> backing1 = {
      kCanaryLeft, 1, 4, 7, 123, -456, 888, kCanaryRight,
  };
  const std::array<int, 5uz> backing2 = {-777, 2, 5, 9, 777};

  const std::span<int> sub1 = std::span<int>{backing1}.subspan(1uz, 6uz);
  const std::span<const int> sub2 =
      std::span<const int>{backing2}.subspan(1uz, 3uz);

  const auto res = leet_code::merge_sorted_array(sub1, 3uz, sub2, 3uz);
  ASSERT_TRUE(res.has_value());

  const std::array<int, 8uz> expected_backing1 = {
      kCanaryLeft, 1, 2, 4, 5, 7, 9, kCanaryRight,
  };
  EXPECT_EQ(backing1, expected_backing1);
}

// ============================================================================
// TIER 2: BOUNDARY, EDGE & PRECONDITION CASES
// ============================================================================

TEST(MergeSortedArrayTest, VacuousEmptyInputsSucceedAsNoOp) {
  ExpectMergeSuccess({}, 0uz, {}, 0uz, {});
}

TEST(MergeSortedArrayTest, EmptyNums1PrefixCopiesAllOfNums2) {
  ExpectMergeSuccess({0, 0, 0, 0}, 0uz, {-5, 0, 3, 9}, 4uz, {-5, 0, 3, 9});
}

TEST(MergeSortedArrayTest, EmptyNums2PreservesNums1) {
  ExpectMergeSuccess({-10, -2, 0, 4, 8}, 5uz, {}, 0uz, {-10, -2, 0, 4, 8});
}

TEST(MergeSortedArrayTest, SingleElementPairsAllOrderings) {
  {
    SCOPED_TRACE("a < b");
    ExpectMergeSuccess({1, 0}, 1uz, {2}, 1uz, {1, 2});
  }
  {
    SCOPED_TRACE("a == b");
    ExpectMergeSuccess({5, 0}, 1uz, {5}, 1uz, {5, 5});
  }
  {
    SCOPED_TRACE("a > b");
    ExpectMergeSuccess({9, 0}, 1uz, {3}, 1uz, {3, 9});
  }
}

TEST(MergeSortedArrayTest, AllNums2LessThanNums1ExhaustsNums1First) {
  {
    SCOPED_TRACE("m=3, n=4");
    ExpectMergeSuccess({10, 20, 30, 0, 0, 0, 0}, 3uz, {-5, 0, 1, 9}, 4uz,
                       {-5, 0, 1, 9, 10, 20, 30});
  }
  {
    SCOPED_TRACE("m=1, n=3");
    ExpectMergeSuccess({100, 0, 0, 0}, 1uz, {1, 2, 3}, 3uz, {1, 2, 3, 100});
  }
}

TEST(MergeSortedArrayTest, AllNums2GreaterThanNums1ExhaustsNums2First) {
  {
    SCOPED_TRACE("m=3, n=3");
    ExpectMergeSuccess({-10, -5, 0, 0, 0, 0}, 3uz, {1, 2, 3}, 3uz,
                       {-10, -5, 0, 1, 2, 3});
  }
  {
    SCOPED_TRACE("m=3, n=1");
    ExpectMergeSuccess({1, 2, 3, 0}, 3uz, {100}, 1uz, {1, 2, 3, 100});
  }
}

TEST(MergeSortedArrayTest, HeavilyInterleavedSequences) {
  {
    SCOPED_TRACE("odd in nums1, even in nums2");
    ExpectMergeSuccess({1, 3, 5, 7, 9, 0, 0, 0, 0, 0}, 5uz, {2, 4, 6, 8, 10},
                       5uz, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  }
  {
    SCOPED_TRACE("even in nums1, odd in nums2");
    ExpectMergeSuccess({2, 4, 6, 8, 10, 0, 0, 0, 0, 0}, 5uz, {1, 3, 5, 7, 9},
                       5uz, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
  }
}

TEST(MergeSortedArrayTest, AllDuplicatesAndEqualElements) {
  {
    SCOPED_TRACE("all equal non-zero");
    ExpectMergeSuccess({7, 7, 7, 0, 0}, 3uz, {7, 7}, 2uz, {7, 7, 7, 7, 7});
  }
  {
    SCOPED_TRACE("all zeros");
    ExpectMergeSuccess({0, 0, 0, 0}, 2uz, {0, 0}, 2uz, {0, 0, 0, 0});
  }
  {
    SCOPED_TRACE("duplicate clusters across arrays");
    ExpectMergeSuccess({-2, -2, 0, 3, 3, 0, 0, 0, 0}, 5uz, {-2, 0, 0, 3}, 4uz,
                       {-2, -2, -2, 0, 0, 0, 3, 3, 3});
  }
}

TEST(MergeSortedArrayTest, NegativeNumbersAndExtremeIntLimits) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();
  constexpr int kBound = 1'000'000'000;

  {
    SCOPED_TRACE("official +/-1e9 bounds");
    ExpectMergeSuccess({-kBound, -500, kBound, 0, 0, 0}, 3uz,
                       {-kBound, 0, kBound}, 3uz,
                       {-kBound, -kBound, -500, 0, kBound, kBound});
  }
  {
    SCOPED_TRACE("INT32_MIN and INT32_MAX extremes");
    ExpectMergeSuccess({kMin, -1, kMax, 0, 0, 0, 0}, 3uz, {kMin, 0, 1, kMax},
                       4uz, {kMin, kMin, -1, 0, 1, kMax, kMax});
  }
  {
    SCOPED_TRACE("single max vs min");
    ExpectMergeSuccess({kMax, 0}, 1uz, {kMin}, 1uz, {kMin, kMax});
  }
  {
    SCOPED_TRACE("single min vs max");
    ExpectMergeSuccess({kMin, 0}, 1uz, {kMax}, 1uz, {kMin, kMax});
  }
}

TEST(MergeSortedArrayTest, NonZeroUnsortedTrailingSlackIgnoredAndOverwritten) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  {
    SCOPED_TRACE("m > 0 with unsorted extreme garbage slack");
    ExpectMergeSuccess({1, 5, 9, kMax, kMin, -999}, 3uz, {2, 6, 8}, 3uz,
                       {1, 2, 5, 6, 8, 9});
  }
  {
    SCOPED_TRACE("m == 0 with unsorted extreme garbage slack");
    ExpectMergeSuccess({kMax, -999, kMin}, 0uz, {-10, 0, 10}, 3uz,
                       {-10, 0, 10});
  }
  {
    SCOPED_TRACE("trailing slack strictly smaller than nums1[m-1]");
    ExpectMergeSuccess({10, 20, -100, -200}, 2uz, {15, 25}, 2uz,
                       {10, 15, 20, 25});
  }
}

TEST(MergeSortedArrayTest, PreconditionRejectsInvalidLengthsAndSizeOverflow) {
  constexpr size_t kSizeMax = std::numeric_limits<size_t>::max();
  constexpr auto kInvalidLength =
      leet_code::MergeSortedArrayError::kInvalidLength;

  {
    SCOPED_TRACE("nums1 too short (m + n > nums1.size())");
    ExpectMergeError({1, 2, 0}, 2uz, {3, 4}, 2uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("m > nums1.size()");
    ExpectMergeError({1, 2}, 3uz, {}, 0uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums1 too long (m + n < nums1.size())");
    ExpectMergeError({1, 2, 0, 0}, 2uz, {3}, 1uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums1 non-empty when m == 0 and n == 0");
    ExpectMergeError({0}, 0uz, {}, 0uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums2 too short (nums2.size() < n)");
    ExpectMergeError({1, 0, 0}, 1uz, {2}, 2uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums2 too long (nums2.size() > n)");
    ExpectMergeError({1, 0}, 1uz, {2, 3}, 1uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums2 non-empty when n == 0");
    ExpectMergeError({1}, 1uz, {2}, 0uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("m + n size_t overflow (m = max, n = 1)");
    ExpectMergeError({1}, kSizeMax, {2}, 1uz, kInvalidLength);
  }
  {
    SCOPED_TRACE(
        "m + n size_t wraparound equals nums1.size() (m = max, n = 2)");
    ExpectMergeError({1}, kSizeMax, {2, 3}, 2uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("m + n size_t wraparound to zero (m = 1, n = max)");
    ExpectMergeError({}, 1uz, {}, kSizeMax, kInvalidLength);
  }
}

TEST(MergeSortedArrayTest, SolutionMergeNegativeLengthsLeaveInputsUntouched) {
  constexpr int kIntMin = std::numeric_limits<int>::min();
  struct NegativeCase {
    std::vector<int> nums1;
    int m = 0;
    std::vector<int> nums2;
    int n = 0;
  };
  const std::vector<NegativeCase> cases = {
      {.nums1 = {1, 2, 0}, .m = -1, .nums2 = {3}, .n = 1},
      {.nums1 = {1, 2, 0}, .m = 1, .nums2 = {3}, .n = -1},
      {.nums1 = {5, 4, 3}, .m = -2, .nums2 = {2, 1}, .n = -3},
      {.nums1 = {1, 2}, .m = kIntMin, .nums2 = {}, .n = 0},
      {.nums1 = {1, 2}, .m = 0, .nums2 = {3, 4}, .n = kIntMin},
  };

  leet_code::Solution solution;
  for (size_t i = 0uz; i < cases.size(); ++i) {
    SCOPED_TRACE(i);
    std::vector<int> nums1 = cases[i].nums1;
    std::vector<int> nums2 = cases[i].nums2;
    solution.merge(nums1, cases[i].m, nums2, cases[i].n);
    EXPECT_EQ(nums1, cases[i].nums1);
    EXPECT_EQ(nums2, cases[i].nums2);
  }
}

TEST(MergeSortedArrayTest, PreconditionRejectsUnsortedInputsAtomically) {
  constexpr auto kUnsortedInput =
      leet_code::MergeSortedArrayError::kUnsortedInput;

  {
    SCOPED_TRACE("unsorted nums1[..m] at start");
    ExpectMergeError({3, 1, 2, 99}, 3uz, {4}, 1uz, kUnsortedInput);
  }
  {
    SCOPED_TRACE("unsorted nums1[..m] at end");
    ExpectMergeError({1, 5, 4, -7, 8}, 3uz, {2, 3}, 2uz, kUnsortedInput);
  }
  {
    SCOPED_TRACE("unsorted nums1[..m] with n == 0");
    ExpectMergeError({2, 1}, 2uz, {}, 0uz, kUnsortedInput);
  }
  {
    SCOPED_TRACE("unsorted nums2[..n]");
    ExpectMergeError({1, 2, 77, -88}, 2uz, {5, 3}, 2uz, kUnsortedInput);
  }
  {
    SCOPED_TRACE("unsorted nums2[..n] with m == 0");
    ExpectMergeError({42, -1, 7}, 0uz, {1, 3, 2}, 3uz, kUnsortedInput);
  }
  {
    SCOPED_TRACE("both nums1[..m] and nums2[..n] unsorted");
    ExpectMergeError({5, 1, 10, -20}, 2uz, {4, 2}, 2uz, kUnsortedInput);
  }
}

TEST(MergeSortedArrayTest, PreconditionOrderLengthCheckedBeforeSortedness) {
  constexpr auto kInvalidLength =
      leet_code::MergeSortedArrayError::kInvalidLength;

  {
    SCOPED_TRACE("nums1 too short AND both inputs unsorted");
    ExpectMergeError({5, 1, 0}, 2uz, {4, 2}, 2uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums1 too long AND nums2 wrong length AND both unsorted");
    ExpectMergeError({5, 1, 9, 8}, 2uz, {4, 2}, 1uz, kInvalidLength);
  }
  {
    SCOPED_TRACE("nums1.size() == m + n, nums2.size() != n, AND both unsorted");
    ExpectMergeError({5, 1, 0}, 2uz, {4, 2}, 1uz, kInvalidLength);
  }
}

// ============================================================================
// TIER 3: ALGEBRAIC INVARIANTS & DIFFERENTIAL REFERENCE ORACLE
// ============================================================================

TEST(MergeSortedArrayTest, DifferentialOracleMatchesNaiveSortAcrossRandomData) {
  constexpr auto kSeed = uint64_t{0x2026111900000088};
  constexpr size_t kTrialCount = 250uz;
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<size_t> len_dist(0uz, 40uz);
  std::uniform_int_distribution<int> regime_dist(0, 3);
  std::uniform_int_distribution<int> small_dist(-5, 5);
  std::uniform_int_distribution<int> medium_dist(-1'000, 1'000);
  std::uniform_int_distribution<int> wide_dist(-1'000'000'000, 1'000'000'000);
  std::uniform_int_distribution<int> garbage_dist(kMin, kMax);
  constexpr std::array<int, 6uz> kExtremes = {kMin, kMin + 1, -1, 0, 1, kMax};
  std::uniform_int_distribution<size_t> extreme_idx_dist(
      0uz, kExtremes.size() - 1uz);

  auto draw_value = [&](int regime) -> int {
    switch (regime) {
      case 0:
        return small_dist(rng);
      case 1:
        return medium_dist(rng);
      case 2:
        return wide_dist(rng);
      default:
        return kExtremes[extreme_idx_dist(rng)];
    }
  };

  leet_code::Solution solution;

  for (size_t trial = 0uz; trial < kTrialCount; ++trial) {
    SCOPED_TRACE(trial);
    const size_t m = len_dist(rng);
    const size_t n = len_dist(rng);
    const int regime = regime_dist(rng);

    std::vector<int> prefix1;
    prefix1.reserve(m);
    for (size_t i = 0uz; i < m; ++i) {
      prefix1.push_back(draw_value(regime));
    }
    std::ranges::sort(prefix1);

    std::vector<int> nums2;
    nums2.reserve(n);
    for (size_t i = 0uz; i < n; ++i) {
      nums2.push_back(draw_value(regime));
    }
    std::ranges::sort(nums2);

    const std::vector<int> expected = merge_reference_oracle(prefix1, nums2);

    std::vector<int> nums1 = prefix1;
    nums1.reserve(m + n);
    for (size_t i = 0uz; i < n; ++i) {
      nums1.push_back(garbage_dist(rng));
    }

    std::vector<int> modern_nums1 = nums1;
    const std::vector<int> original_nums2 = nums2;
    const auto res = leet_code::merge_sorted_array(modern_nums1, m, nums2, n);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(std::ranges::is_sorted(modern_nums1));
    EXPECT_EQ(modern_nums1, expected);
    EXPECT_EQ(nums2, original_nums2);

    std::vector<int> sol_nums1 = nums1;
    std::vector<int> sol_nums2 = nums2;
    const int* const data_before = sol_nums1.data();
    solution.merge(sol_nums1, static_cast<int>(m), sol_nums2,
                   static_cast<int>(n));
    EXPECT_EQ(sol_nums1, expected);
    EXPECT_EQ(sol_nums2, original_nums2);
    EXPECT_EQ(sol_nums1.data(), data_before);
  }
}

TEST(MergeSortedArrayTest, IdempotenceAndCommutativityInvariants) {
  constexpr auto kSeed = uint64_t{0x5EED008820260003};
  constexpr size_t kTrialCount = 100uz;

  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<size_t> len_dist(0uz, 30uz);
  std::uniform_int_distribution<int> val_dist(-50, 50);

  for (size_t trial = 0uz; trial < kTrialCount; ++trial) {
    SCOPED_TRACE(trial);
    const size_t m = len_dist(rng);
    const size_t n = len_dist(rng);

    std::vector<int> seq_a;
    seq_a.reserve(m);
    for (size_t i = 0uz; i < m; ++i) {
      seq_a.push_back(val_dist(rng));
    }
    std::ranges::sort(seq_a);

    std::vector<int> seq_b;
    seq_b.reserve(n);
    for (size_t i = 0uz; i < n; ++i) {
      seq_b.push_back(val_dist(rng));
    }
    std::ranges::sort(seq_b);

    std::vector<int> merge_ab = seq_a;
    merge_ab.resize(m + n, -12345);
    const auto res_ab = leet_code::merge_sorted_array(merge_ab, m, seq_b, n);
    ASSERT_TRUE(res_ab.has_value());

    std::vector<int> merge_ba = seq_b;
    merge_ba.resize(m + n, 54321);
    const auto res_ba = leet_code::merge_sorted_array(merge_ba, n, seq_a, m);
    ASSERT_TRUE(res_ba.has_value());

    // Commutativity: merge(A, B) == merge(B, A).
    EXPECT_EQ(merge_ab, merge_ba);

    // Idempotence with empty nums2: merge(merged, {}) == merged.
    const std::vector<int> snapshot = merge_ab;
    const auto res_idemp =
        leet_code::merge_sorted_array(merge_ab, merge_ab.size(), {}, 0uz);
    ASSERT_TRUE(res_idemp.has_value());
    EXPECT_EQ(merge_ab, snapshot);
  }
}

// ============================================================================
// TIER 4: STRESS WORKLOADS & ONLINE JUDGE SIMULATION
// ============================================================================

TEST(MergeSortedArrayTest, MaxConstraint200By200Workloads) {
  constexpr size_t kMaxSide = 200uz;

  {
    SCOPED_TRACE("interleaved m=200, n=200 with trailing garbage");
    std::vector<int> nums1(kMaxSide * 2uz, -999);
    std::vector<int> nums2(kMaxSide, 0);
    std::vector<int> expected(kMaxSide * 2uz, 0);
    for (size_t i = 0uz; i < kMaxSide; ++i) {
      const int idx = static_cast<int>(i);
      nums1[i] = idx * 2;
      nums2[i] = (idx * 2) + 1;
      expected[i * 2uz] = idx * 2;
      expected[(i * 2uz) + 1uz] = (idx * 2) + 1;
    }
    ExpectMergeSuccess(nums1, kMaxSide, nums2, kMaxSide, expected);
  }
  {
    SCOPED_TRACE("disjoint nums2 < nums1 (m=200, n=200)");
    std::vector<int> nums1(kMaxSide * 2uz, 777);
    std::vector<int> nums2(kMaxSide, 0);
    std::vector<int> expected(kMaxSide * 2uz, 0);
    for (size_t i = 0uz; i < kMaxSide; ++i) {
      const int idx = static_cast<int>(i);
      nums1[i] = idx + static_cast<int>(kMaxSide);
      nums2[i] = idx;
    }
    for (size_t i = 0uz; i < kMaxSide * 2uz; ++i) {
      expected[i] = static_cast<int>(i);
    }
    ExpectMergeSuccess(nums1, kMaxSide, nums2, kMaxSide, expected);
  }
  {
    SCOPED_TRACE("m=0, n=200");
    std::vector<int> nums1(kMaxSide, 42);
    std::vector<int> nums2(kMaxSide, 0);
    for (size_t i = 0uz; i < kMaxSide; ++i) {
      nums2[i] = static_cast<int>(i) - 100;
    }
    ExpectMergeSuccess(nums1, 0uz, nums2, kMaxSide, nums2);
  }
  {
    SCOPED_TRACE("m=200, n=0");
    std::vector<int> nums1(kMaxSide, 0);
    for (size_t i = 0uz; i < kMaxSide; ++i) {
      nums1[i] = static_cast<int>(i) - 100;
    }
    ExpectMergeSuccess(nums1, kMaxSide, {}, 0uz, nums1);
  }
}

TEST(MergeSortedArrayTest, LargeLinearTime50kBy50kAlternatingWorkload) {
  constexpr size_t kSide = 50'000uz;
  std::vector<int> nums1(kSide * 2uz, -1);
  std::vector<int> nums2(kSide, 0);
  std::vector<int> expected(kSide * 2uz, 0);

  for (size_t i = 0uz; i < kSide; ++i) {
    const int val = static_cast<int>(i);
    nums1[i] = val * 2;
    nums2[i] = (val * 2) + 1;
    expected[i * 2uz] = val * 2;
    expected[(i * 2uz) + 1uz] = (val * 2) + 1;
  }

  const auto start = std::chrono::steady_clock::now();
  const auto res = leet_code::merge_sorted_array(nums1, kSide, nums2, kSide);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(nums1, expected);
}

TEST(MergeSortedArrayTest, LargeLinearTime50kBy50kReversedBlockWorkload) {
  constexpr size_t kSide = 50'000uz;
  std::vector<int> nums1(kSide * 2uz, 999'999);
  std::vector<int> nums2(kSide, 0);
  std::vector<int> expected(kSide * 2uz, 0);

  for (size_t i = 0uz; i < kSide; ++i) {
    const int val = static_cast<int>(i);
    nums1[i] = val + static_cast<int>(kSide);
    nums2[i] = val;
  }
  for (size_t i = 0uz; i < kSide * 2uz; ++i) {
    expected[i] = static_cast<int>(i);
  }

  const auto start = std::chrono::steady_clock::now();
  const auto res = leet_code::merge_sorted_array(nums1, kSide, nums2, kSide);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(nums1, expected);
}

TEST(MergeSortedArrayTest, LargeLinearTime50kBy50kRandomDuplicatesWorkload) {
  constexpr size_t kSide = 50'000uz;
  constexpr auto kSeed = uint64_t{0xBEEF008820260004};
  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<int> val_dist(-1'000'000, 1'000'000);

  std::vector<int> prefix1;
  prefix1.reserve(kSide);
  std::vector<int> nums2;
  nums2.reserve(kSide);
  for (size_t i = 0uz; i < kSide; ++i) {
    prefix1.push_back(val_dist(rng));
    nums2.push_back(val_dist(rng));
  }
  std::ranges::sort(prefix1);
  std::ranges::sort(nums2);

  const std::vector<int> expected = merge_reference_oracle(prefix1, nums2);

  std::vector<int> nums1 = prefix1;
  nums1.resize(kSide * 2uz, 0);

  const auto start = std::chrono::steady_clock::now();
  const auto res = leet_code::merge_sorted_array(nums1, kSide, nums2, kSide);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(std::ranges::is_sorted(nums1));
  EXPECT_EQ(nums1, expected);
}

struct TestCase {
  std::vector<int> nums1;
  int m = 0;
  std::vector<int> nums2;
  int n = 0;
  std::vector<int> expected;
};

TEST(MergeSortedArrayTest, LeetCodeOnlineJudgeSimulationHarness) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  const std::vector<TestCase> test_cases = {
      {.nums1 = {1, 2, 3, 0, 0, 0},
       .m = 3,
       .nums2 = {2, 5, 6},
       .n = 3,
       .expected = {1, 2, 2, 3, 5, 6}},
      {.nums1 = {1}, .m = 1, .nums2 = {}, .n = 0, .expected = {1}},
      {.nums1 = {0}, .m = 0, .nums2 = {1}, .n = 1, .expected = {1}},
      {.nums1 = {}, .m = 0, .nums2 = {}, .n = 0, .expected = {}},
      {.nums1 = {4, 5, 6, 0, 0, 0},
       .m = 3,
       .nums2 = {1, 2, 3},
       .n = 3,
       .expected = {1, 2, 3, 4, 5, 6}},
      {.nums1 = {-10, -5, 0, 0, 0},
       .m = 3,
       .nums2 = {-7, 5},
       .n = 2,
       .expected = {-10, -7, -5, 0, 5}},
      {.nums1 = {2, 2, 2, 99, -99},
       .m = 3,
       .nums2 = {2, 2},
       .n = 2,
       .expected = {2, 2, 2, 2, 2}},
      {.nums1 = {kMin, kMax, 0, 0},
       .m = 2,
       .nums2 = {kMin, kMax},
       .n = 2,
       .expected = {kMin, kMin, kMax, kMax}},
      // Error cases on Solution::merge leave nums1 untouched.
      {.nums1 = {3, 1, 0}, .m = 2, .nums2 = {2}, .n = 1, .expected = {3, 1, 0}},
      {.nums1 = {1, 2, 0},
       .m = -1,
       .nums2 = {3},
       .n = 1,
       .expected = {1, 2, 0}},
  };

  leet_code::Solution solution;
  for (size_t i = 0uz; i < test_cases.size(); ++i) {
    SCOPED_TRACE(i);
    std::vector<int> nums1 = test_cases[i].nums1;
    std::vector<int> nums2 = test_cases[i].nums2;
    solution.merge(nums1, test_cases[i].m, nums2, test_cases[i].n);
    EXPECT_EQ(nums1, test_cases[i].expected);
    EXPECT_EQ(nums2, test_cases[i].nums2);
  }
}

}  // namespace
