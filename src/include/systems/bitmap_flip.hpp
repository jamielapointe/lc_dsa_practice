#pragma once

/// @file bitmap_flip.hpp
/// @brief In-place horizontal flip (mirror) of 1-bit-per-pixel monochrome
/// bitmaps.
///
/// Problem
/// -------
/// Given a monochrome bitmap stored as a flat array of bytes with one bit per
/// pixel, flip it horizontally in place. Within each byte, bit 0 (LSB) is the
/// leftmost pixel of that byte and bit 7 (MSB) the rightmost. Requirements:
/// `docs/systems/bitmap_flip.md` (prefix `BMFLP`).
///
/// The exercise is staged; each stage has its own test suite so you can turn
/// them green one at a time (`ctest -R '^BitmapFlipTest'`, ...):
///
///   - Byte bit reversal, `reverse_bits()`: `BitmapFlipTest.ReverseBits*`.
///   - Problem 0 (`width % 8 == 0`), `flip_bitmap()`: `BitmapFlipTest`.
///   - Follow-up 1 (performance): same API (`flip_bitmap()`), verified by
///     review.
///   - Follow-up 2 (any width), `flip_bitmap_unaligned()`:
///     `BitmapFlipUnalignedTest`.
///   - Follow-up 3, `flip_bitmap_stride()`: `BitmapFlipStrideTest`;
///     `flip_bitmap_roi()`: `BitmapFlipRoiTest`.
///   - Input validation (all functions): `BitmapFlipErrorTest`.
///   - Reentrancy: `BitmapFlipConcurrencyTest`. Follow-up 4 itself is a
///     thought experiment (spec section 7), not code.
///
/// Memory layout
/// -------------
/// Row `y` starts at byte `y * stride_bytes`. A row holds
/// `bytes_per_row = ceil(width / 8)` pixel bytes; pixel `x` of that row is bit
/// `x % 8` of byte `x / 8`. For the packed API `stride_bytes == bytes_per_row`.
///
///   - *Padding bits*: when `width % 8 != 0`, the high `8 - width % 8` bits of
///     the last pixel byte of each row are not pixels. Their input value is
///     arbitrary (garbage) and must never leak into pixels.
///   - *Stride padding bytes*: bytes `[bytes_per_row, stride_bytes)` of each
///     row belong to the display controller / DMA engine and are never
///     written.
///
/// Design summary
/// --------------
///   - Flip definition: afterwards, pixel `x` of every row holds what pixel
///     `width - 1 - x` of the same row held before. Row order is unchanged.
///   - `flip_bitmap` requires `width % 8 == 0` (Problem 0 and Follow-up 1) and
///     rejects unaligned widths with `kInvalidWidth`.
///   - `flip_bitmap_unaligned` accepts any `width > 0` (Follow-up 2).
///   - Full-row flips (`flip_bitmap_unaligned`, `flip_bitmap_stride`) write the
///     padding bits of each row's last pixel byte as **zero** (BMFLP-REQ-009).
///   - ROI flips (`flip_bitmap_roi`) change only the pixels inside the ROI;
///     every other bit of the buffer, including padding bits, is preserved.
///   - Validation happens before any write: on error the buffer is untouched.
///     Order of checks (first failure wins): `kNullPointer`, `kZeroDimension`,
///     `kInvalidWidth` (`flip_bitmap` only), `kInvalidStride`, `kInvalidRoi`,
///     `kBufferTooSmall` (BMFLP-REQ-022).
///
/// Thread safety
/// -------------
/// Every function is reentrant: no hidden mutable state (no lazily built
/// tables, no statics written at run time). Concurrent calls on different
/// buffers, or on disjoint row ranges of one buffer, are data-race free.
/// Concurrent calls touching the same rows race (the caller synchronizes).
///
/// Blocking
/// --------
/// Nothing blocks, sleeps, or waits; run time is bounded by the image size.
///
/// Memory
/// ------
/// No heap, no recursion, O(1) auxiliary space (a few scalars or a fixed-size
/// table of constants). All work happens in the caller's buffer.
///
/// Error handling
/// --------------
/// Public operations are `noexcept` and report invalid input as
/// `std::expected<void, BitmapFlipError>`.
///
/// Portability
/// -----------
/// Pure portable C++23 (`<bit>`, fixed-width integers). No OS calls, no
/// compiler intrinsics required, no dependency on `systems/platform.hpp`.
///
/// Implementation outline (what to build, not how)
/// -----------------------------------------------
/// 1. `reverse_bits()`: mirror one byte. Several very different solutions
///    exist; think about which suits a small MCU vs a 64-bit core.
/// 2. Problem 0 (`flip_bitmap`): flip one byte-aligned row (`width % 8 == 0`)
///    in place, then apply it to every row. Write the time and space
///    complexity in a comment (BMFLP-REQ-006).
/// 3. Follow-up 1 (`flip_bitmap`): make it faster without changing the API
///    (BMFLP-REQ-007). Write down what you changed and why it is faster.
/// 4. Follow-up 2 (`flip_bitmap_unaligned`): support any `width > 0`
///    (including `width % 8 != 0`). A row no longer ends on a byte boundary,
///    so check where each mirrored pixel must land, and make sure padding
///    garbage cannot end up in a pixel.
/// 5. Follow-up 3: generalize the row addressing to a byte stride, then to a
///    rectangle whose left and right edges may fall in the middle of a byte.
/// 6. Validation: implement the checks in the documented order, guarding
///    every size computation against `std::size_t` overflow.
/// 7. Follow-up 4: write your answers to the questions in the spec (section
///    "Follow-up 4") as a comment in `bitmap_flip.cpp`.
///
/// Pitfalls
/// --------
///   - Swapping bytes without reversing their bits (or the reverse).
///   - The middle byte of a row with an odd byte count.
///   - Off-by-one alignment shifts for unaligned widths (1-pixel shear).
///   - Padding garbage leaking into pixels; reading or writing past the row.
///   - ROI edges inside a byte clobbering neighbouring pixels.
///   - `stride_bytes * height` or `roi.x + roi.width` overflowing.
///   - Implicit integer promotion: `~byte` and `byte << n` are `int`.
///
/// Examples
/// ---------
/// 1 - aligned width: width = 16, height = 1 (width_bytes = 2, remainder = 0,
/// shift = 0)
///
///    (MSB..LSB): [p7 p6 p5 p4 p3 p2 p1 p0], [p15 p14 p13 p12 p11 p10 p9 p8]
///    - swap bytes: [p15 p14 p13 p12 p11 p10 p9 p8], [p7 p6 p5 p4 p3 p2 p1 p0]
///    - reverse bits:
///       - [p7 p6 p5 p4 p3 p2 p1 p0] -> [p0 p1 p2 p3 p4 p5 p6 p7]
///       - [p15 p14 p13 p12 p11 p10 p9 p8] -> [p8 p9 p10 p11 p12 p13 p14 p15]
///    - result (MSB..LSB):
///        - [p8 p9 p10 p11 p12 p13 p14 p15], [p0 p1 p2 p3 p4 p5 p6 p7]
///
/// 2 - unaligned width: width = 10, height = 1 (width_bytes = 2, remainder = 2,
/// shift = 6)
///    (X = padding)
///    (MSB..LSB): [p7 p6 p5 p4 p3 p2 p1 p0],
///                [ X  X  X  X  X  X p9 p8]
///   - swap bytes: [ X  X  X  X  X  X p9 p8], [p7 p6 p5 p4 p3 p2 p1 p0]
///   - reverse bits:
///      - [p7 p6 p5 p4 p3 p2 p1 p0] -> [p0 p1 p2 p3 p4 p5 p6 p7]
///      - [ X  X  X  X  X  X p9 p8] -> [p8 p9  X  X  X  X  X  X]
///   - initial result (MSB..LSB):
///     - [p8 p9  X  X  X  X  X  X], [p0 p1 p2 p3 p4 p5 p6 p7]
///   Now we need to pack everything together and then add zeros for padding at
///   the LSB.
///   - Byte 0 = (Bytes 0 >> 6) | (Byte 1 << 2)
///            = [0 0 0 0 0 0 p8 p9] | [p2 p3 p4 p5 p6 p7 0 0]
///            = [p2 p3 p4 p5 p6 p7 p8 p9]
///   - Byte 1 = (Bytes 1 >> 6)
///            = [0 0 0 0 0 0 p0 p1]
///   - Final result (MSB..LSB): [p2 p3 p4 p5 p6 p7 p8 p9], [0 0 0 0 0 0 p0 p1]
///

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace systems {

/// @brief Number of bits (pixels) in one byte of a 1-bpp bitmap.
inline constexpr std::size_t kBitsPerByte = 8;

/// @brief Maximum number of distinct values for an 8-bit unsigned integer
/// (2^8).
inline constexpr std::size_t kMaxUint8Combinations = 256;

/// @brief Error codes returned by the span-based flip operations.
enum class [[nodiscard]] BitmapFlipError : std::uint8_t {
  /// Stub placeholder; never returned by a finished implementation.
  kNotImplemented = 0,
  /// The buffer span is empty or its data pointer is null.
  kNullPointer,
  /// Width is not a multiple of 8 (`flip_bitmap` only).
  kInvalidWidth,
  /// `width` or `height` (or the ROI's width or height) is zero.
  kZeroDimension,
  /// The buffer is smaller than `stride_bytes * height` bytes.
  kBufferTooSmall,
  /// `stride_bytes` is smaller than `ceil(width / 8)`.
  kInvalidStride,
  /// The ROI does not lie entirely inside the framebuffer.
  kInvalidRoi,
};

/// @brief Returns a short, stable description of a `BitmapFlipError`.
/// @param error Error code to describe.
/// @return Static string; never empty.
[[nodiscard]] constexpr auto to_string(BitmapFlipError error) noexcept
    -> std::string_view {
  switch (error) {
    case BitmapFlipError::kNotImplemented:
      return "not implemented";
    case BitmapFlipError::kNullPointer:
      return "null or empty buffer";
    case BitmapFlipError::kInvalidWidth:
      return "width is not a multiple of 8";
    case BitmapFlipError::kZeroDimension:
      return "zero width or height";
    case BitmapFlipError::kBufferTooSmall:
      return "buffer too small for stride * height";
    case BitmapFlipError::kInvalidStride:
      return "stride smaller than bytes per row";
    case BitmapFlipError::kInvalidRoi:
      return "roi outside framebuffer";
  }
  return "unknown BitmapFlipError";
}

/// @brief Rectangular region of interest, in pixels, inside a framebuffer.
///
/// Valid when `width > 0`, `height > 0`, `x + width <= framebuffer_width` and
/// `y + height <= framebuffer_height` (evaluated without overflow).
struct BitmapRoi {
  /// Leftmost pixel column of the ROI.
  std::size_t x{0};
  /// Topmost row of the ROI.
  std::size_t y{0};
  /// ROI width in pixels.
  std::size_t width{0};
  /// ROI height in rows.
  std::size_t height{0};
};
/// @brief Flips a tightly packed, byte-aligned (`width % 8 == 0`) bitmap
/// horizontally in place (Problem 0 and Follow-up 1).
///
/// Rows are `width / 8` bytes long with no gap between them.
/// @param bitmap Pixel buffer; at least `(width / 8) * height` bytes.
///   Bytes past that size are not accessed.
/// @param width Image width in pixels (must be a non-zero multiple of 8).
/// @param height Image height in rows (> 0).
/// @return Nothing on success; otherwise `kNullPointer`, `kZeroDimension`,
///   `kInvalidWidth`, or `kBufferTooSmall` (checked in that order, buffer
///   untouched).
/// @par Thread safety
/// Reentrant; see the file comment.
/// @par Requirements
/// BMFLP-REQ-001, BMFLP-REQ-003, BMFLP-REQ-004, BMFLP-REQ-005, BMFLP-REQ-017,
/// BMFLP-REQ-018, BMFLP-REQ-021, BMFLP-REQ-022
[[nodiscard]] auto flip_bitmap(std::span<std::uint8_t> bitmap,
                               std::size_t width, std::size_t height) noexcept
    -> std::expected<void, BitmapFlipError>;

/// @brief Flips a tightly packed bitmap of any width horizontally in place
/// (Follow-up 2).
///
/// Rows are `ceil(width / 8)` bytes long with no gap between them. Padding
/// bits of each row's last byte are written as zero.
/// @param bitmap Pixel buffer; at least `ceil(width / 8) * height` bytes.
///   Bytes past that size are not accessed.
/// @param width Image width in pixels (any value > 0, aligned or unaligned).
/// @param height Image height in rows (> 0).
/// @return Nothing on success; otherwise `kNullPointer`, `kZeroDimension`, or
///   `kBufferTooSmall` (checked in that order, buffer untouched).
/// @par Thread safety
/// Reentrant; see the file comment.
/// @par Requirements
/// BMFLP-REQ-001, BMFLP-REQ-004, BMFLP-REQ-005, BMFLP-REQ-008, BMFLP-REQ-009,
/// BMFLP-REQ-017, BMFLP-REQ-018, BMFLP-REQ-021, BMFLP-REQ-022
[[nodiscard]] auto flip_bitmap_unaligned(std::span<std::uint8_t> bitmap,
                                         std::size_t width,
                                         std::size_t height) noexcept
    -> std::expected<void, BitmapFlipError>;

/// @brief Flips a tightly packed bitmap of any width horizontally in place
/// using an optimized candidate implementation (Follow-up 2).
///
/// Initial baseline implementation identical to `flip_bitmap_unaligned`.
/// Rows are `ceil(width / 8)` bytes long with no gap between them. Padding
/// bits of each row's last byte are written as zero.
/// @param bitmap Pixel buffer; at least `ceil(width / 8) * height` bytes.
///   Bytes past that size are not accessed.
/// @param width Image width in pixels (any value > 0, aligned or unaligned).
/// @param height Image height in rows (> 0).
/// @return Nothing on success; otherwise `kNullPointer`, `kZeroDimension`, or
///   `kBufferTooSmall` (checked in that order, buffer untouched).
/// @par Thread safety
/// Reentrant; see the file comment.
/// @par Requirements
/// BMFLP-REQ-001, BMFLP-REQ-004, BMFLP-REQ-005, BMFLP-REQ-008, BMFLP-REQ-009,
/// BMFLP-REQ-017, BMFLP-REQ-018, BMFLP-REQ-021, BMFLP-REQ-022
[[nodiscard]] auto flip_bitmap_unaligned_optimized(
    std::span<std::uint8_t> bitmap, std::size_t width,
    std::size_t height) noexcept -> std::expected<void, BitmapFlipError>;

/// @brief Flips a bitmap whose rows are `stride_bytes` apart, in place.
///
/// Only the `ceil(width / 8)` pixel bytes of each row are written (padding
/// bits zeroed); stride padding bytes are never written.
/// @param bitmap Pixel buffer; at least `stride_bytes * height` bytes.
/// @param width Image width in pixels (> 0).
/// @param height Image height in rows (> 0).
/// @param stride_bytes Distance in bytes between the starts of consecutive
///   rows (`>= ceil(width / 8)`).
/// @return Nothing on success; otherwise `kNullPointer`, `kZeroDimension`,
///   `kInvalidStride`, or `kBufferTooSmall` (in that order, buffer untouched).
/// @par Thread safety
/// Reentrant; see the file comment.
/// @par Requirements
/// BMFLP-REQ-010, BMFLP-REQ-017, BMFLP-REQ-018, BMFLP-REQ-019, BMFLP-REQ-021,
/// BMFLP-REQ-022
[[nodiscard]] auto flip_bitmap_stride(std::span<std::uint8_t> bitmap,
                                      std::size_t width, std::size_t height,
                                      std::size_t stride_bytes) noexcept
    -> std::expected<void, BitmapFlipError>;

/// @brief Flips only the pixels inside a region of interest, in place.
///
/// Pixel `x` of an ROI row (`roi.x <= x < roi.x + roi.width`) receives the
/// value of pixel `2 * roi.x + roi.width - 1 - x`. Every bit outside the ROI,
/// including padding bits and stride padding bytes, is preserved. The ROI's
/// left and right edges may fall anywhere inside a byte.
/// @param bitmap Pixel buffer; at least `stride_bytes * framebuffer_height`
///   bytes.
/// @param framebuffer_width Full framebuffer width in pixels (> 0).
/// @param framebuffer_height Full framebuffer height in rows (> 0).
/// @param stride_bytes Distance in bytes between row starts
///   (`>= ceil(framebuffer_width / 8)`).
/// @param roi Region to flip (non-empty, inside the framebuffer).
/// @return Nothing on success; otherwise `kNullPointer`, `kZeroDimension`,
///   `kInvalidStride`, `kInvalidRoi`, or `kBufferTooSmall` (in that order,
///   buffer untouched).
/// @par Thread safety
/// Reentrant. Writes only bytes that contain ROI pixels, so calls whose ROIs
/// cover disjoint row ranges may run concurrently on one buffer.
/// @par Requirements
/// BMFLP-REQ-011, BMFLP-REQ-017, BMFLP-REQ-018, BMFLP-REQ-019, BMFLP-REQ-020,
/// BMFLP-REQ-021, BMFLP-REQ-022
[[nodiscard]] auto flip_bitmap_roi(std::span<std::uint8_t> bitmap,
                                   std::size_t framebuffer_width,
                                   std::size_t framebuffer_height,
                                   std::size_t stride_bytes,
                                   BitmapRoi roi) noexcept
    -> std::expected<void, BitmapFlipError>;

}  // namespace systems
