/**
 * @file bit_pattern_c_test.cpp
 * @brief Google Test suite for ISO C23 bit_pattern component.
 * Traceability: BITPAT-REQ-001 through BITPAT-REQ-006, BITPAT-REQ-008,
 *               BITPAT-REQ-009, BITPAT-REQ-011 through BITPAT-REQ-014.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

extern "C" {
#include <systems/bit_pattern.h>
}

#include "systems_test/allocation_counter.hpp"

namespace {

// ============================================================================
// Tier 1: Byte-Aligned & Cross-Byte Functional Tests
//         (BITPAT-REQ-001, BITPAT-REQ-002, BITPAT-REQ-014)
// ============================================================================

TEST(BitPatternCTest, ByteAlignedExactAndCrossByteMatches) {
  RecordProperty("requirements", "BITPAT-REQ-001,BITPAT-REQ-002");
  RecordProperty("requirements", "BITPAT-REQ-014");

  // 24-bit stream: 0x12 (00010010), 0x34 (00110100), 0x56 (01010110)
  const std::array<uint8_t, 3> data = {0x12U, 0x34U, 0x56U};

  // 1. Match at byte-aligned bit offset 8 (0x34 and {0x34, 0x56})
  const std::array<uint8_t, 1> pat_aligned = {0x34U};
  const std::array<uint8_t, 2> pat_aligned_16 = {0x34U, 0x56U};
  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 24U, pat_aligned.data(), 8U),
      FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_byte_aligned(data.data(), 24U,
                                          pat_aligned_16.data(), 16U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_naive(data.data(), 24U, pat_aligned.data(), 8U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(data.data(), 24U, pat_aligned.data(), 8U),
            FOUND_PATTERN);
  EXPECT_EQ(found(data.data(), 24U, pat_aligned.data(), 8U), FOUND_PATTERN);

  // 2. Match at cross-byte bit offset 4:
  //    low nibble of 0x12 (0010) + high nibble of 0x34 (0011) = 00100011 = 0x23
  //    Tier 1 (byte-aligned only) does not match cross-byte offsets, whereas
  //    Tiers 2 and 3 match at bit offset 4.
  const std::array<uint8_t, 1> pat_cross = {0x23U};
  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 24U, pat_cross.data(), 8U),
      PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_naive(data.data(), 24U, pat_cross.data(), 8U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(data.data(), 24U, pat_cross.data(), 8U),
            FOUND_PATTERN);
  EXPECT_EQ(found(data.data(), 24U, pat_cross.data(), 8U), FOUND_PATTERN);

  // 3. 16-bit cross-byte match starting at bit offset 4:
  //    bits 4..19 = 00100011 01000101 = {0x23, 0x45}
  const std::array<uint8_t, 2> pat_cross_16 = {0x23U, 0x45U};
  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 24U, pat_cross_16.data(), 16U),
      PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_naive(data.data(), 24U, pat_cross_16.data(), 16U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(data.data(), 24U, pat_cross_16.data(), 16U),
            FOUND_PATTERN);
  EXPECT_EQ(found(data.data(), 24U, pat_cross_16.data(), 16U), FOUND_PATTERN);

  // 4. Pattern absent from stream
  const std::array<uint8_t, 1> pat_missing = {0xFFU};
  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 24U, pat_missing.data(), 8U),
      PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_naive(data.data(), 24U, pat_missing.data(), 8U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_kmp(data.data(), 24U, pat_missing.data(), 8U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(found(data.data(), 24U, pat_missing.data(), 8U), PATTERN_NOT_FOUND);

  // 5. Byte-aligned function rejects non-multiple-of-8 sizes
  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 11U, pat_aligned.data(), 8U),
      FOUND_ERR_UNALIGNED_SIZE);
  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 24U, pat_aligned.data(), 5U),
      FOUND_ERR_UNALIGNED_SIZE);
}

// ============================================================================
// Tier 2: Non-Byte-Aligned Bit Sizes & Padding Invariance
//         (BITPAT-REQ-001, BITPAT-REQ-003, BITPAT-REQ-004, BITPAT-REQ-005)
// ============================================================================

TEST(BitPatternCTest, NonByteAlignedSizesSubByteAndMultiByte) {
  RecordProperty("requirements", "BITPAT-REQ-001,BITPAT-REQ-003");
  RecordProperty("requirements", "BITPAT-REQ-005");

  // 1. 11-bit stream: 10110110 101(00000) -> {0xB6, 0xA0}
  //    Bits 6..10 are 10101 -> {0xA8} (pattern_size = 5)
  const std::array<uint8_t, 2> stream_11 = {0xB6U, 0xA0U};
  const std::array<uint8_t, 1> pat_5_hit = {0xA8U};   // 10101(000)
  const std::array<uint8_t, 1> pat_5_miss = {0x40U};  // 01000(000)

  EXPECT_EQ(bit_pattern_find_naive(stream_11.data(), 11U, pat_5_hit.data(), 5U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(stream_11.data(), 11U, pat_5_hit.data(), 5U),
            FOUND_PATTERN);
  EXPECT_EQ(found(stream_11.data(), 11U, pat_5_hit.data(), 5U), FOUND_PATTERN);

  EXPECT_EQ(
      bit_pattern_find_naive(stream_11.data(), 11U, pat_5_miss.data(), 5U),
      PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_kmp(stream_11.data(), 11U, pat_5_miss.data(), 5U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(found(stream_11.data(), 11U, pat_5_miss.data(), 5U),
            PATTERN_NOT_FOUND);

  // 2. 31-bit stream with a 13-bit pattern spanning 3 bytes (bits 13..25):
  //    Byte 0: 00000000 (0x00)
  //    Byte 1: 00000111 (0x07) -> bits 13..15 = 111
  //    Byte 2: 10101111 (0xAF) -> bits 16..23 = 10101111
  //    Byte 3: 10000000 (0x80) -> bits 24..25 = 10, bits 26..30 = 00000
  //    13-bit pattern = 11110101 11110(000) = {0xF5, 0xF0}
  const std::array<uint8_t, 4> stream_31 = {0x00U, 0x07U, 0xAFU, 0x80U};
  const std::array<uint8_t, 2> pat_13 = {0xF5U, 0xF0U};
  EXPECT_EQ(bit_pattern_find_naive(stream_31.data(), 31U, pat_13.data(), 13U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(stream_31.data(), 31U, pat_13.data(), 13U),
            FOUND_PATTERN);
  EXPECT_EQ(found(stream_31.data(), 31U, pat_13.data(), 13U), FOUND_PATTERN);

  // 3. Single-bit stream and single-bit pattern (data_size = 1, pattern_size =
  // 1)
  const std::array<uint8_t, 1> bit_one = {0x80U};
  const std::array<uint8_t, 1> bit_zero = {0x00U};
  EXPECT_EQ(bit_pattern_find_naive(bit_one.data(), 1U, bit_one.data(), 1U),
            FOUND_PATTERN);
  EXPECT_EQ(found(bit_one.data(), 1U, bit_one.data(), 1U), FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_naive(bit_one.data(), 1U, bit_zero.data(), 1U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(found(bit_one.data(), 1U, bit_zero.data(), 1U), PATTERN_NOT_FOUND);
}

TEST(BitPatternCTest, TrailingPaddingBitsIgnoredInDataAndPattern) {
  RecordProperty("requirements", "BITPAT-REQ-004");

  // 1. Garbage 1s in the trailing 5 padding bits of an 11-bit all-zero stream
  //    must NOT match a 5-bit all-ones pattern.
  const std::array<uint8_t, 2> padded_data = {0x00U,
                                              0x1FU};  // 11 zeros + 11111
  const std::array<uint8_t, 1> ones_pat = {0xF8U};     // 11111(000)
  EXPECT_EQ(
      bit_pattern_find_naive(padded_data.data(), 11U, ones_pat.data(), 5U),
      PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_kmp(padded_data.data(), 11U, ones_pat.data(), 5U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(found(padded_data.data(), 11U, ones_pat.data(), 5U),
            PATTERN_NOT_FOUND);

  // 2. Garbage 1s in the trailing 3 padding bits of a 5-bit pattern (10101)
  //    must NOT prevent matching an alternating 9-bit stream (10101010 1).
  const std::array<uint8_t, 2> alt_stream = {0xAAU, 0x80U};
  const std::array<uint8_t, 1> noisy_pat = {0xAFU};  // 10101 + 111 garbage
  EXPECT_EQ(bit_pattern_find_naive(alt_stream.data(), 9U, noisy_pat.data(), 5U),
            FOUND_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(alt_stream.data(), 9U, noisy_pat.data(), 5U),
            FOUND_PATTERN);
  EXPECT_EQ(found(alt_stream.data(), 9U, noisy_pat.data(), 5U), FOUND_PATTERN);
}

// ============================================================================
// Tier 3: KMP Self-Overlapping & Max-Length Stress Tests (BITPAT-REQ-005)
// ============================================================================

TEST(BitPatternCTest, KmpSelfOverlappingAndMaxConstraintPatterns) {
  RecordProperty("requirements", "BITPAT-REQ-005");

  // 1. Repetitive self-overlapping pattern: 31 ones + 0 inside 63 ones + 0
  const std::array<uint8_t, 8> rep_data = {0xFFU, 0xFFU, 0xFFU, 0xFFU,
                                           0xFFU, 0xFFU, 0xFFU, 0xFEU};
  const std::array<uint8_t, 4> rep_pat = {0xFFU, 0xFFU, 0xFFU, 0xFEU};
  EXPECT_EQ(bit_pattern_find_kmp(rep_data.data(), 64U, rep_pat.data(), 32U),
            FOUND_PATTERN);
  EXPECT_EQ(found(rep_data.data(), 64U, rep_pat.data(), 32U), FOUND_PATTERN);

  // 2. Periodic fallback transition: pattern "10101011" (8 bits = 0xAB)
  //    inside "10101010101011" (14 bits = {0xAA, 0xAC}) at bit offset 6
  const std::array<uint8_t, 2> periodic_data = {0xAAU, 0xACU};
  const std::array<uint8_t, 1> periodic_pat = {0xABU};
  EXPECT_EQ(bit_pattern_find_naive(periodic_data.data(), 14U,
                                   periodic_pat.data(), 8U),
            FOUND_PATTERN);
  EXPECT_EQ(
      bit_pattern_find_kmp(periodic_data.data(), 14U, periodic_pat.data(), 8U),
      FOUND_PATTERN);
  EXPECT_EQ(found(periodic_data.data(), 14U, periodic_pat.data(), 8U),
            FOUND_PATTERN);

  // 3. Max pattern length (BIT_PATTERN_MAX_PATTERN_BITS = 1024 bits = 128
  // bytes)
  std::array<uint8_t, 256> large_data{};
  large_data.fill(0xFFU);
  large_data[255] = 0xFEU;  // single 0-bit at the very last bit (bit 2047)

  std::array<uint8_t, 128> max_pat{};
  max_pat.fill(0xFFU);
  max_pat[127] = 0xFEU;  // 1023 ones followed by a single 0-bit

  EXPECT_EQ(bit_pattern_find_kmp(large_data.data(), 2048U, max_pat.data(),
                                 BIT_PATTERN_MAX_PATTERN_BITS),
            FOUND_PATTERN);
  EXPECT_EQ(found(large_data.data(), 2048U, max_pat.data(),
                  BIT_PATTERN_MAX_PATTERN_BITS),
            FOUND_PATTERN);
}

// ============================================================================
// Tier 4: Boundary & Validation Error Precedence
//         (BITPAT-REQ-006, BITPAT-REQ-011, BITPAT-REQ-012, BITPAT-REQ-013)
// ============================================================================

TEST(BitPatternCTest, PatternLongerThanDataReturnsNotFound) {
  RecordProperty("requirements", "BITPAT-REQ-006");

  const std::array<uint8_t, 2> data = {0xAAU, 0x55U};
  const std::array<uint8_t, 3> long_pat = {0xAAU, 0x55U, 0xAAU};

  EXPECT_EQ(
      bit_pattern_find_byte_aligned(data.data(), 16U, long_pat.data(), 24U),
      PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_naive(data.data(), 11U, long_pat.data(), 12U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(bit_pattern_find_kmp(data.data(), 11U, long_pat.data(), 12U),
            PATTERN_NOT_FOUND);
  EXPECT_EQ(found(data.data(), 11U, long_pat.data(), 12U), PATTERN_NOT_FOUND);
}

TEST(BitPatternCTest, ValidationPrecedenceAndBoundaryErrors) {
  RecordProperty("requirements", "BITPAT-REQ-011,BITPAT-REQ-012");
  RecordProperty("requirements", "BITPAT-REQ-013");

  const std::array<uint8_t, 2> buf = {0xAAU, 0x55U};

  // 1. Null pointers take highest precedence (even when sizes are 0 or
  // unaligned)
  EXPECT_EQ(bit_pattern_find_byte_aligned(nullptr, 0U, buf.data(), 0U),
            FOUND_ERR_NULL_PTR);
  EXPECT_EQ(bit_pattern_find_byte_aligned(buf.data(), 0U, nullptr, 0U),
            FOUND_ERR_NULL_PTR);
  EXPECT_EQ(bit_pattern_find_naive(nullptr, 0U, buf.data(), 0U),
            FOUND_ERR_NULL_PTR);
  EXPECT_EQ(bit_pattern_find_naive(buf.data(), 0U, nullptr, 0U),
            FOUND_ERR_NULL_PTR);
  EXPECT_EQ(bit_pattern_find_kmp(nullptr, 0U, buf.data(), 0U),
            FOUND_ERR_NULL_PTR);
  EXPECT_EQ(bit_pattern_find_kmp(buf.data(), 0U, nullptr, 0U),
            FOUND_ERR_NULL_PTR);
  EXPECT_EQ(found(nullptr, 8U, buf.data(), 8U), FOUND_ERR_NULL_PTR);
  EXPECT_EQ(found(buf.data(), 8U, nullptr, 8U), FOUND_ERR_NULL_PTR);

  // 2. Empty data (data_size == 0) takes precedence over empty pattern
  EXPECT_EQ(bit_pattern_find_byte_aligned(buf.data(), 0U, buf.data(), 0U),
            FOUND_ERR_EMPTY_DATA);
  EXPECT_EQ(bit_pattern_find_naive(buf.data(), 0U, buf.data(), 0U),
            FOUND_ERR_EMPTY_DATA);
  EXPECT_EQ(bit_pattern_find_kmp(buf.data(), 0U, buf.data(), 0U),
            FOUND_ERR_EMPTY_DATA);
  EXPECT_EQ(found(buf.data(), 0U, buf.data(), 8U), FOUND_ERR_EMPTY_DATA);

  // 3. Empty pattern (pattern_size == 0) takes precedence over unaligned size
  EXPECT_EQ(bit_pattern_find_byte_aligned(buf.data(), 11U, buf.data(), 0U),
            FOUND_ERR_EMPTY_PATTERN);
  EXPECT_EQ(bit_pattern_find_naive(buf.data(), 11U, buf.data(), 0U),
            FOUND_ERR_EMPTY_PATTERN);
  EXPECT_EQ(bit_pattern_find_kmp(buf.data(), 11U, buf.data(), 0U),
            FOUND_ERR_EMPTY_PATTERN);
  EXPECT_EQ(found(buf.data(), 11U, buf.data(), 0U), FOUND_ERR_EMPTY_PATTERN);

  // 4. Unaligned size takes precedence over pattern too long in byte-aligned
  // tier
  EXPECT_EQ(bit_pattern_find_byte_aligned(buf.data(), 16U, buf.data(), 1025U),
            FOUND_ERR_UNALIGNED_SIZE);

  // 5. Pattern too long (> 1024) takes precedence over pattern_size > data_size
  EXPECT_EQ(bit_pattern_find_byte_aligned(buf.data(), 16U, buf.data(), 1032U),
            FOUND_ERR_PATTERN_TOO_LONG);
  EXPECT_EQ(bit_pattern_find_naive(buf.data(), 16U, buf.data(), 1025U),
            FOUND_ERR_PATTERN_TOO_LONG);
  EXPECT_EQ(bit_pattern_find_kmp(buf.data(), 16U, buf.data(), 1025U),
            FOUND_ERR_PATTERN_TOO_LONG);
  EXPECT_EQ(found(buf.data(), 16U, buf.data(), 1025U),
            FOUND_ERR_PATTERN_TOO_LONG);

  // Verify error strings
  EXPECT_STREQ(found_error_to_string(FOUND_PATTERN), "pattern found");
  EXPECT_STREQ(found_error_to_string(PATTERN_NOT_FOUND), "pattern not found");
  EXPECT_NE(found_error_to_string(FOUND_ERR_NOT_IMPLEMENTED), nullptr);
  EXPECT_NE(found_error_to_string(FOUND_ERR_NULL_PTR), nullptr);
  EXPECT_NE(found_error_to_string(FOUND_ERR_EMPTY_DATA), nullptr);
  EXPECT_NE(found_error_to_string(FOUND_ERR_EMPTY_PATTERN), nullptr);
  EXPECT_NE(found_error_to_string(FOUND_ERR_PATTERN_TOO_LONG), nullptr);
  EXPECT_NE(found_error_to_string(FOUND_ERR_UNALIGNED_SIZE), nullptr);
}

// ============================================================================
// Tier 5: Resource / Zero Heap Allocation (BITPAT-REQ-009)
// ============================================================================

TEST(BitPatternCTest, ZeroHeapAllocationAcrossAllTiers) {
  RecordProperty("requirements", "BITPAT-REQ-009");

  const std::array<uint8_t, 4> data = {0x12U, 0x34U, 0x56U, 0x78U};
  const std::array<uint8_t, 2> aligned_pat = {0x34U, 0x56U};
  const std::array<uint8_t, 2> pat = {0x23U, 0x45U};

  enum FoundError aligned_res = FOUND_ERR_NOT_IMPLEMENTED;
  enum FoundError naive_res = FOUND_ERR_NOT_IMPLEMENTED;
  enum FoundError kmp_res = FOUND_ERR_NOT_IMPLEMENTED;
  enum FoundError found_res = FOUND_ERR_NOT_IMPLEMENTED;
  std::size_t allocations = 0U;

  {
    const systems_test::AllocationScope scope;
    aligned_res = bit_pattern_find_byte_aligned(data.data(), 32U,
                                                aligned_pat.data(), 16U);
    naive_res = bit_pattern_find_naive(data.data(), 31U, pat.data(), 13U);
    kmp_res = bit_pattern_find_kmp(data.data(), 31U, pat.data(), 13U);
    found_res = found(data.data(), 31U, pat.data(), 13U);
    allocations = scope.allocations();
  }

  EXPECT_EQ(allocations, 0U);
  EXPECT_EQ(aligned_res, FOUND_PATTERN);
  EXPECT_EQ(naive_res, FOUND_PATTERN);
  EXPECT_EQ(kmp_res, FOUND_PATTERN);
  EXPECT_EQ(found_res, FOUND_PATTERN);
}

// ============================================================================
// Tier 6: Multi-Thread Reentrancy Stress (BITPAT-REQ-008)
// ============================================================================

TEST(BitPatternConcurrencyCTest, ConcurrentSearchOnSharedAndLocalStreams) {
  RecordProperty("requirements", "BITPAT-REQ-008");

  constexpr size_t kThreads = 4U;
  constexpr size_t kIterations = 100U;
  const std::array<uint8_t, 4> shared_data = {0x12U, 0x34U, 0x56U, 0x78U};
  const std::array<uint8_t, 2> shared_aligned_pat = {0x34U, 0x56U};
  const std::array<uint8_t, 2> shared_pat = {0x23U, 0x45U};
  std::atomic<size_t> successes{0U};

  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (size_t t = 0U; t < kThreads; ++t) {
    workers.emplace_back([&shared_data, &shared_aligned_pat, &shared_pat,
                          &successes] {
      const std::array<uint8_t, 2> local_miss = {0xFFU, 0xFFU};
      for (size_t i = 0U; i < kIterations; ++i) {
        if (bit_pattern_find_byte_aligned(shared_data.data(), 32U,
                                          shared_aligned_pat.data(),
                                          16U) == FOUND_PATTERN &&
            bit_pattern_find_naive(shared_data.data(), 31U, shared_pat.data(),
                                   13U) == FOUND_PATTERN &&
            bit_pattern_find_kmp(shared_data.data(), 31U, shared_pat.data(),
                                 13U) == FOUND_PATTERN &&
            found(shared_data.data(), 31U, shared_pat.data(), 13U) ==
                FOUND_PATTERN &&
            found(shared_data.data(), 31U, local_miss.data(), 13U) ==
                PATTERN_NOT_FOUND) {
          successes.fetch_add(1U, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  EXPECT_EQ(successes.load(), kThreads * kIterations);
}

}  // namespace
