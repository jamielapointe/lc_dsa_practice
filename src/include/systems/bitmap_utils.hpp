#pragma once

/// @file bitmap_utils.hpp
/// @brief Internal bitmap and byte-level manipulation utilities.

#include <array>
#include <cstddef>
#include <cstdint>

namespace systems::internal {

/// @brief Number of bits (pixels) in one byte of a 1-bpp bitmap.
inline constexpr std::size_t kBitsPerByte = 8;

/// @brief Maximum number of distinct values for an 8-bit unsigned integer
/// (2^8).
inline constexpr std::size_t kMaxUint8Combinations = 256;

/// @brief Swaps the byte values pointed to by left and right.
/// @param left Pointer to the first byte.
/// @param right Pointer to the second byte.
inline void swap_bytes(std::uint8_t* left, std::uint8_t* right) noexcept {
  const std::uint8_t temp = *left;
  *left = *right;
  *right = temp;
}

/// @brief Computes the bit-reversed value of an 8-bit integer.
/// @param value Byte to reverse.
/// @return The reversed byte with bit i moved to bit 7 - i.
[[nodiscard]] consteval auto compute_reversed_bits(std::uint8_t value) noexcept
    -> std::uint8_t {
  std::uint8_t reverse = 0;
  for (std::uint8_t i = 0; i < 8; ++i) {
    reverse = static_cast<std::uint8_t>((reverse << 1) | (value & 1));
    value >>= 1;
  }
  return reverse;
}

/// @brief Generates a 256-element lookup table of bit-reversed uint8_t values.
/// @return Array containing bit-reversed values for all 256 possible bytes.
[[nodiscard]] consteval auto generate_bit_reverse_table() noexcept
    -> std::array<std::uint8_t, kMaxUint8Combinations> {
  std::array<std::uint8_t, kMaxUint8Combinations> table{};
  for (std::size_t i = 0; i < kMaxUint8Combinations; ++i) {
    table[i] = compute_reversed_bits(static_cast<std::uint8_t>(i));
  }
  return table;
}

/// @brief Mirrors the bit order of one byte: bit `i` moves to bit `7 - i`.
///
/// This is the horizontal flip of one fully used 8-pixel byte.
/// @param value Byte to mirror.
/// @return The mirrored byte.
/// @par Thread safety
/// Pure function; reentrant.
/// @par Requirements
/// BMFLP-REQ-002
[[nodiscard]] constexpr auto reverse_bits(std::uint8_t value) noexcept
    -> std::uint8_t {
  // This is the naive solution; the optimal solution would use a table
  // lookup which would consume 256 bytes.
  //   1. Return `value` with bit i moved to bit 7 - i.
  //   2. Keep it reentrant: no table that is filled in lazily at run time.
  std::uint8_t reverse = 0;
  for (std::uint8_t i = 0; i < 8; ++i) {
    reverse = static_cast<std::uint8_t>((reverse << 1) | (value & 1));
    value >>= 1;
  }
  return reverse;
}

/// @brief Mirrors the bit order of one byte using a 256-entry lookup table.
///
/// Functionally identical to reverse_bits(value). Uses a compile-time static
/// constexpr lookup table of 256 bytes that fits comfortably in L1 cache.
/// @param value Byte to mirror.
/// @return The mirrored byte.
/// @par Thread safety
/// Pure function; reentrant.
/// @par Requirements
/// BMFLP-REQ-002
[[nodiscard]] constexpr auto reverse_bits_lookup(std::uint8_t value) noexcept
    -> std::uint8_t {
  static constexpr std::array<std::uint8_t, kMaxUint8Combinations>
      kLookupTable = generate_bit_reverse_table();
  return kLookupTable[value];
}

}  // namespace systems::internal
