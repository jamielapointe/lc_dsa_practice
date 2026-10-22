/// @file bitmap_flip.c
/// @brief In-place horizontal flip of 1-bpp monochrome bitmaps in ISO C23.
///
/// Specification: docs/systems/bitmap_flip.md (Prefix BMFLP).
/// Time complexity (BMFLP-REQ-006): O(width * height) - each pixel/byte is
/// visited a constant number of times. Auxiliary space complexity: O(1) -
/// strictly in-place, zero steady-state heap allocations.
///
/// Follow-up 1 notes (BMFLP-REQ-007):
/// Byte reversal uses a precomputed 256-entry lookup table in .rodata,
/// eliminating loop-based bit extraction. Symmetric byte swapping processes 8
/// pixels at a time per load/store pair, avoiding single-bit operations in the
/// inner loop for aligned rows.
///
/// Follow-up 4 answers (BMFLP-REQ-014):
/// 1. Multicore split: Partition rows across worker threads [row_start,
/// row_end). Each row is fully
///    independent. Workers never share byte memory or cache lines since row
///    pitch is aligned to byte boundaries. For ROI, split rows across workers;
///    horizontal sub-byte splitting should be avoided due to false sharing and
///    byte-masking races at boundaries.
/// 2. Diminishing returns: For small framebuffers (< 4-8 KB), thread
/// synchronization overhead (barrier,
///    kernel wakeup, context switch ~1-5 microseconds) exceeds the entire
///    sequential flip time
///    (~0.5-2 microseconds for 64-bit unrolled SIMD).
/// 3. Display tearing: If flipping occurs while the display controller
/// rasterizes the framebuffer,
///    the upper part will show the old frame and the lower part the new frame
///    (horizontal tearing). Mitigate using double-buffering with VSYNC
///    synchronization (page flipping during vertical blanking).
/// 4. DMA cache maintenance: When the CPU writes to a cached framebuffer read
/// by a DMA display engine,
///    the CPU must clean (flush) the data cache lines to PoC (Point of
///    Coherency / RAM) before triggering DMA. Without cache cleaning, the DMA
///    controller reads stale data from RAM. If the buffer is mapped as
///    non-cacheable / write-combining (WC), explicit cache maintenance is
///    unnecessary.

#include "systems/bitmap_flip.h"

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

/// @brief Precomputed 256-byte compile-time bit reversal table (BMFLP-REQ-002,
/// BMFLP-REQ-013).
static constexpr uint8_t BITMAP_BIT_REVERSE_TABLE[256] = {
    0x00U, 0x80U, 0x40U, 0xC0U, 0x20U, 0xA0U, 0x60U, 0xE0U, 0x10U, 0x90U, 0x50U,
    0xD0U, 0x30U, 0xB0U, 0x70U, 0xF0U, 0x08U, 0x88U, 0x48U, 0xC8U, 0x28U, 0xA8U,
    0x68U, 0xE8U, 0x18U, 0x98U, 0x58U, 0xD8U, 0x38U, 0xB8U, 0x78U, 0xF8U, 0x04U,
    0x84U, 0x44U, 0xC4U, 0x24U, 0xA4U, 0x64U, 0xE4U, 0x14U, 0x94U, 0x54U, 0xD4U,
    0x34U, 0xB4U, 0x74U, 0xF4U, 0x0CU, 0x8CU, 0x4CU, 0xCCU, 0x2CU, 0xACU, 0x6CU,
    0xECU, 0x1CU, 0x9CU, 0x5CU, 0xDCU, 0x3CU, 0xBCU, 0x7CU, 0xFCU, 0x02U, 0x82U,
    0x42U, 0xC2U, 0x22U, 0xA2U, 0x62U, 0xE2U, 0x12U, 0x92U, 0x52U, 0xD2U, 0x32U,
    0xB2U, 0x72U, 0xF2U, 0x0AU, 0x8AU, 0x4AU, 0xCAU, 0x2AU, 0xAAU, 0x6AU, 0xEAU,
    0x1AU, 0x9AU, 0x5AU, 0xDAU, 0x3AU, 0xBAU, 0x7AU, 0xFAU, 0x06U, 0x86U, 0x46U,
    0xC6U, 0x26U, 0xA6U, 0x66U, 0xE6U, 0x16U, 0x96U, 0x56U, 0xD6U, 0x36U, 0xB6U,
    0x76U, 0xF6U, 0x0EU, 0x8EU, 0x4EU, 0xCEU, 0x2EU, 0xAEU, 0x6EU, 0xEEU, 0x1EU,
    0x9EU, 0x5EU, 0xDEU, 0x3EU, 0xBEU, 0x7EU, 0xFEU, 0x01U, 0x81U, 0x41U, 0xC1U,
    0x21U, 0xA1U, 0x61U, 0xE1U, 0x11U, 0x91U, 0x51U, 0xD1U, 0x31U, 0xB1U, 0x71U,
    0xF1U, 0x09U, 0x89U, 0x49U, 0xC9U, 0x29U, 0xA9U, 0x69U, 0xE9U, 0x19U, 0x99U,
    0x59U, 0xD9U, 0x39U, 0xB9U, 0x79U, 0xF9U, 0x05U, 0x85U, 0x45U, 0xC5U, 0x25U,
    0xA5U, 0x65U, 0xE5U, 0x15U, 0x95U, 0x55U, 0xD5U, 0x35U, 0xB5U, 0x75U, 0xF5U,
    0x0DU, 0x8DU, 0x4DU, 0xCDU, 0x2DU, 0xADU, 0x6DU, 0xEDU, 0x1DU, 0x9DU, 0x5DU,
    0xDDU, 0x3DU, 0xBDU, 0x7DU, 0xFDU, 0x03U, 0x83U, 0x43U, 0xC3U, 0x23U, 0xA3U,
    0x63U, 0xE3U, 0x13U, 0x93U, 0x53U, 0xD3U, 0x33U, 0xB3U, 0x73U, 0xF3U, 0x0BU,
    0x8BU, 0x4BU, 0xCBU, 0x2BU, 0xABU, 0x6BU, 0xEBU, 0x1BU, 0x9BU, 0x5BU, 0xDBU,
    0x3BU, 0xBBU, 0x7BU, 0xFBU, 0x07U, 0x87U, 0x47U, 0xC7U, 0x27U, 0xA7U, 0x67U,
    0xE7U, 0x17U, 0x97U, 0x57U, 0xD7U, 0x37U, 0xB7U, 0x77U, 0xF7U, 0x0FU, 0x8FU,
    0x4FU, 0xCFU, 0x2FU, 0xAFU, 0x6FU, 0xEFU, 0x1FU, 0x9FU, 0x5FU, 0xDFU, 0x3FU,
    0xBFU, 0x7FU, 0xFFU,
};

[[nodiscard]] const char* bitmap_flip_error_to_string(
    enum bitmap_flip_error error) {
  switch (error) {
    case BITMAP_FLIP_OK:
      return "ok";
    case BITMAP_FLIP_ERR_NULL_POINTER:
      return "null or empty buffer";
    case BITMAP_FLIP_ERR_ZERO_DIMENSIONS:
      return "zero width or height";
    case BITMAP_FLIP_ERR_INVALID_WIDTH:
      return "width is not a multiple of 8";
    case BITMAP_FLIP_ERR_INVALID_HEIGHT:
      return "invalid height";
    case BITMAP_FLIP_ERR_INVALID_STRIDE:
      return "stride smaller than bytes per row";
    case BITMAP_FLIP_ERR_BUFFER_TOO_SMALL:
      return "buffer too small for stride * height";
    case BITMAP_FLIP_ERR_INVALID_ROI:
      return "roi outside framebuffer";
  }
  return "unknown BitmapFlipError";
}

[[nodiscard]] uint8_t bitmap_reverse_bits(uint8_t value) {
  uint8_t reverse = 0U;
  for (uint8_t i = 0U; i < 8U; ++i) {
    reverse = (uint8_t)(((uint32_t)reverse << 1U) | ((uint32_t)value & 1U));
    value = (uint8_t)((uint32_t)value >> 1U);
  }
  return reverse;
}

[[nodiscard]] uint8_t bitmap_reverse_bits_lookup(uint8_t value) {
  return BITMAP_BIT_REVERSE_TABLE[value];
}

[[nodiscard]] static inline size_t compute_active_bytes(size_t width) {
  return (width / BITMAP_BITS_PER_BYTE) +
         ((width % BITMAP_BITS_PER_BYTE != 0U) ? 1U : 0U);
}

[[nodiscard]] static inline uint64_t load_u64_le(const uint8_t* ptr) {
  return ((uint64_t)ptr[0] << 0U) | ((uint64_t)ptr[1] << 8U) |
         ((uint64_t)ptr[2] << 16U) | ((uint64_t)ptr[3] << 24U) |
         ((uint64_t)ptr[4] << 32U) | ((uint64_t)ptr[5] << 40U) |
         ((uint64_t)ptr[6] << 48U) | ((uint64_t)ptr[7] << 56U);
}

static inline void store_u64_le(uint8_t* ptr, uint64_t val) {
  ptr[0] = (uint8_t)(val >> 0U);
  ptr[1] = (uint8_t)(val >> 8U);
  ptr[2] = (uint8_t)(val >> 16U);
  ptr[3] = (uint8_t)(val >> 24U);
  ptr[4] = (uint8_t)(val >> 32U);
  ptr[5] = (uint8_t)(val >> 40U);
  ptr[6] = (uint8_t)(val >> 48U);
  ptr[7] = (uint8_t)(val >> 56U);
}

[[nodiscard]] static inline uint64_t reverse_bits_u64(uint64_t x) {
  x = ((x & 0x5555555555555555ULL) << 1U) | ((x >> 1U) & 0x5555555555555555ULL);
  x = ((x & 0x3333333333333333ULL) << 2U) | ((x >> 2U) & 0x3333333333333333ULL);
  x = ((x & 0x0F0F0F0F0F0F0F0FULL) << 4U) | ((x >> 4U) & 0x0F0F0F0F0F0F0F0FULL);
  return __builtin_bswap64(x);
}

static void flip_row_standard(uint8_t* row, size_t num_bytes,
                              size_t remainder) {
  for (size_t i = 0U; i < num_bytes / 2U; ++i) {
    const size_t right = num_bytes - 1U - i;
    const uint8_t left_val = row[i];
    const uint8_t right_val = row[right];
    row[i] = bitmap_reverse_bits_lookup(right_val);
    row[right] = bitmap_reverse_bits_lookup(left_val);
  }
  if (num_bytes % 2U == 1U) {
    const size_t mid = num_bytes / 2U;
    row[mid] = bitmap_reverse_bits_lookup(row[mid]);
  }
  if (remainder > 0U) {
    const size_t shift = BITMAP_BITS_PER_BYTE - remainder;
    for (size_t col = 0U; col < num_bytes - 1U; ++col) {
      row[col] = (uint8_t)(((uint32_t)row[col] >> shift) |
                           (((uint32_t)row[col + 1U] << remainder) & 0xFFU));
    }
    row[num_bytes - 1U] = (uint8_t)((uint32_t)row[num_bytes - 1U] >> shift);
  }
}

enum bitmap_flip_error bitmap_flip_aligned(uint8_t* bitmap, size_t buf_size,
                                           size_t width, size_t height) {
  if (bitmap == nullptr || buf_size == 0U) {
    return BITMAP_FLIP_ERR_NULL_POINTER;
  }
  if (width == 0U || height == 0U) {
    return BITMAP_FLIP_ERR_ZERO_DIMENSIONS;
  }
  if ((width % BITMAP_BITS_PER_BYTE) != 0U) {
    return BITMAP_FLIP_ERR_INVALID_WIDTH;
  }
  const size_t num_bytes = width / BITMAP_BITS_PER_BYTE;
  size_t required = 0U;
  if (ckd_mul(&required, num_bytes, height) || buf_size < required) {
    return BITMAP_FLIP_ERR_BUFFER_TOO_SMALL;
  }

  for (size_t row = 0U; row < height; ++row) {
    uint8_t* const row_ptr = bitmap + (row * num_bytes);
    flip_row_standard(row_ptr, num_bytes, 0U);
  }
  return BITMAP_FLIP_OK;
}

enum bitmap_flip_error bitmap_flip_unaligned(uint8_t* bitmap, size_t buf_size,
                                             size_t width, size_t height) {
  if (bitmap == nullptr || buf_size == 0U) {
    return BITMAP_FLIP_ERR_NULL_POINTER;
  }
  if (width == 0U || height == 0U) {
    return BITMAP_FLIP_ERR_ZERO_DIMENSIONS;
  }
  const size_t num_bytes = compute_active_bytes(width);
  size_t required = 0U;
  if (ckd_mul(&required, num_bytes, height) || buf_size < required) {
    return BITMAP_FLIP_ERR_BUFFER_TOO_SMALL;
  }
  const size_t remainder = width % BITMAP_BITS_PER_BYTE;

  for (size_t row = 0U; row < height; ++row) {
    uint8_t* const row_ptr = bitmap + (row * num_bytes);
    flip_row_standard(row_ptr, num_bytes, remainder);
  }
  return BITMAP_FLIP_OK;
}

enum bitmap_flip_error bitmap_flip_unaligned_candidate(uint8_t* bitmap,
                                                       size_t buf_size,
                                                       size_t width,
                                                       size_t height) {
  if (bitmap == nullptr || buf_size == 0U) {
    return BITMAP_FLIP_ERR_NULL_POINTER;
  }
  if (width == 0U || height == 0U) {
    return BITMAP_FLIP_ERR_ZERO_DIMENSIONS;
  }
  const size_t num_bytes = compute_active_bytes(width);
  size_t required = 0U;
  if (ckd_mul(&required, num_bytes, height) || buf_size < required) {
    return BITMAP_FLIP_ERR_BUFFER_TOO_SMALL;
  }
  const size_t remainder = width % BITMAP_BITS_PER_BYTE;
  const size_t half_bytes = num_bytes / 2U;
  constexpr size_t kBytesPerWord64 = sizeof(uint64_t);
  constexpr size_t kBytesPerQuadWord = 4U * kBytesPerWord64;
  constexpr size_t kBytesPerDualWord = 2U * kBytesPerWord64;

  for (size_t row = 0U; row < height; ++row) {
    uint8_t* const row_ptr = bitmap + (row * num_bytes);
    uint8_t* left_ptr = row_ptr;
    uint8_t* right_ptr = row_ptr + num_bytes;
    size_t rem_half = half_bytes;

    while (rem_half >= kBytesPerQuadWord) {
      right_ptr -= kBytesPerQuadWord;
      const uint64_t left_w0 = load_u64_le(left_ptr);
      const uint64_t left_w1 = load_u64_le(left_ptr + kBytesPerWord64);
      const uint64_t left_w2 = load_u64_le(left_ptr + (2U * kBytesPerWord64));
      const uint64_t left_w3 = load_u64_le(left_ptr + (3U * kBytesPerWord64));
      const uint64_t right_w0 = load_u64_le(right_ptr);
      const uint64_t right_w1 = load_u64_le(right_ptr + kBytesPerWord64);
      const uint64_t right_w2 = load_u64_le(right_ptr + (2U * kBytesPerWord64));
      const uint64_t right_w3 = load_u64_le(right_ptr + (3U * kBytesPerWord64));
      store_u64_le(left_ptr, reverse_bits_u64(right_w3));
      store_u64_le(left_ptr + kBytesPerWord64, reverse_bits_u64(right_w2));
      store_u64_le(left_ptr + (2U * kBytesPerWord64),
                   reverse_bits_u64(right_w1));
      store_u64_le(left_ptr + (3U * kBytesPerWord64),
                   reverse_bits_u64(right_w0));
      store_u64_le(right_ptr, reverse_bits_u64(left_w3));
      store_u64_le(right_ptr + kBytesPerWord64, reverse_bits_u64(left_w2));
      store_u64_le(right_ptr + (2U * kBytesPerWord64),
                   reverse_bits_u64(left_w1));
      store_u64_le(right_ptr + (3U * kBytesPerWord64),
                   reverse_bits_u64(left_w0));
      left_ptr += kBytesPerQuadWord;
      rem_half -= kBytesPerQuadWord;
    }

    if (rem_half >= kBytesPerDualWord) {
      right_ptr -= kBytesPerDualWord;
      const uint64_t left_w0 = load_u64_le(left_ptr);
      const uint64_t left_w1 = load_u64_le(left_ptr + kBytesPerWord64);
      const uint64_t right_w0 = load_u64_le(right_ptr);
      const uint64_t right_w1 = load_u64_le(right_ptr + kBytesPerWord64);
      store_u64_le(left_ptr, reverse_bits_u64(right_w1));
      store_u64_le(left_ptr + kBytesPerWord64, reverse_bits_u64(right_w0));
      store_u64_le(right_ptr, reverse_bits_u64(left_w1));
      store_u64_le(right_ptr + kBytesPerWord64, reverse_bits_u64(left_w0));
      left_ptr += kBytesPerDualWord;
      rem_half -= kBytesPerDualWord;
    }

    if (rem_half >= kBytesPerWord64) {
      right_ptr -= kBytesPerWord64;
      const uint64_t left_word = load_u64_le(left_ptr);
      const uint64_t right_word = load_u64_le(right_ptr);
      store_u64_le(left_ptr, reverse_bits_u64(right_word));
      store_u64_le(right_ptr, reverse_bits_u64(left_word));
      left_ptr += kBytesPerWord64;
      rem_half -= kBytesPerWord64;
    }

    while (rem_half > 0U) {
      --right_ptr;
      const uint8_t left_byte = *left_ptr;
      const uint8_t right_byte = *right_ptr;
      *left_ptr = bitmap_reverse_bits_lookup(right_byte);
      *right_ptr = bitmap_reverse_bits_lookup(left_byte);
      ++left_ptr;
      --rem_half;
    }

    if (num_bytes % 2U == 1U) {
      row_ptr[half_bytes] = bitmap_reverse_bits_lookup(row_ptr[half_bytes]);
    }

    if (remainder > 0U) {
      const size_t shift = BITMAP_BITS_PER_BYTE - remainder;
      for (size_t col = 0U; col < num_bytes - 1U; ++col) {
        row_ptr[col] =
            (uint8_t)(((uint32_t)row_ptr[col] >> shift) |
                      (((uint32_t)row_ptr[col + 1U] << remainder) & 0xFFU));
      }
      row_ptr[num_bytes - 1U] =
          (uint8_t)((uint32_t)row_ptr[num_bytes - 1U] >> shift);
    }
  }
  return BITMAP_FLIP_OK;
}

enum bitmap_flip_error bitmap_flip_strided(uint8_t* bitmap, size_t buf_size,
                                           size_t width, size_t height,
                                           size_t stride_bytes) {
  if (bitmap == nullptr || buf_size == 0U) {
    return BITMAP_FLIP_ERR_NULL_POINTER;
  }
  if (width == 0U || height == 0U) {
    return BITMAP_FLIP_ERR_ZERO_DIMENSIONS;
  }
  const size_t num_bytes = compute_active_bytes(width);
  if (stride_bytes < num_bytes) {
    return BITMAP_FLIP_ERR_INVALID_STRIDE;
  }
  size_t required = 0U;
  if (ckd_mul(&required, stride_bytes, height) || buf_size < required) {
    return BITMAP_FLIP_ERR_BUFFER_TOO_SMALL;
  }
  const size_t remainder = width % BITMAP_BITS_PER_BYTE;

  for (size_t row = 0U; row < height; ++row) {
    uint8_t* const row_ptr = bitmap + (row * stride_bytes);
    flip_row_standard(row_ptr, num_bytes, remainder);
  }
  return BITMAP_FLIP_OK;
}

enum bitmap_flip_error bitmap_flip_roi(uint8_t* bitmap, size_t buf_size,
                                       size_t frame_width, size_t frame_height,
                                       size_t stride_bytes,
                                       struct bitmap_roi roi) {
  if (bitmap == nullptr || buf_size == 0U) {
    return BITMAP_FLIP_ERR_NULL_POINTER;
  }
  if (frame_width == 0U || frame_height == 0U || roi.width == 0U ||
      roi.height == 0U) {
    return BITMAP_FLIP_ERR_ZERO_DIMENSIONS;
  }
  const size_t num_bytes_per_row = compute_active_bytes(frame_width);
  if (stride_bytes < num_bytes_per_row) {
    return BITMAP_FLIP_ERR_INVALID_STRIDE;
  }
  size_t roi_right = 0U;
  size_t roi_bottom = 0U;
  if (ckd_add(&roi_right, roi.x, roi.width) || roi_right > frame_width ||
      ckd_add(&roi_bottom, roi.y, roi.height) || roi_bottom > frame_height) {
    return BITMAP_FLIP_ERR_INVALID_ROI;
  }
  size_t required = 0U;
  if (ckd_mul(&required, stride_bytes, frame_height) || buf_size < required) {
    return BITMAP_FLIP_ERR_BUFFER_TOO_SMALL;
  }

  for (size_t row_offset = 0U; row_offset < roi.height; ++row_offset) {
    const size_t start_row = (roi.y + row_offset) * stride_bytes;
    for (size_t dx = 0U; dx < roi.width / 2U; ++dx) {
      const size_t left_x = roi.x + dx;
      const size_t right_x = roi.x + roi.width - 1U - dx;

      const size_t left_byte_idx = start_row + (left_x / BITMAP_BITS_PER_BYTE);
      const size_t left_bit_idx = left_x % BITMAP_BITS_PER_BYTE;

      const size_t right_byte_idx =
          start_row + (right_x / BITMAP_BITS_PER_BYTE);
      const size_t right_bit_idx = right_x % BITMAP_BITS_PER_BYTE;

      const uint8_t left_val =
          (uint8_t)(((uint32_t)bitmap[left_byte_idx] >> left_bit_idx) & 1U);
      const uint8_t right_val =
          (uint8_t)(((uint32_t)bitmap[right_byte_idx] >> right_bit_idx) & 1U);

      if (left_val != right_val) {
        if (right_val != 0U) {
          bitmap[left_byte_idx] =
              (uint8_t)((uint32_t)bitmap[left_byte_idx] | (1U << left_bit_idx));
        } else {
          bitmap[left_byte_idx] = (uint8_t)((uint32_t)bitmap[left_byte_idx] &
                                            ~(1U << left_bit_idx));
        }

        if (left_val != 0U) {
          bitmap[right_byte_idx] = (uint8_t)((uint32_t)bitmap[right_byte_idx] |
                                             (1U << right_bit_idx));
        } else {
          bitmap[right_byte_idx] = (uint8_t)((uint32_t)bitmap[right_byte_idx] &
                                             ~(1U << right_bit_idx));
        }
      }
    }
  }
  return BITMAP_FLIP_OK;
}

void flip_bitmap_aligned(uint8_t* bitmap, size_t width, size_t height) {
  if (bitmap == nullptr || width == 0U || height == 0U ||
      (width % BITMAP_BITS_PER_BYTE) != 0U) {
    return;
  }
  const size_t bytes_per_row = width / BITMAP_BITS_PER_BYTE;
  size_t required = 0U;
  if (ckd_mul(&required, bytes_per_row, height)) {
    return;
  }
  (void)bitmap_flip_aligned(bitmap, required, width, height);
}

void flip_bitmap_unaligned(uint8_t* bitmap, size_t width, size_t height) {
  if (bitmap == nullptr || width == 0U || height == 0U) {
    return;
  }
  const size_t bytes_per_row = compute_active_bytes(width);
  size_t required = 0U;
  if (ckd_mul(&required, bytes_per_row, height)) {
    return;
  }
  (void)bitmap_flip_unaligned(bitmap, required, width, height);
}

void flip_bitmap_unaligned_optimized(uint8_t* bitmap, size_t width,
                                     size_t height) {
  if (bitmap == nullptr || width == 0U || height == 0U) {
    return;
  }
  const size_t bytes_per_row = compute_active_bytes(width);
  size_t required = 0U;
  if (ckd_mul(&required, bytes_per_row, height)) {
    return;
  }
  (void)bitmap_flip_unaligned_candidate(bitmap, required, width, height);
}

void flip_bitmap_stride(uint8_t* bitmap, size_t width, size_t height,
                        size_t stride_bytes) {
  if (bitmap == nullptr || width == 0U || height == 0U) {
    return;
  }
  const size_t bytes_per_row = compute_active_bytes(width);
  if (stride_bytes < bytes_per_row) {
    return;
  }
  size_t required = 0U;
  if (ckd_mul(&required, stride_bytes, height)) {
    return;
  }
  (void)bitmap_flip_strided(bitmap, required, width, height, stride_bytes);
}

void flip_bitmap_roi(uint8_t* bitmap, size_t frame_width, size_t frame_height,
                     size_t stride_bytes, size_t roi_x, size_t roi_y,
                     size_t roi_w, size_t roi_h) {
  if (bitmap == nullptr || frame_width == 0U || frame_height == 0U ||
      roi_w == 0U || roi_h == 0U) {
    return;
  }
  const size_t bytes_per_row = compute_active_bytes(frame_width);
  if (stride_bytes < bytes_per_row) {
    return;
  }
  size_t required = 0U;
  if (ckd_mul(&required, stride_bytes, frame_height)) {
    return;
  }
  const struct bitmap_roi roi = {
      .x = roi_x,
      .y = roi_y,
      .width = roi_w,
      .height = roi_h,
  };
  (void)bitmap_flip_roi(bitmap, required, frame_width, frame_height,
                        stride_bytes, roi);
}
