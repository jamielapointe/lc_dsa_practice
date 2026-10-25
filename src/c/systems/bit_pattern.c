/// @file bit_pattern.c
/// @brief MSB-first bit-stream pattern matching in ISO C23.
///
/// Specification: docs/systems/bit_pattern.md (Prefix BITPAT).
///
/// Complexity analysis (BITPAT-REQ-007):
///   1. Tier 1 (`bit_pattern_find_byte_aligned`):
///      - Time: O(((N - M) / 8 + 1) * (M / 8)) byte comparisons across
///        byte-aligned starting offsets where N = `data_size` and
///        M = `pattern_size`.
///      - Space: O(1) auxiliary stack space, zero heap allocation.
///   2. Tier 2 (`bit_pattern_find_naive`):
///      - Time: O((N - M + 1) * M) = O(N * M) bit comparisons in the worst
///        case.
///      - Space: O(1) auxiliary stack space, zero heap allocation.
///   3. Tier 3 (`bit_pattern_find_kmp` and `found`):
///      - Time: O(M) to compute the KMP `lps` failure table (at most 2*M loop
///        steps) + O(N) single-pass stream scan (at most 2*N loop steps) =
///        O(N + M) worst-case time.
///      - Space: O(1) bounded stack space (`uint16_t lps[1024]` = 2048 bytes),
///        zero heap allocation, no recursion, and no VLAs.

#include "systems/bit_pattern.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

[[nodiscard]] const char* found_error_to_string(const enum FoundError error) {
  switch (error) {
    case FOUND_PATTERN:
      return "pattern found";
    case PATTERN_NOT_FOUND:
      return "pattern not found";
    case FOUND_ERR_NOT_IMPLEMENTED:
      return "not implemented";
    case FOUND_ERR_NULL_PTR:
      return "null pointer argument";
    case FOUND_ERR_EMPTY_DATA:
      return "empty input data bit stream (data_size == 0)";
    case FOUND_ERR_EMPTY_PATTERN:
      return "empty target bit pattern (pattern_size == 0)";
    case FOUND_ERR_PATTERN_TOO_LONG:
      return "pattern_size exceeds BIT_PATTERN_MAX_PATTERN_BITS (1024)";
    case FOUND_ERR_UNALIGNED_SIZE:
      return "data_size and pattern_size must be multiples of 8";
  }
  return "unknown FoundError";
}

[[nodiscard]] enum FoundError bit_pattern_find_byte_aligned(
    const void* const in_data, const size_t data_size,
    const void* const pattern, const size_t pattern_size) {
  if (in_data == nullptr || pattern == nullptr) {
    return FOUND_ERR_NULL_PTR;
  }
  if (data_size == 0U) {
    return FOUND_ERR_EMPTY_DATA;
  }
  if (pattern_size == 0U) {
    return FOUND_ERR_EMPTY_PATTERN;
  }
  if ((data_size % 8U != 0) || (pattern_size % 8U != 0)) {
    return FOUND_ERR_UNALIGNED_SIZE;
  }
  if (pattern_size > BIT_PATTERN_MAX_PATTERN_BITS) {
    return FOUND_ERR_PATTERN_TOO_LONG;
  }
  if (pattern_size > data_size) {
    return PATTERN_NOT_FOUND;
  }

  const uint8_t* data_bytes = (const uint8_t*)in_data;
  const uint8_t* pattern_bytes = (const uint8_t*)pattern;
  const size_t data_size_bytes = data_size >> 3U;
  const size_t pattern_size_bytes = pattern_size >> 3U;

  for (size_t s = 0; s <= data_size_bytes - pattern_size_bytes; ++s) {
    if (memcmp(data_bytes + s, pattern_bytes, pattern_size_bytes) == 0) {
      return FOUND_PATTERN;
    }
  }

  return PATTERN_NOT_FOUND;
}

[[nodiscard]] static inline uint8_t get_msb_bit(const uint8_t* const data_bytes,
                                                const size_t bit_index) {
  const size_t byte_idx = bit_index >> 3U;
  const size_t bit_shift = (uint8_t)(7U - (bit_index & 7U));
  return (data_bytes[byte_idx] >> bit_shift) & 1U;
}

[[nodiscard]] enum FoundError bit_pattern_find_naive(
    const void* const in_data, const size_t data_size,
    const void* const pattern, const size_t pattern_size) {
  if (in_data == nullptr || pattern == nullptr) {
    return FOUND_ERR_NULL_PTR;
  }
  if (data_size == 0U) {
    return FOUND_ERR_EMPTY_DATA;
  }
  if (pattern_size == 0U) {
    return FOUND_ERR_EMPTY_PATTERN;
  }
  if (pattern_size > BIT_PATTERN_MAX_PATTERN_BITS) {
    return FOUND_ERR_PATTERN_TOO_LONG;
  }
  if (pattern_size > data_size) {
    return PATTERN_NOT_FOUND;
  }

  const uint8_t* data_bytes = (const uint8_t*)in_data;
  const uint8_t* pattern_bytes = (const uint8_t*)pattern;

  // safe substraction since data_size >= pattern_size >= 1
  // && pattern_size < BIT_PATTERN_MAX_PATTERN_BITS
  const size_t max_start_bit = data_size - pattern_size;

  for (size_t start_bit = 0U; start_bit <= max_start_bit; ++start_bit) {
    bool is_match = true;

    for (size_t pattern_bit = 0U; pattern_bit < pattern_size; ++pattern_bit) {
      const uint8_t data_bit_val =
          get_msb_bit(data_bytes, start_bit + pattern_bit);
      const uint8_t pattern_bit_val = get_msb_bit(pattern_bytes, pattern_bit);

      if (data_bit_val != pattern_bit_val) {
        is_match = false;
        break;
      }
    }

    if (is_match) {
      return FOUND_PATTERN;
    }
  }

  return PATTERN_NOT_FOUND;
}

static void build_lps(const void* const pattern, size_t pattern_size,
                      uint16_t* const lps) {
  lps[0] = 0;
  size_t len = 0;

  for (size_t i = 1U; i < pattern_size; ++i) {
    const uint8_t b = get_msb_bit(pattern, i);

    while ((len > 0U) && b != get_msb_bit(pattern, len)) {
      len = lps[len - 1];
    }

    if (b == get_msb_bit(pattern, len)) {
      ++len;
    }

    lps[i] = (uint16_t)len;
  }
}

[[nodiscard]] enum FoundError bit_pattern_find_kmp(const void* const in_data,
                                                   const size_t data_size,
                                                   const void* const pattern,
                                                   const size_t pattern_size) {
  if (in_data == nullptr || pattern == nullptr) {
    return FOUND_ERR_NULL_PTR;
  }
  if (data_size == 0U) {
    return FOUND_ERR_EMPTY_DATA;
  }
  if (pattern_size == 0U) {
    return FOUND_ERR_EMPTY_PATTERN;
  }
  if (pattern_size > BIT_PATTERN_MAX_PATTERN_BITS) {
    return FOUND_ERR_PATTERN_TOO_LONG;
  }
  if (pattern_size > data_size) {
    return PATTERN_NOT_FOUND;
  }

  uint16_t lps[BIT_PATTERN_MAX_PATTERN_BITS];
  build_lps(pattern, pattern_size, lps);

  size_t data_idx = 0U;
  size_t pat_idx = 0U;

  while (data_idx < data_size) {
    const uint8_t d_bit = get_msb_bit(in_data, data_idx);
    const uint8_t p_bit = get_msb_bit(pattern, pat_idx);

    if (d_bit == p_bit) {
      ++data_idx;
      ++pat_idx;

      if (pat_idx == pattern_size) {
        return FOUND_PATTERN;
      }
    } else if (pat_idx > 0U) {
      pat_idx = lps[pat_idx - 1];
    } else {
      ++data_idx;
    }
  }

  return PATTERN_NOT_FOUND;
}

[[nodiscard]] enum FoundError found(const void* const in_data,
                                    const size_t data_size,
                                    const void* const pattern,
                                    const size_t pattern_size) {
  return bit_pattern_find_kmp(in_data, data_size, pattern, pattern_size);
}
