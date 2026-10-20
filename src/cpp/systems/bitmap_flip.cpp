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
    std::size_t i = 0;
    for (; i + kBytesPerWord64 <= half_bytes; i += kBytesPerWord64) {
      std::uint8_t* const left_ptr = row_ptr + i;
      std::uint8_t* const right_ptr = row_ptr + num_bytes - kBytesPerWord64 - i;
      const std::uint64_t left_word = load_u64_le(left_ptr);
      const std::uint64_t right_word = load_u64_le(right_ptr);
      store_u64_le(left_ptr, reverse_bits_u64(right_word));
      store_u64_le(right_ptr, reverse_bits_u64(left_word));
    }
    for (; i < half_bytes; ++i) {
      const std::size_t right_idx = num_bytes - 1 - i;
      const std::uint8_t left_byte = row_ptr[i];
      const std::uint8_t right_byte = row_ptr[right_idx];
      row_ptr[i] = reverse_bits_lookup(right_byte);
      row_ptr[right_idx] = reverse_bits_lookup(left_byte);
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
  static_cast<void>(bitmap);
  static_cast<void>(width);
  static_cast<void>(height);
  static_cast<void>(stride_bytes);
  return std::unexpected(BitmapFlipError::kNotImplemented);
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
  static_cast<void>(bitmap);
  static_cast<void>(framebuffer_width);
  static_cast<void>(framebuffer_height);
  static_cast<void>(stride_bytes);
  static_cast<void>(roi);
  return std::unexpected(BitmapFlipError::kNotImplemented);
}

// Assume width is a multiple of 8.
void flip_bitmap(std::uint8_t* bitmap, std::size_t width,
                 std::size_t height) noexcept {
  if (bitmap == nullptr) {
    return;
  }
  if (width == 0 || height == 0 || (width % kBitsPerByte) != 0) {
    return;
  }
  const std::size_t bytes_per_row = width / kBitsPerByte;
  for (std::size_t row = 0; row < height; ++row) {
    std::uint8_t* start = bitmap + (row * bytes_per_row);
    std::uint8_t* end = start + bytes_per_row - 1;
    for (std::size_t i = 0; i < bytes_per_row / 2; ++i) {
      auto* left = start + i;
      auto* right = end - i;
      swap_bytes(left, right);
      *left = reverse_bits(*left);
      *right = reverse_bits(*right);
    }
    if (bytes_per_row % 2 == 1) {
      *(start + (bytes_per_row / 2)) =
          reverse_bits(*(start + (bytes_per_row / 2)));
    }
  }
}

// The stub never writes through `bitmap`; the implementation will.
// NOLINTNEXTLINE(readability-non-const-parameter)
void flip_bitmap_unaligned(std::uint8_t* bitmap, std::size_t width,
                           std::size_t height) noexcept {
  // TODO(BMFLP-REQ-012):
  //   1. Return immediately if bitmap is null or width or height is zero.
  //   2. Otherwise build a span of ceil(width / 8) * height bytes and call
  //      flip_bitmap_unaligned(span, width, height) (ignore the result
  //      explicitly).
  if (bitmap == nullptr) {
    return;
  }
  if (width == 0 || height == 0) {
    return;
  }
  const std::size_t bytes_per_row =
      (width / kBitsPerByte) + ((width % kBitsPerByte != 0) ? 1U : 0U);
  std::span<std::uint8_t> bitmap_span(bitmap, bytes_per_row * height);
  (void)flip_bitmap_unaligned(bitmap_span, width, height);
}

// NOLINTNEXTLINE(readability-non-const-parameter)
void flip_bitmap_unaligned_optimized(std::uint8_t* bitmap, std::size_t width,
                                     std::size_t height) noexcept {
  if (bitmap == nullptr) {
    return;
  }
  if (width == 0 || height == 0) {
    return;
  }
  const std::size_t bytes_per_row =
      (width / kBitsPerByte) + ((width % kBitsPerByte != 0) ? 1U : 0U);
  std::span<std::uint8_t> bitmap_span(bitmap, bytes_per_row * height);
  (void)flip_bitmap_unaligned_optimized(bitmap_span, width, height);
}

}  // namespace systems
