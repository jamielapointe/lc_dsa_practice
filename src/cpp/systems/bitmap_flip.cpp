#include "systems/bitmap_flip.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

// Complexity (BMFLP-REQ-006): TODO - write the time and space complexity of
// your flip_bitmap() here, for Problem 0 and again after Follow-up 1.
//
// Follow-up 1 notes (BMFLP-REQ-007): TODO - what did you optimize, and why is
// it faster on the target you have in mind?
//
// Follow-up 4 answers (BMFLP-REQ-014): TODO - see the questions in
// docs/systems/bitmap_flip.md, section 7.

namespace systems {

auto reverse_bits(std::uint8_t value) noexcept -> std::uint8_t {
  // TODO(BMFLP-REQ-002):
  //   1. Return `value` with bit i moved to bit 7 - i.
  //   2. Keep it reentrant: no table that is filled in lazily at run time.
  static_cast<void>(value);
  return 0;
}

auto flip_bitmap(std::span<std::uint8_t> bitmap, std::size_t width,
                 std::size_t height) noexcept
    -> std::expected<void, BitmapFlipError> {
  // TODO(BMFLP-REQ-022): validate in order, before touching the buffer:
  //   1. (BMFLP-REQ-017) empty span / null data -> kNullPointer.
  //   2. (BMFLP-REQ-018) width == 0 or height == 0 -> kZeroDimension.
  //   3. (BMFLP-REQ-021) size < ceil(width / 8) * height (overflow-safe)
  //      -> kBufferTooSmall.
  // TODO(BMFLP-REQ-003): 4. Flip one row of width % 8 == 0 in place.
  // TODO(BMFLP-REQ-004): 5. Apply it to every row.
  // TODO(BMFLP-REQ-008): 6. Support width % 8 != 0 (Follow-up 2).
  // TODO(BMFLP-REQ-009): 7. Ignore input padding bits; write them as zero.
  // TODO(BMFLP-REQ-015): 8. No heap, no recursion.
  // Hint: once flip_bitmap_stride() exists this can delegate to it.
  static_cast<void>(bitmap);
  static_cast<void>(width);
  static_cast<void>(height);
  return std::unexpected(BitmapFlipError::kNotImplemented);
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

// The stub never writes through `bitmap`; the implementation will.
// NOLINTNEXTLINE(readability-non-const-parameter)
void flip_bitmap(std::uint8_t* bitmap, std::size_t width,
                 std::size_t height) noexcept {
  // TODO(BMFLP-REQ-012):
  //   1. Return immediately if bitmap is null or width or height is zero.
  //   2. Otherwise build a span of ceil(width / 8) * height bytes and call
  //      the span overload (the result cannot be reported; ignore it
  //      explicitly).
  static_cast<void>(bitmap);
  static_cast<void>(width);
  static_cast<void>(height);
}

}  // namespace systems
