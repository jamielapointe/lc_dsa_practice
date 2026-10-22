#include "systems/bitmap_flip.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "systems/bitmap_utils.hpp"

// Complexity (BMFLP-REQ-006): TODO - write the time and space complexity of
// your flip_bitmap() here, for Problem 0 and again after Follow-up 1.
//
// Follow-up 1 notes (BMFLP-REQ-007): TODO - what did you optimize, and why is
// it faster on the target you have in mind?
//
// Follow-up 4 answers (BMFLP-REQ-014): TODO - see the questions in
// docs/systems/bitmap_flip.md, section 7.

namespace systems {

namespace {

inline constexpr std::size_t kBytesPerWord64 = sizeof(std::uint64_t);

[[nodiscard]] constexpr auto load_u64_le(const std::uint8_t* ptr) noexcept
    -> std::uint64_t {
  return (static_cast<std::uint64_t>(ptr[0]) << 0U) |
         (static_cast<std::uint64_t>(ptr[1]) << 8U) |
         (static_cast<std::uint64_t>(ptr[2]) << 16U) |
         (static_cast<std::uint64_t>(ptr[3]) << 24U) |
         (static_cast<std::uint64_t>(ptr[4]) << 32U) |
         (static_cast<std::uint64_t>(ptr[5]) << 40U) |
         (static_cast<std::uint64_t>(ptr[6]) << 48U) |
         (static_cast<std::uint64_t>(ptr[7]) << 56U);
}

constexpr void store_u64_le(std::uint8_t* ptr, std::uint64_t val) noexcept {
  ptr[0] = static_cast<std::uint8_t>(val >> 0U);
  ptr[1] = static_cast<std::uint8_t>(val >> 8U);
  ptr[2] = static_cast<std::uint8_t>(val >> 16U);
  ptr[3] = static_cast<std::uint8_t>(val >> 24U);
  ptr[4] = static_cast<std::uint8_t>(val >> 32U);
  ptr[5] = static_cast<std::uint8_t>(val >> 40U);
  ptr[6] = static_cast<std::uint8_t>(val >> 48U);
  ptr[7] = static_cast<std::uint8_t>(val >> 56U);
}

[[nodiscard]] constexpr auto reverse_bits_u64(std::uint64_t x) noexcept
    -> std::uint64_t {
  x = ((x & 0x5555555555555555ULL) << 1U) | ((x >> 1U) & 0x5555555555555555ULL);
  x = ((x & 0x3333333333333333ULL) << 2U) | ((x >> 2U) & 0x3333333333333333ULL);
  x = ((x & 0x0F0F0F0F0F0F0F0FULL) << 4U) | ((x >> 4U) & 0x0F0F0F0F0F0F0F0FULL);
  return std::byteswap(x);
}

}  // namespace

using internal::reverse_bits;
using internal::reverse_bits_lookup;
using internal::swap_bytes;

auto flip_bitmap(std::span<std::uint8_t> bitmap, std::size_t width,
                 std::size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  // Validate in order, before touching the buffer (BMFLP-REQ-022):
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-003) width % 8 != 0 -> kInvalidWidth.
  //   4. (BMFLP-REQ-021) size < (width / 8) * height (overflow-safe)
  //      -> kBufferTooSmall.
  if (bitmap.empty()) {
    return std::unexpected(BitmapFlipError::kNullPointer);
  }
  if (width == 0 || height == 0) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  if ((width % kBitsPerByte) != 0) {
    return std::unexpected(BitmapFlipError::kInvalidWidth);
  }
  const std::size_t num_bytes = width / kBitsPerByte;
  if (bitmap.size() / height < num_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  for (std::size_t row = 0; row < height; ++row) {
    const std::size_t start_row = row * num_bytes;
    const std::size_t end_row = start_row + num_bytes - 1;
    for (std::size_t i = 0; i < num_bytes / 2; ++i) {
      const std::size_t left = start_row + i;
      const std::size_t right = end_row - i;
      swap_bytes(&bitmap[left], &bitmap[right]);
      bitmap[left] = reverse_bits(bitmap[left]);
      bitmap[right] = reverse_bits(bitmap[right]);
    }
    if (num_bytes % 2 == 1) {
      bitmap[start_row + (num_bytes / 2)] =
          reverse_bits(bitmap[start_row + (num_bytes / 2)]);
    }
  }
  return {};
}

auto flip_bitmap_unaligned(std::span<std::uint8_t> bitmap, std::size_t width,
                           std::size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  // TODO(BMFLP-REQ-022): validate in order, before touching the buffer:
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-021) size < ceil(width / 8) * height (overflow-safe)
  //      -> kBufferTooSmall.
  // TODO(BMFLP-REQ-008): 4. Support any width > 0 (including width % 8 != 0).
  // TODO(BMFLP-REQ-009): 5. Ignore input padding bits; write them as zero.
  // TODO(BMFLP-REQ-015): 6. No heap, no recursion.
  // Hint: once flip_bitmap_stride() exists this can delegate to it.
  if (bitmap.empty()) {
    return std::unexpected(BitmapFlipError::kNullPointer);
  }
  if (width == 0 || height == 0) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  // this is equivalent to num_bytes = ceil(width / 8) without overflow
  const std::size_t num_bytes =
      (width / kBitsPerByte) + ((width % kBitsPerByte != 0) ? 1U : 0U);
  if (bitmap.size() / height < num_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  const std::size_t remainder = width % kBitsPerByte;
  for (std::size_t row = 0; row < height; ++row) {
    // First do the swap as if width was a multiple of 8
    const std::size_t start_row = row * num_bytes;
    const std::size_t end_row = start_row + num_bytes - 1;
    for (std::size_t i = 0; i < num_bytes / 2; ++i) {
      const std::size_t left = start_row + i;
      const std::size_t right = end_row - i;
      swap_bytes(&bitmap[left], &bitmap[right]);
      bitmap[left] = reverse_bits_lookup(bitmap[left]);
      bitmap[right] = reverse_bits_lookup(bitmap[right]);
    }
    if (num_bytes % 2 == 1) {
      const std::size_t mid_index = start_row + (num_bytes / 2);
      bitmap[mid_index] = reverse_bits_lookup(bitmap[mid_index]);
    }
    // If width is not a multiple of 8, the 8-remainder unused padding bits
    // that were originally in the MSBs of the last byte are now sitting in
    // the LSBs of the first byte.
    // Shift all bits across the row right by `shift = 87 - remainder` so
    // pixel `width - 1` lands at bit 0 (LSB) and pad bits with 0
    if (remainder > 0) {
      const std::size_t shift = kBitsPerByte - remainder;
      for (std::size_t i = 0; i < num_bytes - 1; ++i) {
        bitmap[start_row + i] =
            static_cast<std::uint8_t>((bitmap[start_row + i] >> shift) |
                                      (bitmap[start_row + i + 1] << remainder));
      }
      // Need to shift the last byte, which will automatically fill the MSB with
      // zeroes.
      bitmap[start_row + num_bytes - 1] =
          bitmap[start_row + num_bytes - 1] >> shift;
    }
  }

  return {};
}

auto flip_bitmap_unaligned_optimized(std::span<std::uint8_t> bitmap,
                                     std::size_t width,
                                     std::size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  if (bitmap.empty()) {
    return std::unexpected(BitmapFlipError::kNullPointer);
  }
  if (width == 0 || height == 0) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  // this is equivalent to num_bytes = ceil(width / 8) without overflow
  const std::size_t num_bytes =
      (width / kBitsPerByte) + ((width % kBitsPerByte != 0) ? 1U : 0U);
  if (bitmap.size() / height < num_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  const std::size_t remainder = width % kBitsPerByte;
  std::uint8_t* const base_ptr = bitmap.data();
  const std::size_t half_bytes = num_bytes / 2;
  for (std::size_t row = 0; row < height; ++row) {
    std::uint8_t* const row_ptr = base_ptr + (row * num_bytes);
    std::uint8_t* left_ptr = row_ptr;
    std::uint8_t* right_ptr = row_ptr + num_bytes;
    std::size_t rem_half = half_bytes;
    constexpr std::size_t kBytesPerQuadWord = 4U * kBytesPerWord64;
    while (rem_half >= kBytesPerQuadWord) {
      right_ptr -= kBytesPerQuadWord;
      const std::uint64_t left_w0 = load_u64_le(left_ptr);
      const std::uint64_t left_w1 = load_u64_le(left_ptr + kBytesPerWord64);
      const std::uint64_t left_w2 =
          load_u64_le(left_ptr + (2U * kBytesPerWord64));
      const std::uint64_t left_w3 =
          load_u64_le(left_ptr + (3U * kBytesPerWord64));
      const std::uint64_t right_w0 = load_u64_le(right_ptr);
      const std::uint64_t right_w1 = load_u64_le(right_ptr + kBytesPerWord64);
      const std::uint64_t right_w2 =
          load_u64_le(right_ptr + (2U * kBytesPerWord64));
      const std::uint64_t right_w3 =
          load_u64_le(right_ptr + (3U * kBytesPerWord64));
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
    constexpr std::size_t kBytesPerDualWord = 2U * kBytesPerWord64;
    if (rem_half >= kBytesPerDualWord) {
      right_ptr -= kBytesPerDualWord;
      const std::uint64_t left_w0 = load_u64_le(left_ptr);
      const std::uint64_t left_w1 = load_u64_le(left_ptr + kBytesPerWord64);
      const std::uint64_t right_w0 = load_u64_le(right_ptr);
      const std::uint64_t right_w1 = load_u64_le(right_ptr + kBytesPerWord64);
      store_u64_le(left_ptr, reverse_bits_u64(right_w1));
      store_u64_le(left_ptr + kBytesPerWord64, reverse_bits_u64(right_w0));
      store_u64_le(right_ptr, reverse_bits_u64(left_w1));
      store_u64_le(right_ptr + kBytesPerWord64, reverse_bits_u64(left_w0));
      left_ptr += kBytesPerDualWord;
      rem_half -= kBytesPerDualWord;
    }
    if (rem_half >= kBytesPerWord64) {
      right_ptr -= kBytesPerWord64;
      const std::uint64_t left_word = load_u64_le(left_ptr);
      const std::uint64_t right_word = load_u64_le(right_ptr);
      store_u64_le(left_ptr, reverse_bits_u64(right_word));
      store_u64_le(right_ptr, reverse_bits_u64(left_word));
      left_ptr += kBytesPerWord64;
      rem_half -= kBytesPerWord64;
    }
    while (rem_half > 0) {
      --right_ptr;
      const std::uint8_t left_byte = *left_ptr;
      const std::uint8_t right_byte = *right_ptr;
      *left_ptr = reverse_bits_lookup(right_byte);
      *right_ptr = reverse_bits_lookup(left_byte);
      ++left_ptr;
      --rem_half;
    }
    if (num_bytes % 2 == 1) {
      row_ptr[half_bytes] = reverse_bits_lookup(row_ptr[half_bytes]);
    }
    // If width is not a multiple of 8, the 8-remainder unused padding bits
    // that were originally in the MSBs of the last byte are now sitting in
    // the LSBs of the first byte.
    // Shift all bits across the row right by `shift = 8 - remainder` so
    // pixel `width - 1` lands at bit 0 (LSB) and pad bits with 0
    if (remainder > 0) {
      const std::size_t shift = kBitsPerByte - remainder;
      for (std::size_t col = 0; col < num_bytes - 1; ++col) {
        row_ptr[col] = static_cast<std::uint8_t>(
            (row_ptr[col] >> shift) | (row_ptr[col + 1] << remainder));
      }
      // Need to shift the last byte, which will automatically fill the MSB with
      // zeroes.
      row_ptr[num_bytes - 1] =
          static_cast<std::uint8_t>(row_ptr[num_bytes - 1] >> shift);
    }
  }

  return {};
}

auto flip_bitmap_stride(std::span<std::uint8_t> bitmap, std::size_t width,
                        std::size_t height, std::size_t stride_bytes) noexcept
    -> std::expected<void, BitmapFlipError> {
  // TODO(BMFLP-REQ-022): validate in order, before touching the buffer:
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-019) stride_bytes < ceil(width / 8) -> kInvalidStride.
  //   4. (BMFLP-REQ-021) size < stride_bytes * height (overflow-safe)
  //      -> kBufferTooSmall.
  // TODO(BMFLP-REQ-010): 5. Row y starts at y * stride_bytes; flip its
  //   ceil(width / 8) pixel bytes, never write the stride padding bytes.
  // Recall that stride_bytes >= ceil(width / 8) and is the distance in bytes
  // between the start of consecutive rows.
  if (bitmap.empty()) {
    return std::unexpected(BitmapFlipError::kNullPointer);
  }
  if (width == 0 || height == 0) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  const std::size_t num_bytes_per_row =
      (width / kBitsPerByte) + ((width % kBitsPerByte != 0) ? 1U : 0U);
  if (stride_bytes < num_bytes_per_row) {
    return std::unexpected(BitmapFlipError::kInvalidStride);
  }
  if (bitmap.size() / height < stride_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  const std::size_t remainder = width % kBitsPerByte;

  // BMFLP-REQ-009: shall write every padding bit as zero.
  for (std::size_t row = 0; row < height; ++row) {
    const std::size_t start_row = row * stride_bytes;
    const std::size_t end_row = start_row + num_bytes_per_row - 1;
    for (std::size_t i = 0; i < num_bytes_per_row / 2; ++i) {
      const std::size_t left = start_row + i;
      const std::size_t right = end_row - i;
      swap_bytes(&bitmap[left], &bitmap[right]);
      bitmap[left] = reverse_bits_lookup(bitmap[left]);
      bitmap[right] = reverse_bits_lookup(bitmap[right]);
    }
    if (num_bytes_per_row % 2 == 1) {
      const std::size_t mid_index = start_row + (num_bytes_per_row / 2);
      bitmap[mid_index] = reverse_bits_lookup(bitmap[mid_index]);
    }
    if (remainder > 0) {
      const std::size_t shift = kBitsPerByte - remainder;
      for (std::size_t i = 0; i < num_bytes_per_row - 1; ++i) {
        bitmap[start_row + i] =
            static_cast<std::uint8_t>((bitmap[start_row + i] >> shift) |
                                      (bitmap[start_row + i + 1] << remainder));
      }
      bitmap[end_row] = bitmap[end_row] >> shift;
    }
  }
  return {};
}

auto flip_bitmap_roi(std::span<std::uint8_t> bitmap,
                     std::size_t framebuffer_width,
                     std::size_t framebuffer_height, std::size_t stride_bytes,
                     BitmapRoi roi) noexcept
    -> std::expected<void, BitmapFlipError> {
  // TODO(BMFLP-REQ-022): validate in order, before touching the buffer:
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) any of framebuffer_width, framebuffer_height,
  //      roi.width, roi.height is zero -> kZeroDimension.
  //   3. (BMFLP-REQ-019) stride_bytes < ceil(framebuffer_width / 8)
  //      -> kInvalidStride.
  //   4. (BMFLP-REQ-020) ROI not inside the framebuffer (overflow-safe)
  //      -> kInvalidRoi.
  //   5. (BMFLP-REQ-021) size < stride_bytes * framebuffer_height
  //      (overflow-safe) -> kBufferTooSmall.
  // TODO(BMFLP-REQ-011): 6. For each ROI row, mirror pixels
  //   [roi.x, roi.x + roi.width) and preserve every other bit, including
  //   the partial bytes at both ROI edges.
  if (bitmap.empty()) {
    return std::unexpected(BitmapFlipError::kNullPointer);
  }
  if (framebuffer_width == 0 || framebuffer_height == 0 || roi.width == 0 ||
      roi.height == 0) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  const std::size_t num_bytes_per_row =
      (framebuffer_width / kBitsPerByte) +
      ((framebuffer_width % kBitsPerByte != 0) ? 1U : 0U);
  if (stride_bytes < num_bytes_per_row) {
    return std::unexpected(BitmapFlipError::kInvalidStride);
  }
  if (roi.x > framebuffer_width || roi.width > framebuffer_width - roi.x) {
    return std::unexpected(BitmapFlipError::kInvalidRoi);
  }
  if (roi.y > framebuffer_height || roi.height > framebuffer_height - roi.y) {
    return std::unexpected(BitmapFlipError::kInvalidRoi);
  }
  if (bitmap.size() / framebuffer_height < stride_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }

  // General unaligned-start case (`roi.x % kBitsPerByte != 0`):
  // Swap symmetric pixel paris `(left_x, y)` and `(right_x, y)` within the ROI
  // so bits outside of the ROI in the boundary bytes are left untouched.
  for (std::size_t row_offset = 0; row_offset < roi.height; ++row_offset) {
    std::size_t start_row = (roi.y + row_offset) * stride_bytes;
    for (size_t dx = 0; dx < roi.width / 2; ++dx) {
      const std::size_t left_x = roi.x + dx;
      const std::size_t right_x = roi.x + roi.width - dx - 1;

      const std::size_t left_byte_index = start_row + (left_x / kBitsPerByte);
      const std::size_t left_bit_index = left_x % kBitsPerByte;

      const std::size_t right_byte_index = start_row + (right_x / kBitsPerByte);
      const std::size_t right_bit_index = right_x % kBitsPerByte;

      const std::uint8_t left_val =
          (bitmap[left_byte_index] >> left_bit_index) & 1U;
      const std::uint8_t right_val =
          (bitmap[right_byte_index] >> right_bit_index) & 1U;

      if (right_val != 0) {
        bitmap[left_byte_index] |=
            static_cast<std::uint8_t>(1U << left_bit_index);
      } else {
        bitmap[left_byte_index] &=
            static_cast<std::uint8_t>(~(1U << left_bit_index));
      }

      if (left_val != 0) {
        bitmap[right_byte_index] |=
            static_cast<std::uint8_t>(1U << right_bit_index);
      } else {
        bitmap[right_byte_index] &=
            static_cast<std::uint8_t>(~(1U << right_bit_index));
      }
    }
  }
  return {};
}

}  // namespace systems
