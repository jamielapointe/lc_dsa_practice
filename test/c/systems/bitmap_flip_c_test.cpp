/**
 * @file bitmap_flip_c_test.cpp
 * @brief Google Test suite for ISO C23 bitmap_flip component.
 * Traceability: BMFLP-REQ-001 through BMFLP-REQ-011, BMFLP-REQ-013,
 *               BMFLP-REQ-015, BMFLP-REQ-017 through BMFLP-REQ-022.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

extern "C" {
#include <systems/bitmap_flip.h>
}

#include "systems_test/allocation_counter.hpp"

namespace {

[[nodiscard]] auto get_pixel(const uint8_t* bitmap, size_t stride_bytes,
                             size_t x, size_t y) noexcept -> uint8_t {
  const size_t byte_idx = (y * stride_bytes) + (x / 8U);
  const size_t bit_idx = x % 8U;
  return static_cast<uint8_t>((bitmap[byte_idx] >> bit_idx) & 1U);
}

// ============================================================================
// Tier 1: Compile-Time Contracts & Bit Reversal (BMFLP-REQ-001, BMFLP-REQ-002)
// ============================================================================

TEST(BitmapFlipCTest, CompileTimeContracts) {
  RecordProperty("requirements", "BMFLP-REQ-001");
  EXPECT_EQ(BITMAP_BITS_PER_BYTE, 8U);
  EXPECT_EQ(BITMAP_MAX_UINT8_COMBINATIONS, 256U);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_OK), 0);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_NULL_POINTER), 1);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_ZERO_DIMENSIONS), 2);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_INVALID_WIDTH), 3);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_INVALID_HEIGHT), 4);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_INVALID_STRIDE), 5);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_BUFFER_TOO_SMALL), 6);
  EXPECT_EQ(static_cast<int32_t>(BITMAP_FLIP_ERR_INVALID_ROI), 7);
}

TEST(BitmapFlipCTest, ReverseBitsKnownValuesAndExhaustive) {
  RecordProperty("requirements", "BMFLP-REQ-002");
  EXPECT_EQ(bitmap_reverse_bits(0x00U), 0x00U);
  EXPECT_EQ(bitmap_reverse_bits(0xFFU), 0xFFU);
  EXPECT_EQ(bitmap_reverse_bits(0x01U), 0x80U);
  EXPECT_EQ(bitmap_reverse_bits(0x80U), 0x01U);
  EXPECT_EQ(bitmap_reverse_bits(0x0FU), 0xF0U);
  EXPECT_EQ(bitmap_reverse_bits(0x55U), 0xAAU);
  EXPECT_EQ(bitmap_reverse_bits(0x3CU), 0x3CU);

  for (size_t val = 0U; val < 256U; ++val) {
    const auto in = static_cast<uint8_t>(val);
    const uint8_t loop_rev = bitmap_reverse_bits(in);
    const uint8_t lookup_rev = bitmap_reverse_bits_lookup(in);
    EXPECT_EQ(loop_rev, lookup_rev);

    // Verify bit-by-bit mapping: bit i moves to bit 7 - i
    for (size_t i = 0U; i < 8U; ++i) {
      const auto bit_in = static_cast<uint8_t>((in >> i) & 1U);
      const auto bit_out = static_cast<uint8_t>((loop_rev >> (7U - i)) & 1U);
      EXPECT_EQ(bit_in, bit_out);
    }
    // Involution property
    EXPECT_EQ(bitmap_reverse_bits(loop_rev), in);
  }
}

// ============================================================================
// Tier 2: Aligned Bitmaps (BMFLP-REQ-001, BMFLP-REQ-003, BMFLP-REQ-004,
// BMFLP-REQ-005)
// ============================================================================

TEST(BitmapFlipCTest, AlignedSingleByteAndMultiByte) {
  RecordProperty("requirements", "BMFLP-REQ-001,BMFLP-REQ-003");
  // 1 row, 8 pixels: [0x01] (p0=1, p1..p7=0) -> after flip: [0x80] (p7=1,
  // p0..p6=0)
  std::array<uint8_t, 1> buf1 = {0x01U};
  EXPECT_EQ(bitmap_flip_aligned(buf1.data(), buf1.size(), 8U, 1U),
            BITMAP_FLIP_OK);
  EXPECT_EQ(buf1[0], 0x80U);

  // 1 row, 16 pixels: [0x01, 0x00] -> [0x00, 0x80]
  std::array<uint8_t, 2> buf2 = {0x01U, 0x00U};
  EXPECT_EQ(bitmap_flip_aligned(buf2.data(), buf2.size(), 16U, 1U),
            BITMAP_FLIP_OK);
  EXPECT_EQ(buf2[0], 0x00U);
  EXPECT_EQ(buf2[1], 0x80U);

  // 1 row, 24 pixels (odd byte count, middle byte): [0x01, 0x0F, 0x00] ->
  // [0x00, 0xF0, 0x80]
  std::array<uint8_t, 3> buf3 = {0x01U, 0x0FU, 0x00U};
  EXPECT_EQ(bitmap_flip_aligned(buf3.data(), buf3.size(), 24U, 1U),
            BITMAP_FLIP_OK);
  EXPECT_EQ(buf3[0], 0x00U);
  EXPECT_EQ(buf3[1], 0xF0U);
  EXPECT_EQ(buf3[2], 0x80U);
}

TEST(BitmapFlipCTest, AlignedMultiRowRowOrder) {
  RecordProperty("requirements", "BMFLP-REQ-004");
  // 2 rows, 16 pixels each (2 bytes per row)
  std::array<uint8_t, 4> buf = {0x01U, 0x02U, 0x04U, 0x08U};
  EXPECT_EQ(bitmap_flip_aligned(buf.data(), buf.size(), 16U, 2U),
            BITMAP_FLIP_OK);
  EXPECT_EQ(buf[0], 0x40U);
  EXPECT_EQ(buf[1], 0x80U);
  EXPECT_EQ(buf[2], 0x10U);
  EXPECT_EQ(buf[3], 0x20U);
}

TEST(BitmapFlipCTest, AlignedDoubleFlipInvolution) {
  RecordProperty("requirements", "BMFLP-REQ-005");
  constexpr size_t kWidth = 32U;
  constexpr size_t kHeight = 4U;
  constexpr size_t kBytes = (kWidth / 8U) * kHeight;
  std::vector<uint8_t> orig(kBytes);
  std::mt19937 rng(42U);
  for (auto& b : orig) {
    b = static_cast<uint8_t>(rng() & 0xFFU);
  }
  std::vector<uint8_t> buf = orig;

  EXPECT_EQ(bitmap_flip_aligned(buf.data(), buf.size(), kWidth, kHeight),
            BITMAP_FLIP_OK);
  EXPECT_NE(buf, orig);
  EXPECT_EQ(bitmap_flip_aligned(buf.data(), buf.size(), kWidth, kHeight),
            BITMAP_FLIP_OK);
  EXPECT_EQ(buf, orig);
}

TEST(BitmapFlipCTest, AlignedInvalidWidthRejection) {
  RecordProperty("requirements", "BMFLP-REQ-003");
  std::array<uint8_t, 8> buf = {0xAAU, 0x55U};
  EXPECT_EQ(bitmap_flip_aligned(buf.data(), buf.size(), 7U, 1U),
            BITMAP_FLIP_ERR_INVALID_WIDTH);
  EXPECT_EQ(bitmap_flip_aligned(buf.data(), buf.size(), 9U, 1U),
            BITMAP_FLIP_ERR_INVALID_WIDTH);
  EXPECT_EQ(buf[0], 0xAAU);
  EXPECT_EQ(buf[1], 0x55U);
}

// ============================================================================
// Tier 3: Unaligned Widths & Padding Bits (BMFLP-REQ-008, BMFLP-REQ-009)
// ============================================================================

TEST(BitmapFlipCTest, UnalignedSubByteWidths) {
  RecordProperty("requirements", "BMFLP-REQ-008,BMFLP-REQ-009");
  for (size_t w = 1U; w < 8U; ++w) {
    std::array<uint8_t, 1> buf = {
        0xFFU};  // All bits set, including padding bits
    EXPECT_EQ(bitmap_flip_unaligned(buf.data(), buf.size(), w, 1U),
              BITMAP_FLIP_OK);
    // Active bits 0..w-1 should all be 1
    for (size_t x = 0U; x < w; ++x) {
      EXPECT_EQ(get_pixel(buf.data(), 1U, x, 0U), 1U);
    }
    // Padding bits w..7 must be strictly zero
    for (size_t x = w; x < 8U; ++x) {
      EXPECT_EQ((buf[0] >> x) & 1U, 0U);
    }
  }
}

TEST(BitmapFlipCTest, UnalignedMatchesPixelDefinition) {
  RecordProperty("requirements", "BMFLP-REQ-001,BMFLP-REQ-008,BMFLP-REQ-009");
  const std::array<size_t, 18> test_widths = {1U,  2U,  5U,  7U,   8U,   9U,
                                              10U, 13U, 16U, 17U,  23U,  31U,
                                              32U, 33U, 65U, 133U, 261U, 605U};

  for (const size_t width : test_widths) {
    constexpr size_t kHeight = 3U;
    const size_t row_bytes = (width + 7U) / 8U;
    const size_t total_bytes = row_bytes * kHeight;

    std::vector<uint8_t> initial(total_bytes);
    std::mt19937 rng(1337U + static_cast<uint32_t>(width));
    for (auto& b : initial) {
      b = static_cast<uint8_t>(rng() & 0xFFU);
    }

    // Standard
    std::vector<uint8_t> buf_std = initial;
    ASSERT_EQ(
        bitmap_flip_unaligned(buf_std.data(), buf_std.size(), width, kHeight),
        BITMAP_FLIP_OK);

    // Optimized candidate
    std::vector<uint8_t> buf_cand = initial;
    ASSERT_EQ(bitmap_flip_unaligned_optimized(buf_cand.data(), buf_cand.size(),
                                              width, kHeight),
              BITMAP_FLIP_OK);

    // Verify candidate matches standard
    EXPECT_EQ(buf_std, buf_cand);

    // Verify pixel mapping: after(x, y) == before(width - 1 - x, y)
    for (size_t y = 0U; y < kHeight; ++y) {
      for (size_t x = 0U; x < width; ++x) {
        const uint8_t before =
            get_pixel(initial.data(), row_bytes, width - 1U - x, y);
        const uint8_t after = get_pixel(buf_std.data(), row_bytes, x, y);
        EXPECT_EQ(after, before)
            << "Mismatch at (" << x << ", " << y << ") for width " << width;
      }
      // Verify padding bits are zero
      if (width % 8U != 0U) {
        const size_t last_byte_idx = (y * row_bytes) + row_bytes - 1U;
        const size_t rem = width % 8U;
        const auto padding_mask = static_cast<uint8_t>(~((1U << rem) - 1U));
        EXPECT_EQ(buf_std[last_byte_idx] & padding_mask, 0U);
      }
    }
  }
}

// ============================================================================
// Tier 4: Strided & Region of Interest (BMFLP-REQ-010, BMFLP-REQ-011)
// ============================================================================

TEST(BitmapFlipCTest, StridePaddingBytesNeverWritten) {
  RecordProperty("requirements", "BMFLP-REQ-010");
  constexpr size_t kWidth = 10U;  // active bytes = 2
  constexpr size_t kStride = 5U;  // stride padding bytes = 3
  constexpr size_t kHeight = 4U;
  constexpr size_t kTotalBytes = kStride * kHeight;
  constexpr uint8_t kCanary = 0xAAU;

  std::vector<uint8_t> buf(kTotalBytes, kCanary);
  // Put random bits into active pixels
  std::mt19937 rng(999U);
  for (size_t y = 0U; y < kHeight; ++y) {
    buf[y * kStride] = static_cast<uint8_t>(rng() & 0xFFU);
    buf[(y * kStride) + 1U] = static_cast<uint8_t>(rng() & 0xFFU);
  }

  ASSERT_EQ(
      bitmap_flip_strided(buf.data(), buf.size(), kWidth, kHeight, kStride),
      BITMAP_FLIP_OK);

  // Stride padding bytes must remain unaltered
  for (size_t y = 0U; y < kHeight; ++y) {
    for (size_t p = 2U; p < kStride; ++p) {
      EXPECT_EQ(buf[(y * kStride) + p], kCanary)
          << "Stride padding corrupted at row " << y << ", byte " << p;
    }
  }
}

TEST(BitmapFlipCTest, RoiFlipsOnlyInsideAndPreservesNeighbours) {
  RecordProperty("requirements", "BMFLP-REQ-011");
  constexpr size_t kFrameWidth = 32U;
  constexpr size_t kFrameHeight = 8U;
  constexpr size_t kStride = 4U;
  constexpr size_t kTotalBytes = kStride * kFrameHeight;

  std::vector<uint8_t> initial(kTotalBytes);
  std::mt19937 rng(777U);
  for (auto& b : initial) {
    b = static_cast<uint8_t>(rng() & 0xFFU);
  }

  std::vector<uint8_t> buf = initial;
  // Sub-byte ROI: (3, 2, 11, 4) -> x starts at bit 3 (inside byte 0), width 11
  const struct bitmap_roi roi = {
      .x = 3U,
      .y = 2U,
      .width = 11U,
      .height = 4U,
  };

  ASSERT_EQ(bitmap_flip_roi(buf.data(), buf.size(), kFrameWidth, kFrameHeight,
                            kStride, roi),
            BITMAP_FLIP_OK);

  // Verify pixels outside ROI are strictly preserved
  for (size_t y = 0U; y < kFrameHeight; ++y) {
    for (size_t x = 0U; x < kFrameWidth; ++x) {
      const bool in_roi = (y >= roi.y && y < roi.y + roi.height && x >= roi.x &&
                           x < roi.x + roi.width);
      if (!in_roi) {
        EXPECT_EQ(get_pixel(buf.data(), kStride, x, y),
                  get_pixel(initial.data(), kStride, x, y))
            << "Pixel outside ROI corrupted at (" << x << ", " << y << ")";
      } else {
        const size_t mirrored_x = (2U * roi.x) + roi.width - 1U - x;
        EXPECT_EQ(get_pixel(buf.data(), kStride, x, y),
                  get_pixel(initial.data(), kStride, mirrored_x, y))
            << "ROI pixel mismatch at (" << x << ", " << y << ")";
      }
    }
  }

  // Double-flip involution for ROI
  ASSERT_EQ(bitmap_flip_roi(buf.data(), buf.size(), kFrameWidth, kFrameHeight,
                            kStride, roi),
            BITMAP_FLIP_OK);
  EXPECT_EQ(buf, initial);
}

// ============================================================================
// Tier 6: Concurrency & Reentrancy (BMFLP-REQ-013)
// ============================================================================

TEST(BitmapFlipCTest, ReentrancyDisjointRows) {
  RecordProperty("requirements", "BMFLP-REQ-013");
  constexpr size_t kWidth = 32U;
  constexpr size_t kHeight = 100U;
  constexpr size_t kBytesPerRow = 4U;
  constexpr size_t kTotalBytes = kBytesPerRow * kHeight;
  std::vector<uint8_t> buffer(kTotalBytes, 0x55U);

  constexpr size_t kNumThreads = 4U;
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);

  for (size_t t = 0U; t < kNumThreads; ++t) {
    threads.emplace_back([t, &buffer] {
      const size_t rows_per_thread = kHeight / kNumThreads;
      const size_t start_row = t * rows_per_thread;
      uint8_t* const sub_buf = buffer.data() + (start_row * kBytesPerRow);
      const size_t sub_size = rows_per_thread * kBytesPerRow;
      EXPECT_EQ(bitmap_flip_aligned(sub_buf, sub_size, kWidth, rows_per_thread),
                BITMAP_FLIP_OK);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Each byte 0x55 flipped is 0xAA
  for (const uint8_t b : buffer) {
    EXPECT_EQ(b, 0xAAU);
  }
}

// ============================================================================
// Tier 7: Zero Steady-State Heap Allocation (BMFLP-REQ-015)
// ============================================================================

TEST(BitmapFlipCTest, ZeroHeapAllocation) {
  RecordProperty("requirements", "BMFLP-REQ-015");
  constexpr size_t kWidth = 64U;
  constexpr size_t kHeight = 4U;
  constexpr size_t kBytes = (kWidth / 8U) * kHeight;
  std::array<uint8_t, kBytes> buf = {};

  const struct bitmap_roi roi = {.x = 4U, .y = 1U, .width = 20U, .height = 2U};

  {
    const systems_test::AllocationScope scope;
    EXPECT_EQ(bitmap_flip_aligned(buf.data(), buf.size(), kWidth, kHeight),
              BITMAP_FLIP_OK);
    EXPECT_EQ(bitmap_flip_unaligned(buf.data(), buf.size(), kWidth, kHeight),
              BITMAP_FLIP_OK);
    EXPECT_EQ(bitmap_flip_unaligned_optimized(buf.data(), buf.size(), kWidth,
                                              kHeight),
              BITMAP_FLIP_OK);
    EXPECT_EQ(bitmap_flip_strided(buf.data(), buf.size(), kWidth, kHeight,
                                  kWidth / 8U),
              BITMAP_FLIP_OK);
    EXPECT_EQ(bitmap_flip_roi(buf.data(), buf.size(), kWidth, kHeight,
                              kWidth / 8U, roi),
              BITMAP_FLIP_OK);
    EXPECT_EQ(scope.allocations(), 0U);
  }
}

// ============================================================================
// Tier 8: Validation Order & Untouched On Error (BMFLP-REQ-017 through 022)
// ============================================================================

TEST(BitmapFlipCTest, ValidationOrderAndUntouchedOnError) {
  RecordProperty("requirements", "BMFLP-REQ-017,BMFLP-REQ-018,BMFLP-REQ-019");
  RecordProperty("requirements", "BMFLP-REQ-020,BMFLP-REQ-021,BMFLP-REQ-022");
  std::array<uint8_t, 16> canary = {0xDE, 0xAD, 0xBE, 0xEF};
  const std::array<uint8_t, 16> expected_canary = {0xDE, 0xAD, 0xBE, 0xEF};

  // 1. Null pointer / empty buffer
  EXPECT_EQ(bitmap_flip_aligned(nullptr, 16U, 8U, 1U),
            BITMAP_FLIP_ERR_NULL_POINTER);
  EXPECT_EQ(bitmap_flip_aligned(canary.data(), 0U, 8U, 1U),
            BITMAP_FLIP_ERR_NULL_POINTER);
  EXPECT_EQ(bitmap_flip_unaligned_optimized(nullptr, 16U, 10U, 1U),
            BITMAP_FLIP_ERR_NULL_POINTER);
  EXPECT_EQ(bitmap_flip_unaligned_optimized(canary.data(), 0U, 10U, 1U),
            BITMAP_FLIP_ERR_NULL_POINTER);

  // 2. Zero dimension takes precedence over invalid width / size
  EXPECT_EQ(bitmap_flip_aligned(canary.data(), canary.size(), 0U, 1U),
            BITMAP_FLIP_ERR_ZERO_DIMENSIONS);
  EXPECT_EQ(bitmap_flip_aligned(canary.data(), canary.size(), 8U, 0U),
            BITMAP_FLIP_ERR_ZERO_DIMENSIONS);
  EXPECT_EQ(
      bitmap_flip_unaligned_optimized(canary.data(), canary.size(), 0U, 1U),
      BITMAP_FLIP_ERR_ZERO_DIMENSIONS);
  EXPECT_EQ(
      bitmap_flip_unaligned_optimized(canary.data(), canary.size(), 10U, 0U),
      BITMAP_FLIP_ERR_ZERO_DIMENSIONS);

  // 3. Invalid width takes precedence over buffer too small (aligned)
  EXPECT_EQ(bitmap_flip_aligned(canary.data(), 1U, 15U, 100U),
            BITMAP_FLIP_ERR_INVALID_WIDTH);

  // 4. Invalid stride takes precedence over buffer too small
  EXPECT_EQ(bitmap_flip_strided(canary.data(), 1U, 16U, 100U, 1U),
            BITMAP_FLIP_ERR_INVALID_STRIDE);

  // 5. Invalid ROI takes precedence over buffer too small (BMFLP-REQ-022)
  const struct bitmap_roi out_of_bounds_roi = {
      .x = 10U, .y = 0U, .width = 30U, .height = 1U};
  EXPECT_EQ(bitmap_flip_roi(canary.data(), 1U, 32U, 1U, 4U, out_of_bounds_roi),
            BITMAP_FLIP_ERR_INVALID_ROI);

  // 6. Buffer too small
  EXPECT_EQ(
      bitmap_flip_aligned(canary.data(), 2U, 16U, 2U),  // requires 4 bytes
      BITMAP_FLIP_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(
      bitmap_flip_unaligned(canary.data(), 2U, 10U, 2U),  // requires 4 bytes
      BITMAP_FLIP_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(bitmap_flip_unaligned_optimized(canary.data(), 2U, 10U,
                                            2U),  // requires 4 bytes
            BITMAP_FLIP_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(
      bitmap_flip_strided(canary.data(), 2U, 8U, 2U, 4U),  // requires 8 bytes
      BITMAP_FLIP_ERR_BUFFER_TOO_SMALL);

  // Confirm buffer was never touched on any error
  EXPECT_EQ(std::memcmp(canary.data(), expected_canary.data(), canary.size()),
            0);

  // Error string mapping
  EXPECT_STREQ(bitmap_flip_error_to_string(BITMAP_FLIP_OK), "ok");
  EXPECT_STREQ(bitmap_flip_error_to_string(BITMAP_FLIP_ERR_NULL_POINTER),
               "null or empty buffer");
}

}  // namespace
