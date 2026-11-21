/// @file math_extra_test.cpp
/// @brief Unit and compile-time tests for `common::checked_add` and
/// `common::Integer`.

#include "common/math_extra.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>

namespace {

// ============================================================================
// COMPILE-TIME CONCEPT & CONSTEXPR CHECKS
// ============================================================================

static_assert(common::Integer<int8_t>);
static_assert(common::Integer<uint8_t>);
static_assert(common::Integer<int16_t>);
static_assert(common::Integer<uint16_t>);
static_assert(common::Integer<int32_t>);
static_assert(common::Integer<uint32_t>);
static_assert(common::Integer<int64_t>);
static_assert(common::Integer<uint64_t>);
static_assert(common::Integer<int>);
static_assert(common::Integer<size_t>);
static_assert(common::Integer<ptrdiff_t>);
static_assert(common::Integer<const int32_t>);

static_assert(!common::Integer<bool>);
static_assert(!common::Integer<const bool>);
static_assert(!common::Integer<char>);
static_assert(!common::Integer<wchar_t>);
static_assert(!common::Integer<char8_t>);
static_assert(!common::Integer<char16_t>);
static_assert(!common::Integer<char32_t>);
static_assert(!common::Integer<float>);
static_assert(!common::Integer<double>);
static_assert(!common::Integer<int*>);

static_assert(common::checked_add(10, 20) == 30);
static_assert(common::checked_add(std::numeric_limits<int>::max(), 1) ==
              std::nullopt);
static_assert(common::checked_add(std::numeric_limits<int>::min(), -1) ==
              std::nullopt);
static_assert(common::checked_add(std::numeric_limits<size_t>::max(), 1uz) ==
              std::nullopt);
static_assert(common::checked_add(std::numeric_limits<size_t>::max(), 0uz) ==
              std::numeric_limits<size_t>::max());

static_assert(common::detail::checked_add_portable(10, 20) == 30);
static_assert(common::detail::checked_add_portable(
                  std::numeric_limits<int>::max(), 1) == std::nullopt);
static_assert(common::detail::checked_add_portable(
                  std::numeric_limits<int>::min(), -1) == std::nullopt);
static_assert(common::detail::checked_add_portable(
                  std::numeric_limits<size_t>::max(), 1uz) == std::nullopt);

template <typename T>
void ExpectCheckedAddMatches(T a, T b, std::optional<T> expected) {
  EXPECT_EQ(common::checked_add(a, b), expected);
  EXPECT_EQ(common::detail::checked_add_portable(a, b), expected);
}

// ============================================================================
// TYPED TESTS ACROSS ALL UNSIGNED INTEGER TYPES
// ============================================================================

template <typename T>
class UnsignedCheckedAddTest : public ::testing::Test {};

using UnsignedTypes =
    ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t, size_t>;
TYPED_TEST_SUITE(UnsignedCheckedAddTest, UnsignedTypes);

TYPED_TEST(UnsignedCheckedAddTest, ZeroAndValidBoundaries) {
  using T = TypeParam;
  constexpr T kZero = T{0};
  constexpr T kOne = T{1};
  constexpr T kMax = std::numeric_limits<T>::max();
  constexpr T kMaxMinusOne = static_cast<T>(kMax - kOne);

  ExpectCheckedAddMatches<T>(kZero, kZero, kZero);
  ExpectCheckedAddMatches<T>(kZero, kMax, kMax);
  ExpectCheckedAddMatches<T>(kMax, kZero, kMax);
  ExpectCheckedAddMatches<T>(kMaxMinusOne, kOne, kMax);
  ExpectCheckedAddMatches<T>(kOne, kMaxMinusOne, kMax);
  ExpectCheckedAddMatches<T>(T{42}, T{58}, T{100});
}

TYPED_TEST(UnsignedCheckedAddTest, OverflowReturnsNullopt) {
  using T = TypeParam;
  constexpr T kOne = T{1};
  constexpr T kMax = std::numeric_limits<T>::max();
  constexpr T kHalfPlusOne = static_cast<T>((kMax / T{2}) + kOne);

  ExpectCheckedAddMatches<T>(kMax, kOne, std::nullopt);
  ExpectCheckedAddMatches<T>(kOne, kMax, std::nullopt);
  ExpectCheckedAddMatches<T>(kMax, kMax, std::nullopt);
  ExpectCheckedAddMatches<T>(kHalfPlusOne, kHalfPlusOne, std::nullopt);
}

// ============================================================================
// TYPED TESTS ACROSS ALL SIGNED INTEGER TYPES
// ============================================================================

template <typename T>
class SignedCheckedAddTest : public ::testing::Test {};

using SignedTypes =
    ::testing::Types<int8_t, int16_t, int32_t, int64_t, ptrdiff_t>;
TYPED_TEST_SUITE(SignedCheckedAddTest, SignedTypes);

TYPED_TEST(SignedCheckedAddTest, ZeroAndValidBoundaries) {
  using T = TypeParam;
  constexpr T kZero = T{0};
  constexpr T kOne = T{1};
  constexpr T kNegOne = T{-1};
  constexpr T kMin = std::numeric_limits<T>::min();
  constexpr T kMax = std::numeric_limits<T>::max();
  constexpr T kMaxMinusOne = static_cast<T>(kMax - kOne);
  constexpr T kMinPlusOne = static_cast<T>(kMin + kOne);

  ExpectCheckedAddMatches<T>(kZero, kZero, kZero);
  ExpectCheckedAddMatches<T>(kMax, kZero, kMax);
  ExpectCheckedAddMatches<T>(kZero, kMax, kMax);
  ExpectCheckedAddMatches<T>(kMin, kZero, kMin);
  ExpectCheckedAddMatches<T>(kZero, kMin, kMin);
  ExpectCheckedAddMatches<T>(kMaxMinusOne, kOne, kMax);
  ExpectCheckedAddMatches<T>(kOne, kMaxMinusOne, kMax);
  ExpectCheckedAddMatches<T>(kMinPlusOne, kNegOne, kMin);
  ExpectCheckedAddMatches<T>(kNegOne, kMinPlusOne, kMin);
  ExpectCheckedAddMatches<T>(kMax, kMin, kNegOne);
  ExpectCheckedAddMatches<T>(kMin, kMax, kNegOne);
  ExpectCheckedAddMatches<T>(T{-25}, T{17}, T{-8});
}

TYPED_TEST(SignedCheckedAddTest, PositiveOverflowAndNegativeUnderflow) {
  using T = TypeParam;
  constexpr T kOne = T{1};
  constexpr T kNegOne = T{-1};
  constexpr T kMin = std::numeric_limits<T>::min();
  constexpr T kMax = std::numeric_limits<T>::max();
  constexpr T kHalfMaxPlusOne = static_cast<T>((kMax / T{2}) + kOne);
  constexpr T kHalfMinMinusOne = static_cast<T>((kMin / T{2}) - kOne);

  ExpectCheckedAddMatches<T>(kMax, kOne, std::nullopt);
  ExpectCheckedAddMatches<T>(kOne, kMax, std::nullopt);
  ExpectCheckedAddMatches<T>(kMax, kMax, std::nullopt);
  ExpectCheckedAddMatches<T>(kHalfMaxPlusOne, kHalfMaxPlusOne, std::nullopt);

  ExpectCheckedAddMatches<T>(kMin, kNegOne, std::nullopt);
  ExpectCheckedAddMatches<T>(kNegOne, kMin, std::nullopt);
  ExpectCheckedAddMatches<T>(kMin, kMin, std::nullopt);
  ExpectCheckedAddMatches<T>(kHalfMinMinusOne, kHalfMinMinusOne, std::nullopt);
}

// ============================================================================
// EXHAUSTIVE 8-BIT & DIFFERENTIAL 64-BIT PARITY TESTS
// ============================================================================

TEST(MathExtraTest, ExhaustiveUint8AndInt8Parity) {
  for (int a = 0; a <= 255; ++a) {
    for (int b = 0; b <= 255; ++b) {
      const auto ua = static_cast<uint8_t>(a);
      const auto ub = static_cast<uint8_t>(b);
      const std::optional<uint8_t> expected =
          (a + b <= 255) ? std::optional<uint8_t>{static_cast<uint8_t>(a + b)}
                         : std::nullopt;
      EXPECT_EQ(common::checked_add(ua, ub), expected);
      EXPECT_EQ(common::detail::checked_add_portable(ua, ub), expected);
    }
  }

  for (int a = -128; a <= 127; ++a) {
    for (int b = -128; b <= 127; ++b) {
      const auto sa = static_cast<int8_t>(a);
      const auto sb = static_cast<int8_t>(b);
      const std::optional<int8_t> expected =
          (a + b >= -128 && a + b <= 127)
              ? std::optional<int8_t>{static_cast<int8_t>(a + b)}
              : std::nullopt;
      EXPECT_EQ(common::checked_add(sa, sb), expected);
      EXPECT_EQ(common::detail::checked_add_portable(sa, sb), expected);
    }
  }
}

TEST(MathExtraTest, RandomInt64AndUint64BuiltinMatchesPortable) {
  constexpr auto kSeed = uint64_t{0xC001CAFEBABA2026};
  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<int64_t> signed_dist(
      std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max());
  std::uniform_int_distribution<uint64_t> unsigned_dist(
      uint64_t{0}, std::numeric_limits<uint64_t>::max());

  for (size_t i = 0uz; i < 10'000uz; ++i) {
    const int64_t sa = signed_dist(rng);
    const int64_t sb = signed_dist(rng);
    EXPECT_EQ(common::checked_add(sa, sb),
              common::detail::checked_add_portable(sa, sb));

    const uint64_t ua = unsigned_dist(rng);
    const uint64_t ub = unsigned_dist(rng);
    EXPECT_EQ(common::checked_add(ua, ub),
              common::detail::checked_add_portable(ua, ub));
  }
}

}  // namespace
