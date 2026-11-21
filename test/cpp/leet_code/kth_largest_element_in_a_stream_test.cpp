/// @file kth_largest_element_in_a_stream_test.cpp
/// @brief Comprehensive 4-tier Google Test suite for LeetCode #703 (Kth Largest
/// Element in a Stream).
///
/// Tiers:
/// - Tier 1: Official LeetCode Examples 1 & 2 against both `KthLargestStream`
///   and `KthLargest`, plus non-owning `std::span` subspan view creation.
/// - Tier 2: Starved initial streams (`nums.size() == k - 1`), `k = 1` running
///   maximum, `k = n + 1` running minimum on first `add`, all-negative values,
///   all-identical duplicates, `INT32_MIN`/`INT32_MAX` extremes (catching
///   comparator subtraction overflow), and precondition error ordering.
/// - Tier 3: Caller container immutability, bounded `size() <= k` invariant,
///   monotonic non-decreasing `add()` outputs, and seeded PRNG differential
///   testing against a sorted-vector reference oracle.
/// - Tier 4: Programmatic max-constraint stress (`n = 10000`, `k = 5000`,
///   `q = 10000` `add` calls) with `std::chrono::steady_clock` guard and Online
///   Judge simulation harness.

#include "leet_code/kth_largest_element_in_a_stream.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <random>
#include <span>
#include <vector>

namespace {

/// @brief Naive sorted-descending reference oracle for the `k`th largest score.
class SortedPoolOracle {
 public:
  SortedPoolOracle(size_t k, std::span<const int> initial)
      : k_(k), pool_(initial.begin(), initial.end()) {}

  [[nodiscard]] auto add(int val) -> int {
    pool_.push_back(val);
    std::ranges::sort(pool_, std::greater<int>{});
    return pool_[k_ - 1uz];
  }

 private:
  size_t k_;
  std::vector<int> pool_;
};

/// @brief Exercises both `KthLargest` and `KthLargestStream` against `expected`
/// outputs, guarding `std::expected` with `ASSERT_TRUE(stream.has_value())`.
void VerifyBothInterfaces(int k, const std::vector<int>& initial,
                          std::span<const int> adds,
                          std::span<const int> expected) {
  ASSERT_EQ(adds.size(), expected.size());

  std::vector<int> wrapper_nums = initial;
  leet_code::KthLargest wrapper(k, wrapper_nums);
  EXPECT_EQ(wrapper_nums, initial);
  for (size_t i = 0uz; i < adds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(wrapper.add(adds[i]), expected[i]);
  }

  auto stream = leet_code::KthLargestStream::create(k, initial);
  ASSERT_TRUE(stream.has_value());
  const auto target_k = static_cast<size_t>(k);
  EXPECT_EQ(stream->k(), target_k);
  EXPECT_EQ(stream->size(), std::min(initial.size(), target_k));
  for (size_t i = 0uz; i < adds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(stream->add(adds[i]), expected[i]);
    EXPECT_EQ(stream->size(), target_k);
  }
}

/// @brief Verifies `KthLargestStream::create` fails with `expected_error`
/// without mutating caller input, and `KthLargest` degrades safely without
/// throwing or crashing.
void ExpectCreateError(int k, const std::vector<int>& initial,
                       leet_code::KthLargestError expected_error) {
  std::vector<int> mutable_nums = initial;
  leet_code::KthLargest wrapper(k, mutable_nums);
  EXPECT_EQ(mutable_nums, initial);
  EXPECT_EQ(wrapper.add(0), 0);

  const auto stream = leet_code::KthLargestStream::create(k, mutable_nums);
  ASSERT_FALSE(stream.has_value());
  EXPECT_EQ(stream.error(), expected_error);
  EXPECT_EQ(mutable_nums, initial);
}

// ============================================================================
// TIER 1: CANONICAL OFFICIAL EXAMPLES & SPAN VIEW
// ============================================================================

TEST(KthLargestElementInAStreamTest, Example1KthLargestStream) {
  constexpr std::array<int, 4uz> kInitial = {4, 5, 8, 2};
  constexpr std::array<int, 5uz> kAdds = {3, 5, 10, 9, 4};
  constexpr std::array<int, 5uz> kExpected = {4, 5, 5, 8, 8};

  auto stream = leet_code::KthLargestStream::create(3, kInitial);
  ASSERT_TRUE(stream.has_value());
  EXPECT_EQ(stream->k(), 3uz);
  EXPECT_EQ(stream->size(), 3uz);

  for (size_t i = 0uz; i < kAdds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(stream->add(kAdds[i]), kExpected[i]);
    EXPECT_EQ(stream->size(), 3uz);
  }
}

TEST(KthLargestElementInAStreamTest, Example1KthLargestWrapper) {
  std::vector<int> nums = {4, 5, 8, 2};
  const std::vector<int> original = nums;
  constexpr std::array<int, 5uz> kAdds = {3, 5, 10, 9, 4};
  constexpr std::array<int, 5uz> kExpected = {4, 5, 5, 8, 8};

  leet_code::KthLargest kth_largest(3, nums);
  EXPECT_EQ(nums, original);

  for (size_t i = 0uz; i < kAdds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(kth_largest.add(kAdds[i]), kExpected[i]);
  }
}

TEST(KthLargestElementInAStreamTest, Example2KthLargestStream) {
  constexpr std::array<int, 6uz> kInitial = {7, 7, 7, 7, 8, 3};
  constexpr std::array<int, 4uz> kAdds = {2, 10, 9, 9};
  constexpr std::array<int, 4uz> kExpected = {7, 7, 7, 8};

  auto stream = leet_code::KthLargestStream::create(4, kInitial);
  ASSERT_TRUE(stream.has_value());
  EXPECT_EQ(stream->k(), 4uz);
  EXPECT_EQ(stream->size(), 4uz);

  for (size_t i = 0uz; i < kAdds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(stream->add(kAdds[i]), kExpected[i]);
    EXPECT_EQ(stream->size(), 4uz);
  }
}

TEST(KthLargestElementInAStreamTest, Example2KthLargestWrapper) {
  std::vector<int> nums = {7, 7, 7, 7, 8, 3};
  const std::vector<int> original = nums;
  constexpr std::array<int, 4uz> kAdds = {2, 10, 9, 9};
  constexpr std::array<int, 4uz> kExpected = {7, 7, 7, 8};

  leet_code::KthLargest kth_largest(4, nums);
  EXPECT_EQ(nums, original);

  for (size_t i = 0uz; i < kAdds.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(kth_largest.add(kAdds[i]), kExpected[i]);
  }
}

TEST(KthLargestElementInAStreamTest, NonOwningSubspanViewIgnoresOuterCanaries) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();
  constexpr std::array<int, 6uz> kBacking = {kMax, 4, 5, 8, 2, kMin};
  const std::span<const int> sub =
      std::span<const int>{kBacking}.subspan(1uz, 4uz);

  auto stream = leet_code::KthLargestStream::create(3, sub);
  ASSERT_TRUE(stream.has_value());
  EXPECT_EQ(stream->add(3), 4);
  EXPECT_EQ(stream->add(10), 5);
  EXPECT_EQ(stream->add(9), 8);
}

// ============================================================================
// TIER 2: BOUNDARY, EDGE CASES & PRECONDITION VALIDATION
// ============================================================================

TEST(KthLargestElementInAStreamTest, StarvedInitialStreamKEqualsOneEmptyNums) {
  constexpr std::array<int, 5uz> kAdds = {-5, -10, 0, 42, 42};
  constexpr std::array<int, 5uz> kExpected = {-5, -5, 0, 42, 42};
  VerifyBothInterfaces(1, {}, kAdds, kExpected);
}

TEST(KthLargestElementInAStreamTest,
     StarvedInitialStreamKEqualsThreeTwoElements) {
  constexpr std::array<int, 5uz> kAdds = {5, 15, 25, 15, 30};
  constexpr std::array<int, 5uz> kExpected = {5, 10, 15, 15, 20};
  VerifyBothInterfaces(3, {10, 20}, kAdds, kExpected);
}

TEST(KthLargestElementInAStreamTest, KEqualsOneTracksRunningMaximum) {
  constexpr std::array<int, 6uz> kAdds = {2, 4, 5, 0, 10, -100};
  constexpr std::array<int, 6uz> kExpected = {4, 4, 5, 5, 10, 10};
  VerifyBothInterfaces(1, {-3, 1, -7, 4}, kAdds, kExpected);
}

TEST(KthLargestElementInAStreamTest,
     KEqualsLengthPlusOneReturnsRunningMinimumOnFirstAdd) {
  constexpr std::array<int, 4uz> kAdds = {50, 5, 25, 100};
  constexpr std::array<int, 4uz> kExpected = {10, 10, 20, 25};
  VerifyBothInterfaces(5, {40, 10, 30, 20}, kAdds, kExpected);
}

TEST(KthLargestElementInAStreamTest, AllNegativeScores) {
  constexpr std::array<int, 5uz> kAdds = {-500, -75, -25, -10, -1};
  constexpr std::array<int, 5uz> kExpected = {-100, -75, -50, -25, -10};
  VerifyBothInterfaces(3, {-100, -50, -200, -10}, kAdds, kExpected);
}

TEST(KthLargestElementInAStreamTest, AllIdenticalDuplicates) {
  constexpr std::array<int, 6uz> kAdds = {7, 7, 7, 8, 8, 8};
  constexpr std::array<int, 6uz> kExpected = {7, 7, 7, 7, 7, 8};
  VerifyBothInterfaces(3, {7, 7, 7}, kAdds, kExpected);
}

TEST(KthLargestElementInAStreamTest, Int32MinAndMaxExtremesWithoutOverflow) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  {
    SCOPED_TRACE("k = 2 with INT32_MIN and INT32_MAX");
    constexpr std::array<int, 4uz> kAdds = {kMin, 0, kMax - 1, kMax};
    constexpr std::array<int, 4uz> kExpected = {kMin, 0, kMax - 1, kMax};
    VerifyBothInterfaces(2, {kMin, kMax}, kAdds, kExpected);
  }
  {
    SCOPED_TRACE("k = 3 starved with INT32_MIN and INT32_MIN + 1");
    constexpr std::array<int, 4uz> kAdds = {kMax, 0, kMax, kMax};
    constexpr std::array<int, 4uz> kExpected = {kMin, kMin + 1, 0, kMax};
    VerifyBothInterfaces(3, {kMin, kMin + 1}, kAdds, kExpected);
  }
}

TEST(KthLargestElementInAStreamTest,
     PreconditionRejectsInvalidKFirstEvenWhenNumsEmpty) {
  constexpr auto kInvalidK = leet_code::KthLargestError::kInvalidK;
  constexpr int kIntMin = std::numeric_limits<int>::min();

  {
    SCOPED_TRACE("k = 0 with empty nums checks k <= 0 before length");
    ExpectCreateError(0, {}, kInvalidK);
  }
  {
    SCOPED_TRACE("k = 0 with non-empty nums");
    ExpectCreateError(0, {1, 2, 3}, kInvalidK);
  }
  {
    SCOPED_TRACE("k = -1 with empty nums");
    ExpectCreateError(-1, {}, kInvalidK);
  }
  {
    SCOPED_TRACE("k = -5 with non-empty nums");
    ExpectCreateError(-5, {10, 20}, kInvalidK);
  }
  {
    SCOPED_TRACE("k = INT32_MIN");
    ExpectCreateError(kIntMin, {1, 2}, kInvalidK);
  }
}

TEST(KthLargestElementInAStreamTest,
     PreconditionRejectsInsufficientInitialElements) {
  constexpr auto kInsufficient =
      leet_code::KthLargestError::kInsufficientInitialElements;
  constexpr int kIntMax = std::numeric_limits<int>::max();

  {
    SCOPED_TRACE("k = 2, nums = {} (0 + 1 < 2)");
    ExpectCreateError(2, {}, kInsufficient);
  }
  {
    SCOPED_TRACE("k = 3, nums = {42} (1 + 1 < 3)");
    ExpectCreateError(3, {42}, kInsufficient);
  }
  {
    SCOPED_TRACE("k = 5, nums = {1, 2, 3} (3 + 1 < 5)");
    ExpectCreateError(5, {1, 2, 3}, kInsufficient);
  }
  {
    SCOPED_TRACE("k = INT32_MAX, nums = {1, 2, 3}");
    ExpectCreateError(kIntMax, {1, 2, 3}, kInsufficient);
  }
}

// ============================================================================
// TIER 3: INVARIANTS & DIFFERENTIAL REFERENCE ORACLE
// ============================================================================

TEST(KthLargestElementInAStreamTest, CallerInputContainerRemainsUnmodified) {
  std::vector<int> nums = {9, -4, 15, 0, 9, -100, 42};
  const std::vector<int> original = nums;
  const int* const data_before = nums.data();
  const size_t capacity_before = nums.capacity();

  leet_code::KthLargest wrapper(3, nums);
  EXPECT_EQ(nums, original);
  EXPECT_EQ(nums.data(), data_before);
  EXPECT_EQ(nums.capacity(), capacity_before);
  EXPECT_EQ(wrapper.add(10), 10);
  EXPECT_EQ(wrapper.add(50), 15);
  EXPECT_EQ(nums, original);

  auto stream = leet_code::KthLargestStream::create(3, nums);
  ASSERT_TRUE(stream.has_value());
  EXPECT_EQ(nums, original);
  EXPECT_EQ(nums.data(), data_before);
  EXPECT_EQ(stream->add(10), 10);
  EXPECT_EQ(stream->add(50), 15);
  EXPECT_EQ(nums, original);
}

TEST(KthLargestElementInAStreamTest,
     BoundedRetainedSizeAndMonotonicNonDecreasingAddOutputs) {
  constexpr std::array<int, 2uz> kStarvedInit = {20, -10};
  constexpr std::array<int, 10uz> kAdds = {
      -50, 0, -50, 15, 20, 100, -100, 25, 25, 200,
  };

  SortedPoolOracle oracle(3uz, kStarvedInit);
  auto stream = leet_code::KthLargestStream::create(3, kStarvedInit);
  ASSERT_TRUE(stream.has_value());
  EXPECT_EQ(stream->k(), 3uz);
  EXPECT_EQ(stream->size(), 2uz);
  EXPECT_LE(stream->size(), stream->k());

  int prev_kth = std::numeric_limits<int>::min();
  for (size_t i = 0uz; i < kAdds.size(); ++i) {
    SCOPED_TRACE(i);
    const int expected_kth = oracle.add(kAdds[i]);
    const int actual_kth = stream->add(kAdds[i]);
    EXPECT_EQ(actual_kth, expected_kth);
    EXPECT_GE(actual_kth, prev_kth);
    EXPECT_EQ(stream->size(), 3uz);
    EXPECT_LE(stream->size(), stream->k());
    prev_kth = actual_kth;
  }
}

TEST(KthLargestElementInAStreamTest,
     DifferentialSeededPrngMatchesSortedVectorOracle) {
  constexpr auto kSeed = uint64_t{0x2026112100000703};
  constexpr size_t kTrialCount = 100uz;
  constexpr size_t kQueriesPerTrial = 30uz;
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<int> k_dist(1, 25);
  std::uniform_int_distribution<size_t> extra_len_dist(0uz, 30uz);
  std::uniform_int_distribution<int> regime_dist(0, 2);
  std::uniform_int_distribution<int> narrow_dist(-5, 5);
  std::uniform_int_distribution<int> constraint_dist(-10'000, 10'000);
  constexpr std::array<int, 7uz> kExtremes = {
      kMin, kMin + 1, -1, 0, 1, kMax - 1, kMax,
  };
  std::uniform_int_distribution<size_t> extreme_idx_dist(
      0uz, kExtremes.size() - 1uz);

  auto draw_score = [&](int regime) -> int {
    if (regime == 0) {
      return narrow_dist(rng);
    }
    if (regime == 1) {
      return constraint_dist(rng);
    }
    return kExtremes[extreme_idx_dist(rng)];
  };

  for (size_t trial = 0uz; trial < kTrialCount; ++trial) {
    SCOPED_TRACE(trial);
    const int k = k_dist(rng);
    const auto target_k = static_cast<size_t>(k);
    const size_t initial_len = (target_k - 1uz) + extra_len_dist(rng);
    const int regime = regime_dist(rng);

    std::vector<int> initial;
    initial.reserve(initial_len);
    for (size_t i = 0uz; i < initial_len; ++i) {
      initial.push_back(draw_score(regime));
    }

    SortedPoolOracle oracle(target_k, initial);
    std::vector<int> wrapper_nums = initial;
    leet_code::KthLargest wrapper(k, wrapper_nums);

    std::vector<int> queries;
    queries.reserve(kQueriesPerTrial);
    std::vector<int> expected_outputs;
    expected_outputs.reserve(kQueriesPerTrial);
    for (size_t q = 0uz; q < kQueriesPerTrial; ++q) {
      const int val = draw_score(regime);
      queries.push_back(val);
      const int expected = oracle.add(val);
      expected_outputs.push_back(expected);
      ASSERT_EQ(wrapper.add(val), expected);
    }
    EXPECT_EQ(wrapper_nums, initial);

    auto stream = leet_code::KthLargestStream::create(k, initial);
    ASSERT_TRUE(stream.has_value());
    EXPECT_EQ(stream->k(), target_k);
    EXPECT_EQ(stream->size(), std::min(initial_len, target_k));

    int prev_kth = kMin;
    for (size_t q = 0uz; q < kQueriesPerTrial; ++q) {
      SCOPED_TRACE(q);
      const int actual = stream->add(queries[q]);
      EXPECT_EQ(actual, expected_outputs[q]);
      EXPECT_GE(actual, prev_kth);
      EXPECT_EQ(stream->size(), target_k);
      prev_kth = actual;
    }
  }
}

// ============================================================================
// TIER 4: MAX-CONSTRAINT STRESS & ONLINE JUDGE SIMULATION
// ============================================================================

TEST(KthLargestElementInAStreamTest,
     MaxConstraintStress10kInitial5kK10kAddsWithinTimeBudget) {
  constexpr size_t kInitialLen = 10'000uz;
  constexpr int kOrder = 5'000;
  constexpr size_t kHalfQueries = 5'000uz;

  // Since gcd(7919, 10000) == 1, `(i * 7919) % 10000 - 4999` is an exact
  // permutation of [-4999, 5000] within official [-10000, 10000] bounds.
  std::vector<int> initial(kInitialLen, 0);
  for (size_t i = 0uz; i < kInitialLen; ++i) {
    initial[i] = static_cast<int>((i * 7'919uz) % kInitialLen) - 4'999;
  }

  const auto stream_start = std::chrono::steady_clock::now();
  auto stream = leet_code::KthLargestStream::create(kOrder, initial);
  ASSERT_TRUE(stream.has_value());
  EXPECT_EQ(stream->size(), static_cast<size_t>(kOrder));
  for (size_t m = 0uz; m < kHalfQueries; ++m) {
    const int offset = static_cast<int>(m);
    ASSERT_EQ(stream->add(-10'000 + offset), 1 + offset);
    ASSERT_EQ(stream->add(5'001 + offset), 2 + offset);
  }
  const auto stream_elapsed = std::chrono::steady_clock::now() - stream_start;
  EXPECT_LT(stream_elapsed, std::chrono::milliseconds(1000));
  EXPECT_EQ(stream->size(), static_cast<size_t>(kOrder));

  std::vector<int> wrapper_nums = initial;
  const auto wrapper_start = std::chrono::steady_clock::now();
  leet_code::KthLargest wrapper(kOrder, wrapper_nums);
  for (size_t m = 0uz; m < kHalfQueries; ++m) {
    const int offset = static_cast<int>(m);
    ASSERT_EQ(wrapper.add(-10'000 + offset), 1 + offset);
    ASSERT_EQ(wrapper.add(5'001 + offset), 2 + offset);
  }
  const auto wrapper_elapsed = std::chrono::steady_clock::now() - wrapper_start;
  EXPECT_LT(wrapper_elapsed, std::chrono::milliseconds(1000));
}

struct OjTestCase {
  int k = 1;
  std::vector<int> initial;
  std::vector<int> adds;
  std::vector<int> expected;
};

TEST(KthLargestElementInAStreamTest, LeetCodeOnlineJudgeSimulationHarness) {
  constexpr int kMin = std::numeric_limits<int>::min();
  constexpr int kMax = std::numeric_limits<int>::max();

  const std::vector<OjTestCase> cases = {
      {.k = 3,
       .initial = {4, 5, 8, 2},
       .adds = {3, 5, 10, 9, 4},
       .expected = {4, 5, 5, 8, 8}},
      {.k = 4,
       .initial = {7, 7, 7, 7, 8, 3},
       .adds = {2, 10, 9, 9},
       .expected = {7, 7, 7, 8}},
      {.k = 1,
       .initial = {},
       .adds = {-1, 0, -2, 5},
       .expected = {-1, 0, 0, 5}},
      {.k = 2,
       .initial = {kMin},
       .adds = {kMax, 0, kMax},
       .expected = {kMin, 0, kMax}},
  };

  for (size_t i = 0uz; i < cases.size(); ++i) {
    SCOPED_TRACE(i);
    VerifyBothInterfaces(cases[i].k, cases[i].initial, cases[i].adds,
                         cases[i].expected);
  }
}

}  // namespace
