/// @file bitmap_flip_test.cpp
/// @brief GoogleTest suite for `systems::flip_bitmap` and friends
/// (spec: docs/systems/bitmap_flip.md, prefix BMFLP).
///
/// Suites follow the stages of the exercise, so each can be turned green on
/// its own (`ctest --preset dev-debug -R '^BitmapFlipTest\.'`, ...):
///
///   - `BitmapFlipTest`: `reverse_bits()` and Problem 0 (width % 8 == 0):
///     known asymmetric patterns, even / odd bytes per row, single byte /
///     single row, large images, the double-flip involution, no heap.
///   - `BitmapFlipUnalignedTest`: Follow-up 2 (width % 8 != 0), including
///     widths below one byte and garbage in the padding bits.
///   - `BitmapFlipStrideTest`, `BitmapFlipRoiTest`: Follow-up 3.
///   - `BitmapFlipErrorTest`: every error code, overflow-safe size checks,
///     validation order, buffer untouched on error, the C-style wrapper.
///   - `BitmapFlipConcurrencyTest`: reentrancy (disjoint rows / buffers from
///     several threads; run under TSan).
///
/// The oracle never re-implements the flip: it checks the definition pixel by
/// pixel (`after(x, y) == before(width - 1 - x, y)`) against a saved copy.

#include "systems/bitmap_flip.hpp"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ios>
#include <latch>
#include <limits>
#include <random>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include "systems_test/allocation_counter.hpp"

namespace {

using systems::BitmapFlipError;
using systems::BitmapRoi;
using systems::flip_bitmap;
using systems::flip_bitmap_roi;
using systems::flip_bitmap_stride;
using systems::reverse_bits;
using systems::to_string;

using Bytes = std::vector<std::uint8_t>;
using FlipResult = std::expected<void, BitmapFlipError>;

constexpr std::size_t kBits = systems::kBitsPerByte;
constexpr std::size_t kMaxSize = std::numeric_limits<std::size_t>::max();

// ---------------------------------------------------------------------------
// Compile-time contract (holds for the stubs too)
// ---------------------------------------------------------------------------

static_assert(systems::kBitsPerByte == 8);
static_assert(
    std::is_same_v<std::underlying_type_t<BitmapFlipError>, std::uint8_t>);
static_assert(std::is_aggregate_v<BitmapRoi>);
static_assert(std::is_trivially_copyable_v<BitmapRoi>);
static_assert(noexcept(to_string(BitmapFlipError::kNotImplemented)));
static_assert(noexcept(reverse_bits(std::uint8_t{0})));
static_assert(noexcept(flip_bitmap(std::span<std::uint8_t>{}, 0, 0)));
static_assert(noexcept(flip_bitmap_stride(std::span<std::uint8_t>{}, 0, 0, 0)));
static_assert(noexcept(flip_bitmap_roi(std::span<std::uint8_t>{}, 0, 0, 0,
                                       BitmapRoi{})));
static_assert(noexcept(flip_bitmap(static_cast<std::uint8_t*>(nullptr), 0, 0)));
static_assert(
    std::is_same_v<decltype(flip_bitmap(std::span<std::uint8_t>{}, 0, 0)),
                   FlipResult>);
static_assert(std::is_same_v<
              decltype(flip_bitmap(static_cast<std::uint8_t*>(nullptr), 0, 0)),
              void>);

// ---------------------------------------------------------------------------
// Test helpers (oracle = the definition of a horizontal flip)
// ---------------------------------------------------------------------------

/// Pixel bytes per row: ceil(width / 8).
[[nodiscard]] constexpr auto bytes_per_row(std::size_t width) -> std::size_t {
  return (width / kBits) + ((width % kBits) != 0 ? 1U : 0U);
}

[[nodiscard]] auto bit_of(std::uint8_t byte, std::size_t bit) -> bool {
  return ((static_cast<unsigned>(byte) >> bit) & 1U) != 0U;
}

/// Pixel (x, y): bit x % 8 of byte y * stride + x / 8 (bit 0 = leftmost).
[[nodiscard]] auto pixel(std::span<const std::uint8_t> buffer,
                         std::size_t stride, std::size_t x, std::size_t y)
    -> bool {
  return bit_of(buffer[(y * stride) + (x / kBits)], x % kBits);
}

[[nodiscard]] auto random_bytes(std::size_t count, std::uint32_t seed)
    -> Bytes {
  std::mt19937 rng{seed};
  std::uniform_int_distribution<unsigned> dist{0U, 255U};
  Bytes out(count);
  for (auto& byte : out) {
    byte = static_cast<std::uint8_t>(dist(rng));
  }
  return out;
}

/// What a flip must write into the padding bits of the rows it covers.
enum class Padding : std::uint8_t {
  /// Full-row flips: padding bits of the last pixel byte become zero
  /// (BMFLP-REQ-009, spec assumption A-3).
  kZeroed,
  /// ROI flips: every bit outside the ROI is preserved (BMFLP-REQ-011).
  kPreserved,
};

/// Checks every bit of `after` against `before`:
///   - pixels inside `region` are mirrored within the region;
///   - with `Padding::kZeroed`, padding bits (columns >= `image_width` inside
///     the row's pixel bytes) of the region's rows are zero;
///   - every other bit (stride padding bytes, other rows, bytes past the
///     image, pixels outside the region) is unchanged.
[[nodiscard]] auto verify_flip(std::span<const std::uint8_t> before,
                               std::span<const std::uint8_t> after,
                               std::size_t image_width, std::size_t stride,
                               const BitmapRoi& region, Padding padding)
    -> ::testing::AssertionResult {
  if (before.size() != after.size()) {
    return ::testing::AssertionFailure() << "buffer size changed";
  }
  const std::size_t row_bytes = bytes_per_row(image_width);
  for (std::size_t index = 0; index < after.size(); ++index) {
    const std::size_t y = index / stride;
    const std::size_t column_byte = index % stride;
    const bool region_row = y >= region.y && (y - region.y) < region.height;
    for (std::size_t bit = 0; bit < kBits; ++bit) {
      const std::size_t x = (column_byte * kBits) + bit;
      const bool in_region =
          region_row && x >= region.x && (x - region.x) < region.width;
      const bool is_padding = region_row && column_byte < row_bytes &&
                              x >= image_width && padding == Padding::kZeroed;
      bool want = bit_of(before[index], bit);
      std::string_view kind = "must be unchanged";
      if (in_region) {
        want = pixel(before, stride, (2 * region.x) + region.width - 1 - x, y);
        kind = "mirrored pixel";
      } else if (is_padding) {
        want = false;
        kind = "padding bit, must be 0";
      }
      if (bit_of(after[index], bit) != want) {
        ::testing::Message message;
        message << "byte " << index << " (row " << y << ", column byte "
                << column_byte << ", bit " << bit << " = pixel column " << x
                << ", " << kind << "): got " << bit_of(after[index], bit)
                << ", want " << want << "; byte before 0x" << std::hex
                << static_cast<unsigned>(before[index]) << ", after 0x"
                << static_cast<unsigned>(after[index]);
        return ::testing::AssertionFailure() << message;
      }
    }
  }
  return ::testing::AssertionSuccess();
}

/// Full-image flip check (`flip_bitmap`, `flip_bitmap_stride`).
[[nodiscard]] auto verify_full_flip(std::span<const std::uint8_t> before,
                                    std::span<const std::uint8_t> after,
                                    std::size_t width, std::size_t height,
                                    std::size_t stride)
    -> ::testing::AssertionResult {
  return verify_flip(
      before, after, width, stride,
      BitmapRoi{.x = 0, .y = 0, .width = width, .height = height},
      Padding::kZeroed);
}

/// True when both images hold the same pixels (padding bits ignored).
[[nodiscard]] auto same_pixels(std::span<const std::uint8_t> lhs,
                               std::span<const std::uint8_t> rhs,
                               std::size_t width, std::size_t height,
                               std::size_t stride)
    -> ::testing::AssertionResult {
  for (std::size_t y = 0; y < height; ++y) {
    for (std::size_t x = 0; x < width; ++x) {
      if (pixel(lhs, stride, x, y) != pixel(rhs, stride, x, y)) {
        return ::testing::AssertionFailure()
               << "pixel (" << x << ", " << y << ") differs";
      }
    }
  }
  return ::testing::AssertionSuccess();
}

/// Makes row 0 non-palindromic (pixel 0 set, last pixel clear) so that a
/// correct flip must change the image. Requires width >= 2.
void break_symmetry(std::span<std::uint8_t> image, std::size_t width) {
  const std::size_t last = width - 1;
  image[0] = static_cast<std::uint8_t>(image[0] | 0x01U);
  image[last / kBits] =
      static_cast<std::uint8_t>(image[last / kBits] & ~(1U << (last % kBits)));
}

// ===========================================================================
// BitmapFlipTest: reverse_bits() and Problem 0 (width % 8 == 0)
// ===========================================================================

TEST(BitmapFlipTest, ReverseBitsKnownAsymmetricValues) {
  RecordProperty("requirements", "BMFLP-REQ-002");
  struct Case {
    std::uint8_t in;
    std::uint8_t out;
  };
  constexpr std::array kCases{
      Case{.in = 0x01, .out = 0x80}, Case{.in = 0x80, .out = 0x01},
      Case{.in = 0x0F, .out = 0xF0}, Case{.in = 0x33, .out = 0xCC},
      Case{.in = 0x55, .out = 0xAA}, Case{.in = 0xB0, .out = 0x0D},
      Case{.in = 0x12, .out = 0x48}, Case{.in = 0xE0, .out = 0x07},
      Case{.in = 0x00, .out = 0x00}, Case{.in = 0xFF, .out = 0xFF},
  };
  for (const auto& test_case : kCases) {
    EXPECT_EQ(reverse_bits(test_case.in), test_case.out)
        << "reverse_bits(0x" << std::hex << static_cast<unsigned>(test_case.in)
        << ")";
  }
}

TEST(BitmapFlipTest, ReverseBitsExhaustiveMatchesDefinition) {
  RecordProperty("requirements", "BMFLP-REQ-002");
  int mismatches = 0;
  int first_bad = -1;
  for (unsigned value = 0; value <= 0xFFU; ++value) {
    const auto in = static_cast<std::uint8_t>(value);
    const std::uint8_t out = reverse_bits(in);
    bool ok = reverse_bits(out) == in;
    for (std::size_t bit = 0; bit < kBits; ++bit) {
      ok = ok && bit_of(out, bit) == bit_of(in, kBits - 1 - bit);
    }
    if (!ok) {
      ++mismatches;
      first_bad = first_bad < 0 ? static_cast<int>(value) : first_bad;
    }
  }
  EXPECT_EQ(mismatches, 0) << "first wrong input: " << first_bad;
}

TEST(BitmapFlipTest, SingleByteSingleRowReversesBitOrder) {
  RecordProperty("requirements", "BMFLP-REQ-001,BMFLP-REQ-003");
  // Bit 0 is the leftmost pixel; flipping 8 pixels maps bit i to bit 7 - i.
  std::array<std::uint8_t, 1> image{0x01};
  const FlipResult first = flip_bitmap(image, 8, 1);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  EXPECT_EQ(image[0], 0x80);

  image[0] = 0xB0;  // bits 4, 5, 7 -> bits 3, 2, 0
  const FlipResult second = flip_bitmap(image, 8, 1);
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(image[0], 0x0D);
}

TEST(BitmapFlipTest, EvenBytesPerRowSwapsAndReversesBytes) {
  RecordProperty("requirements", "BMFLP-REQ-003");
  // [0x0F, 0x33] -> [rev(0x33), rev(0x0F)] = [0xCC, 0xF0].
  std::array<std::uint8_t, 2> row{0x0F, 0x33};
  const FlipResult result = flip_bitmap(row, 16, 1);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(row[0], 0xCC);
  EXPECT_EQ(row[1], 0xF0);
}

TEST(BitmapFlipTest, OddBytesPerRowAlsoReversesMiddleByte) {
  RecordProperty("requirements", "BMFLP-REQ-003");
  // [0x03, 0x1E, 0x40] -> [rev(0x40), rev(0x1E), rev(0x03)]
  //                     = [0x02, 0x78, 0xC0].
  std::array<std::uint8_t, 3> row{0x03, 0x1E, 0x40};
  const FlipResult result = flip_bitmap(row, 24, 1);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(row[0], 0x02);
  EXPECT_EQ(row[1], 0x78) << "middle byte must be bit-reversed in place";
  EXPECT_EQ(row[2], 0xC0);
}

TEST(BitmapFlipTest, AsymmetricPatternCatchesByteSwapOnlyAndBitReverseOnly) {
  RecordProperty("requirements", "BMFLP-REQ-001,BMFLP-REQ-003");
  // Correct: [0x00, 0x80]. Byte swap only: [0x00, 0x01]. Bit reverse only:
  // [0x80, 0x00]. No-op: [0x01, 0x00].
  std::array<std::uint8_t, 2> two{0x01, 0x00};
  const FlipResult result_two = flip_bitmap(two, 16, 1);
  ASSERT_TRUE(result_two.has_value()) << to_string(result_two.error());
  EXPECT_NE(two[0], 0x80) << "bits reversed but bytes not swapped";
  EXPECT_NE(two[1], 0x01) << "bytes swapped but bits not reversed";
  EXPECT_EQ(two[0], 0x00);
  EXPECT_EQ(two[1], 0x80);

  // Correct: [0x10, 0x20, 0x40, 0x80]. Byte swap only: [0x08, 0x04, 0x02,
  // 0x01]. Bit reverse only: [0x80, 0x40, 0x20, 0x10].
  std::array<std::uint8_t, 4> four{0x01, 0x02, 0x04, 0x08};
  const FlipResult result_four = flip_bitmap(four, 32, 1);
  ASSERT_TRUE(result_four.has_value()) << to_string(result_four.error());
  EXPECT_EQ(four, (std::array<std::uint8_t, 4>{0x10, 0x20, 0x40, 0x80}));
}

TEST(BitmapFlipTest, EveryRowFlippedAndRowOrderKept) {
  RecordProperty("requirements", "BMFLP-REQ-004");
  // Three distinct, non-palindromic rows of 2 bytes.
  std::array<std::uint8_t, 6> image{
      0x01, 0x00,  // row 0 -> [0x00, 0x80]
      0x00, 0x03,  // row 1 -> [0xC0, 0x00]
      0x0F, 0x01,  // row 2 -> [0x80, 0xF0]
  };
  const FlipResult result = flip_bitmap(image, 16, 3);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(image,
            (std::array<std::uint8_t, 6>{0x00, 0x80, 0xC0, 0x00, 0x80, 0xF0}));
}

TEST(BitmapFlipTest, AlignedWidthsWithEvenAndOddBytesPerRow) {
  RecordProperty("requirements", "BMFLP-REQ-001,BMFLP-REQ-003,BMFLP-REQ-004");
  constexpr std::size_t kHeight = 3;
  for (std::size_t width = 8; width <= 136; width += 8) {  // 1..17 bytes
    SCOPED_TRACE(::testing::Message() << "width " << width);
    const std::size_t stride = bytes_per_row(width);
    const Bytes before =
        random_bytes(stride * kHeight, static_cast<std::uint32_t>(width));
    Bytes image = before;
    const FlipResult result = flip_bitmap(image, width, kHeight);
    ASSERT_TRUE(result.has_value()) << to_string(result.error());
    EXPECT_TRUE(verify_full_flip(before, image, width, kHeight, stride));
  }
}

TEST(BitmapFlipTest, SingleRowWideAndSingleByteColumnTall) {
  RecordProperty("requirements", "BMFLP-REQ-003,BMFLP-REQ-004");
  const Bytes wide_before = random_bytes(64, 11);  // 512 x 1
  Bytes wide = wide_before;
  const FlipResult wide_result = flip_bitmap(wide, 512, 1);
  ASSERT_TRUE(wide_result.has_value()) << to_string(wide_result.error());
  EXPECT_TRUE(verify_full_flip(wide_before, wide, 512, 1, 64));

  const Bytes tall_before = random_bytes(200, 12);  // 8 x 200
  Bytes tall = tall_before;
  const FlipResult tall_result = flip_bitmap(tall, 8, 200);
  ASSERT_TRUE(tall_result.has_value()) << to_string(tall_result.error());
  EXPECT_TRUE(verify_full_flip(tall_before, tall, 8, 200, 1));
}

TEST(BitmapFlipTest, DoubleFlipRestoresRandomImage) {
  RecordProperty("requirements", "BMFLP-REQ-005");
  struct Shape {
    std::size_t width;
    std::size_t height;
  };
  // Even (6) and odd (5) bytes per row.
  constexpr std::array kShapes{Shape{.width = 48, .height = 16},
                               Shape{.width = 40, .height = 9}};
  for (std::uint32_t seed = 1; seed <= 4; ++seed) {
    for (const auto& shape : kShapes) {
      SCOPED_TRACE(::testing::Message()
                   << "seed " << seed << ", width " << shape.width);
      Bytes original =
          random_bytes(bytes_per_row(shape.width) * shape.height, seed);
      break_symmetry(original, shape.width);
      Bytes image = original;

      const FlipResult first = flip_bitmap(image, shape.width, shape.height);
      ASSERT_TRUE(first.has_value()) << to_string(first.error());
      EXPECT_NE(image, original) << "flip must not be a no-op";

      const FlipResult second = flip_bitmap(image, shape.width, shape.height);
      ASSERT_TRUE(second.has_value()) << to_string(second.error());
      EXPECT_EQ(image, original) << "flip(flip(image)) != image";
    }
  }
}

TEST(BitmapFlipTest, LargeImage1920x1080) {
  RecordProperty("requirements", "BMFLP-REQ-003,BMFLP-REQ-004,BMFLP-REQ-005");
  constexpr std::size_t kWidth = 1920;
  constexpr std::size_t kHeight = 1080;
  constexpr std::size_t kStride = kWidth / kBits;
  const Bytes before = random_bytes(kStride * kHeight, 1920);
  Bytes image = before;

  const FlipResult first = flip_bitmap(image, kWidth, kHeight);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  EXPECT_TRUE(verify_full_flip(before, image, kWidth, kHeight, kStride));

  const FlipResult second = flip_bitmap(image, kWidth, kHeight);
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(image, before);
}

TEST(BitmapFlipTest, CStyleWrapperFlipsPackedImage) {
  RecordProperty("requirements", "BMFLP-REQ-012");
  std::array<std::uint8_t, 4> image{0x01, 0x00, 0x00, 0x03};  // 16 x 2
  flip_bitmap(image.data(), 16, 2);
  EXPECT_EQ(image, (std::array<std::uint8_t, 4>{0x00, 0x80, 0xC0, 0x00}));
}

TEST(BitmapFlipTest, NoHeapAllocationInAlignedFlip) {
  RecordProperty("requirements", "BMFLP-REQ-015");
  // Problem 0 stage only: aligned width, packed rows. The unaligned, stride,
  // and ROI variants are covered by BitmapFlipRoiTest.NoHeapAllocation*.
  constexpr std::size_t kWidth = 64;
  constexpr std::size_t kHeight = 9;
  Bytes packed = random_bytes(bytes_per_row(kWidth) * kHeight, 21);
  const Bytes before = packed;
  FlipResult result{};
  std::uint8_t reversed = 0;
  std::size_t allocations = 0;
  {
    const systems_test::AllocationScope scope;
    reversed = reverse_bits(0x01);
    result = flip_bitmap(packed, kWidth, kHeight);
    flip_bitmap(packed.data(), kWidth, kHeight);  // flips back
    allocations = scope.allocations();
  }
  EXPECT_EQ(allocations, 0U);
  EXPECT_EQ(reversed, 0x80);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  // Pair the heap check with observable work so the test is red on a stub.
  Bytes once = before;
  ASSERT_TRUE(flip_bitmap(once, kWidth, kHeight).has_value());
  EXPECT_TRUE(
      verify_full_flip(before, once, kWidth, kHeight, bytes_per_row(kWidth)));
  EXPECT_EQ(packed, before) << "C-style wrapper did not flip back";
}

// ===========================================================================
// BitmapFlipUnalignedTest: Follow-up 2 (width % 8 != 0)
// ===========================================================================

TEST(BitmapFlipUnalignedTest, WidthsBelowOneByte) {
  RecordProperty("requirements", "BMFLP-REQ-008");
  struct Case {
    std::size_t width;
    std::uint8_t in;
    std::uint8_t out;
  };
  constexpr std::array kCases{
      Case{.width = 1, .in = 0x01, .out = 0x01},  // one pixel: identity
      Case{.width = 3, .in = 0x03, .out = 0x06},  // pixels {0,1} -> {2,1}
      Case{.width = 5, .in = 0x05, .out = 0x14},  // pixels {0,2} -> {4,2}
      Case{.width = 7, .in = 0x01, .out = 0x40},  // pixel 0 -> 6
      Case{.width = 7, .in = 0x06, .out = 0x30},  // pixels {1,2} -> {5,4}
  };
  for (const auto& test_case : kCases) {
    SCOPED_TRACE(::testing::Message() << "width " << test_case.width);
    std::array<std::uint8_t, 1> image{test_case.in};
    const FlipResult result = flip_bitmap(image, test_case.width, 1);
    ASSERT_TRUE(result.has_value()) << to_string(result.error());
    EXPECT_EQ(image[0], test_case.out)
        << "got 0x" << std::hex << static_cast<unsigned>(image[0]);
  }
}

TEST(BitmapFlipUnalignedTest, MultiByteUnalignedWidths) {
  RecordProperty("requirements", "BMFLP-REQ-008");
  // Width 10: pixels {0, 8, 9} -> {9, 1, 0}.
  std::array<std::uint8_t, 2> ten{0x01, 0x03};
  const FlipResult result_ten = flip_bitmap(ten, 10, 1);
  ASSERT_TRUE(result_ten.has_value()) << to_string(result_ten.error());
  EXPECT_EQ(ten, (std::array<std::uint8_t, 2>{0x03, 0x02}));

  // Width 12: pixels {0, 1} -> {11, 10}.
  std::array<std::uint8_t, 2> twelve{0x03, 0x00};
  const FlipResult result_twelve = flip_bitmap(twelve, 12, 1);
  ASSERT_TRUE(result_twelve.has_value()) << to_string(result_twelve.error());
  EXPECT_EQ(twelve, (std::array<std::uint8_t, 2>{0x00, 0x0C}));

  // Width 17 (one pixel in the last byte): pixel 0 -> 16 and back. Mirroring
  // whole bytes without re-aligning would land on pixel 23 instead.
  std::array<std::uint8_t, 3> seventeen{0x01, 0x00, 0x00};
  const FlipResult result_17a = flip_bitmap(seventeen, 17, 1);
  ASSERT_TRUE(result_17a.has_value()) << to_string(result_17a.error());
  EXPECT_EQ(seventeen, (std::array<std::uint8_t, 3>{0x00, 0x00, 0x01}));
  const FlipResult result_17b = flip_bitmap(seventeen, 17, 1);
  ASSERT_TRUE(result_17b.has_value()) << to_string(result_17b.error());
  EXPECT_EQ(seventeen, (std::array<std::uint8_t, 3>{0x01, 0x00, 0x00}));
}

TEST(BitmapFlipUnalignedTest, PaddingGarbageIgnoredAndWrittenAsZero) {
  RecordProperty("requirements", "BMFLP-REQ-009");
  // Width 5, padding bits 5..7 hold garbage 0b111: same result as 0x05.
  std::array<std::uint8_t, 1> five{0xE5};
  const FlipResult result_five = flip_bitmap(five, 5, 1);
  ASSERT_TRUE(result_five.has_value()) << to_string(result_five.error());
  EXPECT_EQ(five[0], 0x14);

  // Width 1, pixel 0 clear, all 7 padding bits set.
  std::array<std::uint8_t, 1> one{0xFE};
  const FlipResult result_one = flip_bitmap(one, 1, 1);
  ASSERT_TRUE(result_one.has_value()) << to_string(result_one.error());
  EXPECT_EQ(one[0], 0x00);

  // Width 10 with garbage in bits 2..7 of byte 1: same result as {0x01,0x03}.
  std::array<std::uint8_t, 2> ten{0x01, 0xFF};
  const FlipResult result_ten = flip_bitmap(ten, 10, 1);
  ASSERT_TRUE(result_ten.has_value()) << to_string(result_ten.error());
  EXPECT_EQ(ten, (std::array<std::uint8_t, 2>{0x03, 0x02}));

  // Width 15: only the single padding bit (pixel column 15) is set.
  std::array<std::uint8_t, 4> fifteen{0x00, 0x80, 0x00, 0x80};  // 2 rows
  const FlipResult result_15 = flip_bitmap(fifteen, 15, 2);
  ASSERT_TRUE(result_15.has_value()) << to_string(result_15.error());
  EXPECT_EQ(fifteen, (std::array<std::uint8_t, 4>{0x00, 0x00, 0x00, 0x00}));
}

TEST(BitmapFlipUnalignedTest, AllWidthsUpTo72WithGarbagePadding) {
  RecordProperty("requirements", "BMFLP-REQ-008,BMFLP-REQ-009");
  for (std::size_t width = 1; width <= 72; ++width) {
    for (const std::size_t height : {std::size_t{1}, std::size_t{4}}) {
      SCOPED_TRACE(::testing::Message()
                   << "width " << width << ", height " << height);
      const std::size_t stride = bytes_per_row(width);
      // Random bytes: padding bits are garbage in about half the rows.
      const Bytes before = random_bytes(
          stride * height, static_cast<std::uint32_t>((width * 10) + height));
      Bytes image = before;
      const FlipResult result = flip_bitmap(image, width, height);
      ASSERT_TRUE(result.has_value()) << to_string(result.error());
      EXPECT_TRUE(verify_full_flip(before, image, width, height, stride));
    }
  }
}

TEST(BitmapFlipUnalignedTest, DoubleFlipRestoresPixels) {
  RecordProperty("requirements", "BMFLP-REQ-005,BMFLP-REQ-008");
  constexpr std::size_t kHeight = 5;
  for (const std::size_t width : {2U, 7U, 9U, 13U, 31U, 33U, 63U, 65U, 100U}) {
    SCOPED_TRACE(::testing::Message() << "width " << width);
    const std::size_t stride = bytes_per_row(width);
    Bytes original =
        random_bytes(stride * kHeight, static_cast<std::uint32_t>(width));
    break_symmetry(original, width);
    Bytes image = original;

    const FlipResult first = flip_bitmap(image, width, kHeight);
    ASSERT_TRUE(first.has_value()) << to_string(first.error());
    EXPECT_FALSE(same_pixels(original, image, width, kHeight, stride))
        << "flip must not be a no-op";

    const FlipResult second = flip_bitmap(image, width, kHeight);
    ASSERT_TRUE(second.has_value()) << to_string(second.error());
    EXPECT_TRUE(same_pixels(original, image, width, kHeight, stride));
  }
}

TEST(BitmapFlipUnalignedTest, LargeImage1366x768) {
  RecordProperty("requirements", "BMFLP-REQ-008,BMFLP-REQ-009");
  constexpr std::size_t kWidth = 1366;  // 170 bytes + 6 pixels
  constexpr std::size_t kHeight = 768;
  constexpr std::size_t kStride = bytes_per_row(kWidth);
  const Bytes before = random_bytes(kStride * kHeight, 1366);
  Bytes image = before;
  const FlipResult result = flip_bitmap(image, kWidth, kHeight);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(verify_full_flip(before, image, kWidth, kHeight, kStride));
}

TEST(BitmapFlipUnalignedTest, CStyleWrapperHandlesUnalignedWidth) {
  RecordProperty("requirements", "BMFLP-REQ-012");
  std::array<std::uint8_t, 2> image{0x01, 0xFF};  // width 10, garbage padding
  flip_bitmap(image.data(), 10, 1);
  EXPECT_EQ(image, (std::array<std::uint8_t, 2>{0x03, 0x02}));
}

// ===========================================================================
// BitmapFlipStrideTest: Follow-up 3, row stride (pitch)
// ===========================================================================

TEST(BitmapFlipStrideTest, StridePaddingBytesNeverWritten) {
  RecordProperty("requirements", "BMFLP-REQ-010");
  constexpr std::uint8_t kCanary = 0xA5;
  // 16 x 2, stride 4: two pixel bytes and two padding bytes per row.
  std::array<std::uint8_t, 8> buffer{
      0x0F, 0x33, kCanary, kCanary,  // row 0 -> [0xCC, 0xF0]
      0x02, 0x00, kCanary, kCanary,  // row 1 -> [0x00, 0x40]
  };
  const FlipResult result = flip_bitmap_stride(buffer, 16, 2, 4);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(buffer,
            (std::array<std::uint8_t, 8>{0xCC, 0xF0, kCanary, kCanary, 0x00,
                                         0x40, kCanary, kCanary}));
}

TEST(BitmapFlipStrideTest, StrideEqualToRowBytesMatchesPackedFlip) {
  RecordProperty("requirements", "BMFLP-REQ-010");
  for (const std::size_t width : {40U, 45U}) {
    SCOPED_TRACE(::testing::Message() << "width " << width);
    const std::size_t stride = bytes_per_row(width);
    Bytes packed =
        random_bytes(stride * 7, static_cast<std::uint32_t>(width) + 100U);
    Bytes strided = packed;
    const FlipResult packed_result = flip_bitmap(packed, width, 7);
    const FlipResult strided_result =
        flip_bitmap_stride(strided, width, 7, stride);
    ASSERT_TRUE(packed_result.has_value()) << to_string(packed_result.error());
    ASSERT_TRUE(strided_result.has_value())
        << to_string(strided_result.error());
    EXPECT_EQ(strided, packed);
  }
}

TEST(BitmapFlipStrideTest, RandomGeometriesWithUnalignedWidths) {
  RecordProperty("requirements", "BMFLP-REQ-008,BMFLP-REQ-009,BMFLP-REQ-010");
  std::uint32_t seed = 300;
  for (const std::size_t width : {1U, 8U, 13U, 64U, 100U}) {
    for (const std::size_t extra : {0U, 1U, 3U, 8U}) {
      for (const std::size_t height : {1U, 4U}) {
        SCOPED_TRACE(::testing::Message() << "width " << width << ", stride +"
                                          << extra << ", height " << height);
        const std::size_t stride = bytes_per_row(width) + extra;
        const Bytes before = random_bytes(stride * height, ++seed);
        Bytes image = before;
        const FlipResult result =
            flip_bitmap_stride(image, width, height, stride);
        ASSERT_TRUE(result.has_value()) << to_string(result.error());
        EXPECT_TRUE(verify_full_flip(before, image, width, height, stride));
      }
    }
  }
}

TEST(BitmapFlipStrideTest, BytesPastLastRowUntouched) {
  RecordProperty("requirements", "BMFLP-REQ-010");
  constexpr std::size_t kWidth = 21;
  constexpr std::size_t kHeight = 3;
  constexpr std::size_t kStride = 5;
  // Buffer is 7 bytes longer than stride * height; the tail is not image.
  const Bytes before = random_bytes((kStride * kHeight) + 7, 77);
  Bytes image = before;
  const FlipResult result = flip_bitmap_stride(image, kWidth, kHeight, kStride);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(verify_full_flip(before, image, kWidth, kHeight, kStride));
}

TEST(BitmapFlipStrideTest, DoubleFlipRestoresStridedImage) {
  RecordProperty("requirements", "BMFLP-REQ-005,BMFLP-REQ-010");
  constexpr std::size_t kWidth = 32;
  constexpr std::size_t kHeight = 6;
  constexpr std::size_t kStride = 6;
  Bytes original = random_bytes(kStride * kHeight, 32);
  break_symmetry(original, kWidth);
  Bytes image = original;

  const FlipResult first = flip_bitmap_stride(image, kWidth, kHeight, kStride);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  EXPECT_NE(image, original);
  const FlipResult second = flip_bitmap_stride(image, kWidth, kHeight, kStride);
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(image, original);
}

// ===========================================================================
// BitmapFlipRoiTest: Follow-up 3, region of interest
// ===========================================================================

TEST(BitmapFlipRoiTest, ByteAlignedRoiFlipsOnlyItsBytes) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  // 32 x 4 framebuffer, stride 4, random background; ROI = row 1, bytes 1-2.
  Bytes framebuffer = random_bytes(16, 41);
  framebuffer[5] = 0x0F;
  framebuffer[6] = 0x33;
  const Bytes before = framebuffer;
  const BitmapRoi roi{.x = 8, .y = 1, .width = 16, .height = 1};
  const FlipResult result = flip_bitmap_roi(framebuffer, 32, 4, 4, roi);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(framebuffer[5], 0xCC);
  EXPECT_EQ(framebuffer[6], 0xF0);
  EXPECT_TRUE(
      verify_flip(before, framebuffer, 32, 4, roi, Padding::kPreserved));
}

TEST(BitmapFlipRoiTest, RoiInsideOneByteKeepsNeighbourPixels) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  // Pixels {0, 1, 2, 7} set; ROI columns [2, 6): pixel 2 -> 5, pixels 0, 1, 7
  // are outside and stay: 0b1000'0111 -> 0b1010'0011.
  std::array<std::uint8_t, 1> image{0x87};
  const FlipResult result = flip_bitmap_roi(
      image, 8, 1, 1, BitmapRoi{.x = 2, .y = 0, .width = 4, .height = 1});
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(image[0], 0xA3);
}

TEST(BitmapFlipRoiTest, RoiEdgesInsideBytes) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  // 32 x 1. ROI columns [3, 21). Pixel 3 (inside) -> pixel 20. Pixels 0-2 and
  // 21-31 are set and outside the ROI, so they must stay set.
  std::array<std::uint8_t, 4> row{0x0F, 0x00, 0xE0, 0xFF};
  const FlipResult result = flip_bitmap_roi(
      row, 32, 1, 4, BitmapRoi{.x = 3, .y = 0, .width = 18, .height = 1});
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(row, (std::array<std::uint8_t, 4>{0x07, 0x00, 0xF0, 0xFF}));
}

TEST(BitmapFlipRoiTest, RandomRoisMatchDefinition) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  constexpr std::size_t kWidth = 75;  // 9 bytes + 3 pixels
  constexpr std::size_t kHeight = 9;
  constexpr std::size_t kStride = 12;
  constexpr std::array kRois{
      BitmapRoi{.x = 0, .y = 0, .width = kWidth, .height = kHeight},
      BitmapRoi{.x = 0, .y = 2, .width = 1, .height = 3},
      BitmapRoi{.x = 74, .y = 8, .width = 1, .height = 1},
      BitmapRoi{.x = 1, .y = 0, .width = 2, .height = 9},
      BitmapRoi{.x = 3, .y = 1, .width = 5, .height = 4},
      BitmapRoi{.x = 5, .y = 3, .width = 11, .height = 2},
      BitmapRoi{.x = 7, .y = 0, .width = 61, .height = 9},
      BitmapRoi{.x = 8, .y = 4, .width = 64, .height = 5},
      BitmapRoi{.x = 13, .y = 6, .width = 62, .height = 3},
      BitmapRoi{.x = 30, .y = 0, .width = 17, .height = 1},
  };
  std::uint32_t seed = 500;
  for (const auto& roi : kRois) {
    SCOPED_TRACE(::testing::Message()
                 << "roi x=" << roi.x << " y=" << roi.y << " w=" << roi.width
                 << " h=" << roi.height);
    const Bytes before = random_bytes(kStride * kHeight, ++seed);
    Bytes image = before;
    const FlipResult result =
        flip_bitmap_roi(image, kWidth, kHeight, kStride, roi);
    ASSERT_TRUE(result.has_value()) << to_string(result.error());
    EXPECT_TRUE(
        verify_flip(before, image, kWidth, kStride, roi, Padding::kPreserved));
  }
}

TEST(BitmapFlipRoiTest, FullFrameRoiOnAlignedImageMatchesStrideFlip) {
  RecordProperty("requirements", "BMFLP-REQ-010,BMFLP-REQ-011");
  constexpr std::size_t kWidth = 64;
  constexpr std::size_t kHeight = 5;
  constexpr std::size_t kStride = 10;
  Bytes via_roi = random_bytes(kStride * kHeight, 64);
  Bytes via_stride = via_roi;
  const FlipResult roi_result = flip_bitmap_roi(
      via_roi, kWidth, kHeight, kStride,
      BitmapRoi{.x = 0, .y = 0, .width = kWidth, .height = kHeight});
  const FlipResult stride_result =
      flip_bitmap_stride(via_stride, kWidth, kHeight, kStride);
  ASSERT_TRUE(roi_result.has_value()) << to_string(roi_result.error());
  ASSERT_TRUE(stride_result.has_value()) << to_string(stride_result.error());
  EXPECT_EQ(via_roi, via_stride);
}

TEST(BitmapFlipRoiTest, FullFrameRoiPreservesPaddingBits) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  // Unlike the full-row flips, an ROI flip never writes bits outside the ROI,
  // so the padding bits (columns 13-15) keep their garbage.
  constexpr std::size_t kWidth = 13;
  constexpr std::size_t kHeight = 4;
  constexpr std::size_t kStride = 2;
  const Bytes before = random_bytes(kStride * kHeight, 13);
  Bytes image = before;
  const BitmapRoi roi{.x = 0, .y = 0, .width = kWidth, .height = kHeight};
  const FlipResult result =
      flip_bitmap_roi(image, kWidth, kHeight, kStride, roi);
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_TRUE(
      verify_flip(before, image, kWidth, kStride, roi, Padding::kPreserved));
}

TEST(BitmapFlipRoiTest, DoubleFlipRestoresRoiExactly) {
  RecordProperty("requirements", "BMFLP-REQ-005,BMFLP-REQ-011");
  constexpr std::size_t kWidth = 50;
  constexpr std::size_t kHeight = 6;
  constexpr std::size_t kStride = 8;
  const BitmapRoi roi{.x = 5, .y = 1, .width = 27, .height = 4};
  Bytes original = random_bytes(kStride * kHeight, 27);
  // Make ROI row 1 non-palindromic: pixel 5 set, pixel 31 clear.
  original[kStride + 0] = static_cast<std::uint8_t>(original[kStride] | 0x20U);
  original[kStride + 3] =
      static_cast<std::uint8_t>(original[kStride + 3] & 0x7FU);
  Bytes image = original;

  const FlipResult first =
      flip_bitmap_roi(image, kWidth, kHeight, kStride, roi);
  ASSERT_TRUE(first.has_value()) << to_string(first.error());
  EXPECT_NE(image, original);
  const FlipResult second =
      flip_bitmap_roi(image, kWidth, kHeight, kStride, roi);
  ASSERT_TRUE(second.has_value()) << to_string(second.error());
  EXPECT_EQ(image, original);
}

TEST(BitmapFlipRoiTest, OnePixelWideRoiIsIdentity) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  const Bytes before = random_bytes(3 * 4, 1);
  Bytes image = before;
  const FlipResult result = flip_bitmap_roi(
      image, 20, 4, 3, BitmapRoi{.x = 9, .y = 0, .width = 1, .height = 4});
  ASSERT_TRUE(result.has_value()) << to_string(result.error());
  EXPECT_EQ(image, before);
}

TEST(BitmapFlipRoiTest, NoHeapAllocationInUnalignedStrideAndRoiFlips) {
  RecordProperty("requirements", "BMFLP-REQ-015");
  // Lives in the last Follow-up 3 suite because it needs every variant.
  constexpr std::size_t kWidth = 77;
  constexpr std::size_t kHeight = 9;
  constexpr std::size_t kStride = 16;
  constexpr BitmapRoi kRoi{.x = 3, .y = 1, .width = 60, .height = 7};
  Bytes packed = random_bytes(bytes_per_row(kWidth) * kHeight, 21);
  Bytes strided = random_bytes(kStride * kHeight, 22);
  std::array<FlipResult, 3> results{};
  std::size_t allocations = 0;
  {
    const systems_test::AllocationScope scope;
    results[0] = flip_bitmap(packed, kWidth, kHeight);
    results[1] = flip_bitmap_stride(strided, kWidth, kHeight, kStride);
    results[2] = flip_bitmap_roi(strided, kWidth, kHeight, kStride, kRoi);
    flip_bitmap(packed.data(), kWidth, kHeight);
    allocations = scope.allocations();
  }
  EXPECT_EQ(allocations, 0U);
  for (const auto& result : results) {
    EXPECT_TRUE(result.has_value()) << to_string(result.error());
  }
}

// ===========================================================================
// BitmapFlipErrorTest: input validation (all functions)
// ===========================================================================

TEST(BitmapFlipErrorTest, EmptyOrNullSpanIsNullPointer) {
  RecordProperty("requirements", "BMFLP-REQ-017");
  std::array<std::uint8_t, 8> storage{};
  const std::span<std::uint8_t> null_span{};
  const std::span<std::uint8_t> empty_span = std::span{storage}.first(0);
  const BitmapRoi roi{.x = 0, .y = 0, .width = 8, .height = 1};
  for (const auto span : {null_span, empty_span}) {
    const FlipResult packed = flip_bitmap(span, 16, 2);
    ASSERT_FALSE(packed.has_value());
    EXPECT_EQ(packed.error(), BitmapFlipError::kNullPointer)
        << to_string(packed.error());
    const FlipResult strided = flip_bitmap_stride(span, 16, 2, 2);
    ASSERT_FALSE(strided.has_value());
    EXPECT_EQ(strided.error(), BitmapFlipError::kNullPointer)
        << to_string(strided.error());
    const FlipResult region = flip_bitmap_roi(span, 16, 2, 2, roi);
    ASSERT_FALSE(region.has_value());
    EXPECT_EQ(region.error(), BitmapFlipError::kNullPointer)
        << to_string(region.error());
  }
}

TEST(BitmapFlipErrorTest, ZeroDimensionsRejected) {
  RecordProperty("requirements", "BMFLP-REQ-018");
  std::array<std::uint8_t, 16> buffer{};
  const auto expect_zero = [](const FlipResult& result) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BitmapFlipError::kZeroDimension)
        << to_string(result.error());
  };
  expect_zero(flip_bitmap(buffer, 0, 2));
  expect_zero(flip_bitmap(buffer, 16, 0));
  expect_zero(flip_bitmap_stride(buffer, 0, 2, 4));
  expect_zero(flip_bitmap_stride(buffer, 16, 0, 4));
  const BitmapRoi roi{.x = 0, .y = 0, .width = 8, .height = 1};
  expect_zero(flip_bitmap_roi(buffer, 0, 4, 4, roi));
  expect_zero(flip_bitmap_roi(buffer, 32, 0, 4, roi));
  expect_zero(flip_bitmap_roi(
      buffer, 32, 4, 4, BitmapRoi{.x = 0, .y = 0, .width = 0, .height = 1}));
  expect_zero(flip_bitmap_roi(
      buffer, 32, 4, 4, BitmapRoi{.x = 0, .y = 0, .width = 8, .height = 0}));
}

TEST(BitmapFlipErrorTest, StrideSmallerThanRowBytesRejected) {
  RecordProperty("requirements", "BMFLP-REQ-019");
  std::array<std::uint8_t, 64> buffer{};
  const auto expect_stride = [](const FlipResult& result) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BitmapFlipError::kInvalidStride)
        << to_string(result.error());
  };
  expect_stride(flip_bitmap_stride(buffer, 24, 2, 2));  // needs 3
  expect_stride(flip_bitmap_stride(buffer, 9, 2, 1));   // needs 2
  expect_stride(flip_bitmap_stride(buffer, 1, 2, 0));   // needs 1
  expect_stride(flip_bitmap_roi(
      buffer, 24, 2, 2, BitmapRoi{.x = 0, .y = 0, .width = 8, .height = 1}));

  // The exact minimum stride is valid.
  const FlipResult exact = flip_bitmap_stride(buffer, 9, 2, 2);
  EXPECT_TRUE(exact.has_value()) << to_string(exact.error());
}

TEST(BitmapFlipErrorTest, RoiOutsideFramebufferRejected) {
  RecordProperty("requirements", "BMFLP-REQ-020");
  std::array<std::uint8_t, 16> buffer{};  // 32 x 4, stride 4
  const auto roi_error = [&buffer](BitmapRoi roi) {
    return flip_bitmap_roi(buffer, 32, 4, 4, roi);
  };
  const std::array kBadRois{
      BitmapRoi{.x = 20, .y = 0, .width = 13, .height = 1},  // x + w = 33
      BitmapRoi{.x = 32, .y = 0, .width = 1, .height = 1},   // x == width
      BitmapRoi{.x = 0, .y = 3, .width = 8, .height = 2},    // y + h = 5
      BitmapRoi{.x = 0, .y = 4, .width = 8, .height = 1},    // y == height
      // x + w and y + h wrap around to small values.
      BitmapRoi{.x = kMaxSize, .y = 0, .width = 2, .height = 1},
      BitmapRoi{.x = 1, .y = 0, .width = kMaxSize, .height = 1},
      BitmapRoi{.x = 0, .y = kMaxSize, .width = 8, .height = 2},
      BitmapRoi{.x = 0, .y = 1, .width = 8, .height = kMaxSize},
  };
  for (const auto& roi : kBadRois) {
    SCOPED_TRACE(::testing::Message()
                 << "roi x=" << roi.x << " y=" << roi.y << " w=" << roi.width
                 << " h=" << roi.height);
    const FlipResult result = roi_error(roi);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BitmapFlipError::kInvalidRoi)
        << to_string(result.error());
  }

  // An ROI touching the right and bottom edges exactly is valid.
  const FlipResult exact =
      roi_error(BitmapRoi{.x = 19, .y = 1, .width = 13, .height = 3});
  EXPECT_TRUE(exact.has_value()) << to_string(exact.error());
}

TEST(BitmapFlipErrorTest, BufferTooSmallRejectedWithoutOverflow) {
  RecordProperty("requirements", "BMFLP-REQ-021");
  std::array<std::uint8_t, 16> storage{};
  const auto first = [&storage](std::size_t count) {
    return std::span{storage}.first(count);
  };
  const auto expect_small = [](const FlipResult& result) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), BitmapFlipError::kBufferTooSmall)
        << to_string(result.error());
  };
  const BitmapRoi roi{.x = 0, .y = 0, .width = 8, .height = 1};

  expect_small(flip_bitmap(first(7), 16, 4));               // needs 2 * 4 = 8
  expect_small(flip_bitmap(first(5), 10, 3));               // needs 2 * 3 = 6
  expect_small(flip_bitmap_stride(first(7), 16, 2, 4));     // needs 4 * 2 = 8
  expect_small(flip_bitmap_roi(first(15), 32, 4, 4, roi));  // needs 16

  // Sizes whose products wrap around std::size_t must still be rejected. A
  // wrapping check accepts them and the flip then writes out of bounds, so
  // this test aborts under ASan instead of failing an assertion.
  constexpr std::size_t kHalf = (kMaxSize / 2) + 1;   // 2^(N-1)
  expect_small(flip_bitmap(first(16), kMaxSize, 1));  // ceil() must not wrap
  expect_small(flip_bitmap(first(16), kHalf, 16));    // 2^(N-4) * 16 wraps
  expect_small(flip_bitmap_stride(first(16), 8, 2, kHalf));
  expect_small(flip_bitmap_roi(first(16), 8, 2, kHalf, roi));

  // Exactly stride * height bytes is enough.
  const FlipResult exact_packed = flip_bitmap(first(8), 16, 4);
  EXPECT_TRUE(exact_packed.has_value()) << to_string(exact_packed.error());
  const FlipResult exact_strided = flip_bitmap_stride(first(8), 16, 2, 4);
  EXPECT_TRUE(exact_strided.has_value()) << to_string(exact_strided.error());
}

TEST(BitmapFlipErrorTest, ValidationOrderAndBufferUntouchedOnError) {
  RecordProperty("requirements", "BMFLP-REQ-022");
  const Bytes pattern = random_bytes(16, 18);
  Bytes buffer = pattern;
  const auto expect_error = [&](const FlipResult& result,
                                BitmapFlipError expected) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), expected) << to_string(result.error());
    EXPECT_EQ(buffer, pattern) << "buffer modified on error";
  };
  const std::span<std::uint8_t> all{buffer};
  const BitmapRoi bad_roi{.x = 30, .y = 0, .width = 8, .height = 1};

  // Each call has several problems; the first one in the documented order
  // (null, zero, stride, roi, size) wins.
  expect_error(flip_bitmap(std::span<std::uint8_t>{}, 0, 0),
               BitmapFlipError::kNullPointer);
  expect_error(flip_bitmap_stride(all, 24, 0, 1),
               BitmapFlipError::kZeroDimension);
  expect_error(flip_bitmap_roi(all, 32, 8, 1, bad_roi),
               BitmapFlipError::kInvalidStride);
  expect_error(flip_bitmap_roi(all, 32, 8, 4, bad_roi),
               BitmapFlipError::kInvalidRoi);
  expect_error(flip_bitmap(all, 16, 9), BitmapFlipError::kBufferTooSmall);
  expect_error(flip_bitmap_stride(all, 24, 2, 9),
               BitmapFlipError::kBufferTooSmall);
}

TEST(BitmapFlipErrorTest, CStyleWrapperIgnoresInvalidInput) {
  RecordProperty("requirements", "BMFLP-REQ-012");
  flip_bitmap(nullptr, 16, 2);  // must not crash
  flip_bitmap(nullptr, 0, 0);

  std::array<std::uint8_t, 2> image{0x01, 0x00};
  flip_bitmap(image.data(), 0, 1);
  EXPECT_EQ(image, (std::array<std::uint8_t, 2>{0x01, 0x00}));
  flip_bitmap(image.data(), 16, 0);
  EXPECT_EQ(image, (std::array<std::uint8_t, 2>{0x01, 0x00}));

  // A valid call afterwards still flips.
  flip_bitmap(image.data(), 16, 1);
  EXPECT_EQ(image, (std::array<std::uint8_t, 2>{0x00, 0x80}));
}

// ===========================================================================
// BitmapFlipConcurrencyTest: reentrancy (run under TSan)
// ===========================================================================

constexpr std::size_t kThreads = 8;
constexpr auto kLatchCount = static_cast<std::ptrdiff_t>(kThreads) + 1;

TEST(BitmapFlipConcurrencyTest, DisjointRowBandsOfOneImage) {
  RecordProperty("requirements", "BMFLP-REQ-013");
  constexpr std::size_t kWidth = 1003;
  constexpr std::size_t kBandRows = 16;
  constexpr std::size_t kHeight = kBandRows * kThreads;
  constexpr std::size_t kStride = bytes_per_row(kWidth);
  const Bytes before = random_bytes(kStride * kHeight, 19);
  Bytes image = before;
  std::atomic<int> succeeded{0};
  std::latch start{kLatchCount};
  {
    std::vector<std::jthread> workers;
    workers.reserve(kThreads);
    for (std::size_t band = 0; band < kThreads; ++band) {
      const auto rows = std::span{image}.subspan(band * kBandRows * kStride,
                                                 kBandRows * kStride);
      workers.emplace_back([rows, &start, &succeeded] {
        start.arrive_and_wait();
        if (flip_bitmap(rows, kWidth, kBandRows).has_value()) {
          succeeded.fetch_add(1);
        }
      });
    }
    start.arrive_and_wait();
  }  // jthreads join here
  EXPECT_EQ(succeeded.load(), static_cast<int>(kThreads));
  EXPECT_TRUE(verify_full_flip(before, image, kWidth, kHeight, kStride));
}

TEST(BitmapFlipConcurrencyTest, DisjointRoiBandsOfOneFramebuffer) {
  RecordProperty("requirements", "BMFLP-REQ-013");
  constexpr std::size_t kWidth = 1003;
  constexpr std::size_t kBandRows = 16;
  constexpr std::size_t kHeight = kBandRows * kThreads;
  constexpr std::size_t kStride = bytes_per_row(kWidth) + 3;
  constexpr std::size_t kRoiX = 3;
  constexpr std::size_t kRoiWidth = kWidth - 7;
  const Bytes before = random_bytes(kStride * kHeight, 20);
  Bytes framebuffer = before;
  const std::span<std::uint8_t> shared{framebuffer};
  std::atomic<int> succeeded{0};
  std::latch start{kLatchCount};
  {
    std::vector<std::jthread> workers;
    workers.reserve(kThreads);
    for (std::size_t band = 0; band < kThreads; ++band) {
      const BitmapRoi roi{.x = kRoiX,
                          .y = band * kBandRows,
                          .width = kRoiWidth,
                          .height = kBandRows};
      workers.emplace_back([shared, roi, &start, &succeeded] {
        start.arrive_and_wait();
        if (flip_bitmap_roi(shared, kWidth, kHeight, kStride, roi)
                .has_value()) {
          succeeded.fetch_add(1);
        }
      });
    }
    start.arrive_and_wait();
  }
  EXPECT_EQ(succeeded.load(), static_cast<int>(kThreads));
  const BitmapRoi whole{
      .x = kRoiX, .y = 0, .width = kRoiWidth, .height = kHeight};
  EXPECT_TRUE(verify_flip(before, framebuffer, kWidth, kStride, whole,
                          Padding::kPreserved));
}

TEST(BitmapFlipConcurrencyTest, IndependentImagesFlippedRepeatedly) {
  RecordProperty("requirements", "BMFLP-REQ-013");
  constexpr std::size_t kWidth = 77;
  constexpr std::size_t kHeight = 31;
  constexpr std::size_t kStride = bytes_per_row(kWidth);
  constexpr int kFlips = 40;  // even: every image ends where it started
  std::vector<Bytes> originals;
  originals.reserve(kThreads);
  for (std::uint32_t t = 0; t < kThreads; ++t) {
    originals.push_back(random_bytes(kStride * kHeight, 1000U + t));
  }
  std::vector<Bytes> images = originals;
  std::atomic<int> failures{0};
  std::latch start{kLatchCount};
  {
    std::vector<std::jthread> workers;
    workers.reserve(kThreads);
    for (auto& image : images) {
      workers.emplace_back([&image, &start, &failures] {
        start.arrive_and_wait();
        // reverse_bits() first, from every thread at once: a lookup table
        // filled in lazily on first use is a data race that TSan reports.
        for (unsigned value = 0; value <= 0xFFU; ++value) {
          const auto in = static_cast<std::uint8_t>(value);
          if (reverse_bits(reverse_bits(in)) != in ||
              bit_of(reverse_bits(in), 0) != bit_of(in, kBits - 1)) {
            failures.fetch_add(1);
          }
        }
        for (int flip = 0; flip < kFlips; ++flip) {
          if (!flip_bitmap(image, kWidth, kHeight).has_value()) {
            failures.fetch_add(1);
          }
        }
      });
    }
    start.arrive_and_wait();
  }
  EXPECT_EQ(failures.load(), 0);
  for (std::size_t t = 0; t < images.size(); ++t) {
    EXPECT_TRUE(same_pixels(originals[t], images[t], kWidth, kHeight, kStride))
        << "image " << t;
  }
}

}  // namespace
