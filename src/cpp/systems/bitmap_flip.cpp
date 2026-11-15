#include "systems/bitmap_flip.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include "systems/bitmap_utils.hpp"

// Complexity (BMFLP-REQ-006):
//   - Problem 0 (flip_bitmap): O(width * height) time — visits each of the
//     (width / 8) * height bytes once to swap symmetric pairs and reverse bits;
//     O(1) auxiliary space — reverses rows in place without heap allocation.
//   - Follow-up 1 (flip_bitmap_unaligned_optimized): O(width * height) time —
//     swaps/reverses ceil(width / 8) bytes per row in 64-bit chunks plus one
//     O(ceil(width / 8)) byte-shift pass when width % 8 != 0; O(1) space.
//
// Follow-up 1 notes (BMFLP-REQ-007):
//   flip_bitmap_unaligned_optimized replaces single-byte swaps with unrolled
//   32B (4x uint64_t), 16B (2x uint64_t), and 8B (uint64_t) span transfers
//   using SWAR bit-reversal (reverse_bits_u64 + std::byteswap) before a
//   <8-byte tail. Eliminating per-pixel work and widening memory accesses cuts
//   loop overhead and table-lookup serialization on 64-bit CPUs (x86-64,
//   AArch64, RISC-V).
//
// Follow-up 4 answers (BMFLP-REQ-014):
//   1. Multi-core partitioning: split by disjoint row intervals [r0, r1);
//      unit of work = complete rows (stride_bytes). Threads must never share
//      bytes (partial-byte ROI edges) or cache lines (platform::kCacheLineSize)
//      to avoid data races and false sharing.
//   2. Small framebuffers (a few KB): thread synchronization, wakeup latency,
//      and cache-line bouncing (microseconds) exceed a single-core memory pass
//      (< 1 us), so multi-threading slows execution down.
//   3. Mid-scan tearing: flipping in place while the display scans out shows
//      half old / half flipped frames. Avoid via double/triple buffering with
//      VSync page-flip pointer swaps or VBlank-synchronized dirty regions.
//   4. Non-coherent DMA + CPU D-cache: clean/flush D-cache lines for the buffer
//      with a data sync barrier before triggering DMA reads so RAM has the new
//      pixels (and invalidate before CPU reads after DMA writes); otherwise DMA
//      reads stale RAM bytes. In non-cacheable / write-combining memory, no
//      D-cache flush is needed—only a store/write barrier before starting DMA.

namespace systems {

using internal::reverse_bits;
using internal::reverse_bits_lookup;
using internal::swap_bytes;

namespace {

inline constexpr size_t kBytesPerWord64 = sizeof(uint64_t);

[[nodiscard]] constexpr auto load_u64_le(std::span<const uint8_t> bytes,
                                         size_t offset) noexcept -> uint64_t {
  return (static_cast<uint64_t>(bytes[offset + 0uz]) << 0U) |
         (static_cast<uint64_t>(bytes[offset + 1uz]) << 8U) |
         (static_cast<uint64_t>(bytes[offset + 2uz]) << 16U) |
         (static_cast<uint64_t>(bytes[offset + 3uz]) << 24U) |
         (static_cast<uint64_t>(bytes[offset + 4uz]) << 32U) |
         (static_cast<uint64_t>(bytes[offset + 5uz]) << 40U) |
         (static_cast<uint64_t>(bytes[offset + 6uz]) << 48U) |
         (static_cast<uint64_t>(bytes[offset + 7uz]) << 56U);
}

constexpr void store_u64_le(std::span<uint8_t> bytes, size_t offset,
                            uint64_t val) noexcept {
  bytes[offset + 0uz] = static_cast<uint8_t>(val >> 0U);
  bytes[offset + 1uz] = static_cast<uint8_t>(val >> 8U);
  bytes[offset + 2uz] = static_cast<uint8_t>(val >> 16U);
  bytes[offset + 3uz] = static_cast<uint8_t>(val >> 24U);
  bytes[offset + 4uz] = static_cast<uint8_t>(val >> 32U);
  bytes[offset + 5uz] = static_cast<uint8_t>(val >> 40U);
  bytes[offset + 6uz] = static_cast<uint8_t>(val >> 48U);
  bytes[offset + 7uz] = static_cast<uint8_t>(val >> 56U);
}

[[nodiscard]] constexpr auto reverse_bits_u64(uint64_t x) noexcept -> uint64_t {
  x = ((x & 0x5555555555555555ULL) << 1U) | ((x >> 1U) & 0x5555555555555555ULL);
  x = ((x & 0x3333333333333333ULL) << 2U) | ((x >> 2U) & 0x3333333333333333ULL);
  x = ((x & 0x0F0F0F0F0F0F0F0FULL) << 4U) | ((x >> 4U) & 0x0F0F0F0F0F0F0F0FULL);
  return std::byteswap(x);
}

// Overflow-safe ceil(width / 8).
[[nodiscard]] constexpr auto bytes_for_width(size_t width) noexcept -> size_t {
  return (width / kBitsPerByte) + ((width % kBitsPerByte != 0uz) ? 1uz : 0uz);
}

[[nodiscard]] constexpr auto validate_basic_bitmap(
    std::span<const uint8_t> bitmap, size_t width, size_t height) noexcept
    -> std::expected<size_t, BitmapFlipError> {
  if (bitmap.empty()) {
    return std::unexpected(BitmapFlipError::kNullPointer);
  }
  if (width == 0uz || height == 0uz) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  return bytes_for_width(width);
}

[[nodiscard]] constexpr auto validate_packed_bitmap(
    std::span<const uint8_t> bitmap, size_t width, size_t height) noexcept
    -> std::expected<size_t, BitmapFlipError> {
  const auto num_bytes = validate_basic_bitmap(bitmap, width, height);
  if (!num_bytes.has_value()) {
    return num_bytes;
  }
  if (bitmap.size() / height < *num_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  return *num_bytes;
}

[[nodiscard]] constexpr auto validate_strided_header(
    std::span<const uint8_t> bitmap, size_t width, size_t height,
    size_t stride_bytes, bool has_zero_roi = false) noexcept
    -> std::expected<size_t, BitmapFlipError> {
  const auto num_bytes = validate_basic_bitmap(bitmap, width, height);
  if (!num_bytes.has_value()) {
    return num_bytes;
  }
  if (has_zero_roi) {
    return std::unexpected(BitmapFlipError::kZeroDimension);
  }
  if (stride_bytes < *num_bytes) {
    return std::unexpected(BitmapFlipError::kInvalidStride);
  }
  return *num_bytes;
}

// If width is not a multiple of 8, the (8 - remainder) padding bits from the
// original last byte's MSBs sit in the reversed first byte's LSBs. Shift the
// row right by `8 - remainder` so pixel `width - 1` lands at bit 0 (LSB) and
// trailing padding bits become zero (BMFLP-REQ-009).
void shift_row_right_to_lsb(std::span<uint8_t> row_bytes,
                            size_t remainder) noexcept {
  if (remainder == 0uz) {
    return;
  }
  const size_t shift = kBitsPerByte - remainder;
  const size_t num_bytes = row_bytes.size();
  for (size_t i = 0uz; i < num_bytes - 1uz; ++i) {
    row_bytes[i] = static_cast<uint8_t>((row_bytes[i] >> shift) |
                                        (row_bytes[i + 1uz] << remainder));
  }
  row_bytes[num_bytes - 1uz] =
      static_cast<uint8_t>(row_bytes[num_bytes - 1uz] >> shift);
}

void flip_rows_lookup(std::span<uint8_t> bitmap, size_t width, size_t height,
                      size_t stride_bytes) noexcept {
  const size_t num_bytes_per_row = bytes_for_width(width);
  const size_t remainder = width % kBitsPerByte;
  for (size_t row = 0uz; row < height; ++row) {
    const auto row_bytes =
        bitmap.subspan(row * stride_bytes, num_bytes_per_row);
    for (size_t i = 0uz; i < num_bytes_per_row / 2uz; ++i) {
      const size_t right = num_bytes_per_row - 1uz - i;
      swap_bytes(&row_bytes[i], &row_bytes[right]);
      row_bytes[i] = reverse_bits_lookup(row_bytes[i]);
      row_bytes[right] = reverse_bits_lookup(row_bytes[right]);
    }
    if (num_bytes_per_row % 2uz == 1uz) {
      const size_t mid_index = num_bytes_per_row / 2uz;
      row_bytes[mid_index] = reverse_bits_lookup(row_bytes[mid_index]);
    }
    shift_row_right_to_lsb(row_bytes, remainder);
  }
}

void flip_row_unaligned_optimized(std::span<uint8_t> row_bytes,
                                  size_t remainder) noexcept {
  const size_t num_bytes = row_bytes.size();
  const size_t half_bytes = num_bytes / 2uz;
  size_t left_idx = 0uz;
  size_t right_idx = num_bytes;
  size_t rem_half = half_bytes;
  constexpr size_t kBytesPerQuadWord = 4uz * kBytesPerWord64;
  while (rem_half >= kBytesPerQuadWord) {
    right_idx -= kBytesPerQuadWord;
    const uint64_t left_w0 = load_u64_le(row_bytes, left_idx);
    const uint64_t left_w1 = load_u64_le(row_bytes, left_idx + kBytesPerWord64);
    const uint64_t left_w2 =
        load_u64_le(row_bytes, left_idx + (2uz * kBytesPerWord64));
    const uint64_t left_w3 =
        load_u64_le(row_bytes, left_idx + (3uz * kBytesPerWord64));
    const uint64_t right_w0 = load_u64_le(row_bytes, right_idx);
    const uint64_t right_w1 =
        load_u64_le(row_bytes, right_idx + kBytesPerWord64);
    const uint64_t right_w2 =
        load_u64_le(row_bytes, right_idx + (2uz * kBytesPerWord64));
    const uint64_t right_w3 =
        load_u64_le(row_bytes, right_idx + (3uz * kBytesPerWord64));
    store_u64_le(row_bytes, left_idx, reverse_bits_u64(right_w3));
    store_u64_le(row_bytes, left_idx + kBytesPerWord64,
                 reverse_bits_u64(right_w2));
    store_u64_le(row_bytes, left_idx + (2uz * kBytesPerWord64),
                 reverse_bits_u64(right_w1));
    store_u64_le(row_bytes, left_idx + (3uz * kBytesPerWord64),
                 reverse_bits_u64(right_w0));
    store_u64_le(row_bytes, right_idx, reverse_bits_u64(left_w3));
    store_u64_le(row_bytes, right_idx + kBytesPerWord64,
                 reverse_bits_u64(left_w2));
    store_u64_le(row_bytes, right_idx + (2uz * kBytesPerWord64),
                 reverse_bits_u64(left_w1));
    store_u64_le(row_bytes, right_idx + (3uz * kBytesPerWord64),
                 reverse_bits_u64(left_w0));
    left_idx += kBytesPerQuadWord;
    rem_half -= kBytesPerQuadWord;
  }
  constexpr size_t kBytesPerDualWord = 2uz * kBytesPerWord64;
  if (rem_half >= kBytesPerDualWord) {
    right_idx -= kBytesPerDualWord;
    const uint64_t left_w0 = load_u64_le(row_bytes, left_idx);
    const uint64_t left_w1 = load_u64_le(row_bytes, left_idx + kBytesPerWord64);
    const uint64_t right_w0 = load_u64_le(row_bytes, right_idx);
    const uint64_t right_w1 =
        load_u64_le(row_bytes, right_idx + kBytesPerWord64);
    store_u64_le(row_bytes, left_idx, reverse_bits_u64(right_w1));
    store_u64_le(row_bytes, left_idx + kBytesPerWord64,
                 reverse_bits_u64(right_w0));
    store_u64_le(row_bytes, right_idx, reverse_bits_u64(left_w1));
    store_u64_le(row_bytes, right_idx + kBytesPerWord64,
                 reverse_bits_u64(left_w0));
    left_idx += kBytesPerDualWord;
    rem_half -= kBytesPerDualWord;
  }
  if (rem_half >= kBytesPerWord64) {
    right_idx -= kBytesPerWord64;
    const uint64_t left_word = load_u64_le(row_bytes, left_idx);
    const uint64_t right_word = load_u64_le(row_bytes, right_idx);
    store_u64_le(row_bytes, left_idx, reverse_bits_u64(right_word));
    store_u64_le(row_bytes, right_idx, reverse_bits_u64(left_word));
    left_idx += kBytesPerWord64;
    rem_half -= kBytesPerWord64;
  }
  while (rem_half > 0uz) {
    --right_idx;
    const uint8_t left_byte = row_bytes[left_idx];
    const uint8_t right_byte = row_bytes[right_idx];
    row_bytes[left_idx] = reverse_bits_lookup(right_byte);
    row_bytes[right_idx] = reverse_bits_lookup(left_byte);
    ++left_idx;
    --rem_half;
  }
  if (num_bytes % 2uz == 1uz) {
    row_bytes[half_bytes] = reverse_bits_lookup(row_bytes[half_bytes]);
  }
  shift_row_right_to_lsb(row_bytes, remainder);
}

}  // namespace

auto flip_bitmap(std::span<uint8_t> bitmap, size_t width,
                 size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  // Validate in order, before touching the buffer (BMFLP-REQ-022):
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-003) width % 8 != 0 -> kInvalidWidth.
  //   4. (BMFLP-REQ-021) size < (width / 8) * height (overflow-safe)
  //      -> kBufferTooSmall.
  const auto num_bytes_or_err = validate_basic_bitmap(bitmap, width, height);
  if (!num_bytes_or_err.has_value()) {
    return std::unexpected(num_bytes_or_err.error());
  }
  if ((width % kBitsPerByte) != 0uz) {
    return std::unexpected(BitmapFlipError::kInvalidWidth);
  }
  const size_t num_bytes = *num_bytes_or_err;
  if (bitmap.size() / height < num_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  for (size_t row = 0uz; row < height; ++row) {
    const auto row_bytes = bitmap.subspan(row * num_bytes, num_bytes);
    for (size_t i = 0uz; i < num_bytes / 2uz; ++i) {
      const size_t right = num_bytes - 1uz - i;
      swap_bytes(&row_bytes[i], &row_bytes[right]);
      row_bytes[i] = reverse_bits(row_bytes[i]);
      row_bytes[right] = reverse_bits(row_bytes[right]);
    }
    if (num_bytes % 2uz == 1uz) {
      row_bytes[num_bytes / 2uz] = reverse_bits(row_bytes[num_bytes / 2uz]);
    }
  }
  return {};
}

auto flip_bitmap_unaligned(std::span<uint8_t> bitmap, size_t width,
                           size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  // Validate in order, before touching the buffer (BMFLP-REQ-022):
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-021) size < ceil(width / 8) * height (overflow-safe)
  //      -> kBufferTooSmall.
  const auto num_bytes = validate_packed_bitmap(bitmap, width, height);
  if (!num_bytes.has_value()) {
    return std::unexpected(num_bytes.error());
  }
  flip_rows_lookup(bitmap, width, height, *num_bytes);
  return {};
}

auto flip_bitmap_unaligned_optimized(std::span<uint8_t> bitmap, size_t width,
                                     size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  const auto num_bytes = validate_packed_bitmap(bitmap, width, height);
  if (!num_bytes.has_value()) {
    return std::unexpected(num_bytes.error());
  }
  const size_t remainder = width % kBitsPerByte;
  for (size_t row = 0uz; row < height; ++row) {
    flip_row_unaligned_optimized(bitmap.subspan(row * (*num_bytes), *num_bytes),
                                 remainder);
  }
  return {};
}

auto flip_bitmap_stride(std::span<uint8_t> bitmap, size_t width, size_t height,
                        size_t stride_bytes) noexcept
    -> std::expected<void, BitmapFlipError> {
  // Validate in order, before touching the buffer (BMFLP-REQ-022):
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-019) stride_bytes < ceil(width / 8) -> kInvalidStride.
  //   4. (BMFLP-REQ-021) size < stride_bytes * height (overflow-safe)
  //      -> kBufferTooSmall.
  // Each row y starts at y * stride_bytes; only its ceil(width / 8) pixel
  // bytes are written, leaving stride padding bytes untouched (BMFLP-REQ-010).
  const auto header =
      validate_strided_header(bitmap, width, height, stride_bytes);
  if (!header.has_value()) {
    return std::unexpected(header.error());
  }
  if (bitmap.size() / height < stride_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }
  flip_rows_lookup(bitmap, width, height, stride_bytes);
  return {};
}

auto flip_bitmap_roi(std::span<uint8_t> bitmap, size_t framebuffer_width,
                     size_t framebuffer_height, size_t stride_bytes,
                     BitmapRoi roi) noexcept
    -> std::expected<void, BitmapFlipError> {
  // Validate in order, before touching the buffer (BMFLP-REQ-022):
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) any of framebuffer_width, framebuffer_height,
  //      roi.width, roi.height is zero -> kZeroDimension.
  //   3. (BMFLP-REQ-019) stride_bytes < ceil(framebuffer_width / 8)
  //      -> kInvalidStride.
  //   4. (BMFLP-REQ-020) ROI not inside the framebuffer (overflow-safe)
  //      -> kInvalidRoi.
  //   5. (BMFLP-REQ-021) size < stride_bytes * framebuffer_height
  //      (overflow-safe) -> kBufferTooSmall.
  const auto header = validate_strided_header(
      bitmap, framebuffer_width, framebuffer_height, stride_bytes,
      roi.width == 0uz || roi.height == 0uz);
  if (!header.has_value()) {
    return std::unexpected(header.error());
  }
  if (roi.x > framebuffer_width || roi.width > framebuffer_width - roi.x ||
      roi.y > framebuffer_height || roi.height > framebuffer_height - roi.y) {
    return std::unexpected(BitmapFlipError::kInvalidRoi);
  }
  if (bitmap.size() / framebuffer_height < stride_bytes) {
    return std::unexpected(BitmapFlipError::kBufferTooSmall);
  }

  // General unaligned-start case (`roi.x % kBitsPerByte != 0`):
  // Swap symmetric pixel pairs `(left_x, y)` and `(right_x, y)` within the ROI
  // so bits outside of the ROI in the boundary bytes are left untouched.
  for (size_t row_offset = 0uz; row_offset < roi.height; ++row_offset) {
    const size_t start_row = (roi.y + row_offset) * stride_bytes;
    for (size_t dx = 0uz; dx < roi.width / 2uz; ++dx) {
      const size_t left_x = roi.x + dx;
      const size_t right_x = roi.x + roi.width - dx - 1uz;

      const size_t left_byte_index = start_row + (left_x / kBitsPerByte);
      const size_t left_bit_index = left_x % kBitsPerByte;

      const size_t right_byte_index = start_row + (right_x / kBitsPerByte);
      const size_t right_bit_index = right_x % kBitsPerByte;

      const uint8_t left_val = (bitmap[left_byte_index] >> left_bit_index) & 1U;
      const uint8_t right_val =
          (bitmap[right_byte_index] >> right_bit_index) & 1U;

      if (right_val != 0) {
        bitmap[left_byte_index] |= static_cast<uint8_t>(1U << left_bit_index);
      } else {
        bitmap[left_byte_index] &=
            static_cast<uint8_t>(~(1U << left_bit_index));
      }

      if (left_val != 0) {
        bitmap[right_byte_index] |= static_cast<uint8_t>(1U << right_bit_index);
      } else {
        bitmap[right_byte_index] &=
            static_cast<uint8_t>(~(1U << right_bit_index));
      }
    }
  }
  return {};
}

}  // namespace systems
