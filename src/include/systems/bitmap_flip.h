#pragma once

/// @file bitmap_flip.h
/// @brief In-place horizontal flip (mirror) of 1-bit-per-pixel monochrome
/// bitmaps in ISO C23.
///
/// Specification: docs/systems/bitmap_flip.md (Prefix BMFLP).
/// Memory layout: Row y starts at y * stride_bytes. Pixel x is bit (x % 8) of
/// byte (x / 8). Bit 0 (LSB) is the leftmost pixel of each byte; Bit 7 (MSB) is
/// the rightmost.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Number of bits (pixels) per byte in a 1-bpp bitmap.
static constexpr size_t BITMAP_BITS_PER_BYTE = 8U;

/// @brief Number of distinct values for an 8-bit byte (2^8).
static constexpr size_t BITMAP_MAX_UINT8_COMBINATIONS = 256U;

/// @brief Status and error codes returned by bounds-checked bitmap flip
/// operations.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum bitmap_flip_error : int32_t {
  BITMAP_FLIP_OK = 0,
  BITMAP_FLIP_ERR_NULL_POINTER = 1,
  BITMAP_FLIP_ERR_ZERO_DIMENSIONS = 2,
  BITMAP_FLIP_ERR_INVALID_WIDTH = 3,
  BITMAP_FLIP_ERR_INVALID_HEIGHT = 4,
  BITMAP_FLIP_ERR_INVALID_STRIDE = 5,
  BITMAP_FLIP_ERR_BUFFER_TOO_SMALL = 6,
  BITMAP_FLIP_ERR_INVALID_ROI = 7,
} bitmap_flip_error;

/// @brief Returns a human-readable description of a bitmap_flip_error.
[[nodiscard]] const char* bitmap_flip_error_to_string(
    enum bitmap_flip_error error);

/// @brief Rectangular region of interest, in pixels, inside a framebuffer.
struct bitmap_roi {
  size_t x;
  size_t y;
  size_t width;
  size_t height;
};
typedef struct bitmap_roi bitmap_roi;

/// @brief Reverses the bits of a single byte using an arithmetic loop.
[[nodiscard]] uint8_t bitmap_reverse_bits(uint8_t value);

/// @brief Reverses the bits of a single byte using a 256-byte compile-time
/// lookup table.
[[nodiscard]] uint8_t bitmap_reverse_bits_lookup(uint8_t value);

/// @brief Flips a byte-aligned (width % 8 == 0) tightly packed bitmap
/// horizontally in place.
[[nodiscard]] enum bitmap_flip_error bitmap_flip_aligned(uint8_t* bitmap,
                                                         size_t buf_size,
                                                         size_t width,
                                                         size_t height);

/// @brief Flips a tightly packed bitmap of any width (> 0) horizontally in
/// place.
[[nodiscard]] enum bitmap_flip_error bitmap_flip_unaligned(uint8_t* bitmap,
                                                           size_t buf_size,
                                                           size_t width,
                                                           size_t height);

/// @brief Flips a tightly packed bitmap of any width (> 0) horizontally in
/// place using the optimized candidate implementation.
[[nodiscard]] enum bitmap_flip_error bitmap_flip_unaligned_optimized(
    uint8_t* bitmap, size_t buf_size, size_t width, size_t height);

/// @brief Flips a bitmap whose rows are stride_bytes apart in place.
[[nodiscard]] enum bitmap_flip_error bitmap_flip_strided(uint8_t* bitmap,
                                                         size_t buf_size,
                                                         size_t width,
                                                         size_t height,
                                                         size_t stride_bytes);

/// @brief Flips only the pixels inside a region of interest in place.
[[nodiscard]] enum bitmap_flip_error bitmap_flip_roi(
    uint8_t* bitmap, size_t buf_size, size_t frame_width, size_t frame_height,
    size_t stride_bytes, struct bitmap_roi roi);

#ifdef __cplusplus
}
#endif
