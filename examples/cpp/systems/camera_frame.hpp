#pragma once

/// @file camera_frame.hpp
/// @brief Fixed-size synthetic monochrome 32-bit camera frame payload and
/// endian-independent IEEE 802.3 CRC-32 checksum for the `shm_pubsub`
/// multi-process example.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include "systems/shm_pubsub.hpp"

namespace systems::examples {

/// @brief Width of the synthetic monochrome camera frame in pixels.
inline constexpr uint32_t kCameraFrameWidth = 64;
/// @brief Height of the synthetic monochrome camera frame in pixels.
inline constexpr uint32_t kCameraFrameHeight = 64;
/// @brief Total number of 32-bit monochrome pixels per frame (4,096 pixels =
/// 16 KiB).
inline constexpr size_t kCameraPixelCount =
    static_cast<size_t>(kCameraFrameWidth) * kCameraFrameHeight;

/// @brief Fixed-size camera sample transmitted across a `systems::ShmChannel`
/// slot.
struct CameraFramePayload {
  uint64_t frame_id{0};                 ///< 1-based producer frame number.
  uint64_t timestamp_ns{0};             ///< Monotonic publish timestamp.
  uint32_t width{kCameraFrameWidth};    ///< Image width in pixels.
  uint32_t height{kCameraFrameHeight};  ///< Image height in pixels.
  uint32_t pixel_crc32{0};              ///< IEEE 802.3 CRC-32 of `pixels`.
  uint32_t reserved{0};                 ///< Alignment padding.
  std::array<uint32_t, kCameraPixelCount> pixels{};
};

static_assert(std::is_trivially_copyable_v<CameraFramePayload>);
static_assert(std::is_standard_layout_v<CameraFramePayload>);
static_assert(sizeof(CameraFramePayload) <= systems::kShmMaxSlotSize,
              "CameraFramePayload must fit inside a single ShmPubSub slot");

/// @brief Updates a running IEEE 802.3 CRC-32 state with one byte.
[[nodiscard]] constexpr auto crc32_update_byte(uint32_t crc,
                                               uint8_t byte) noexcept
    -> uint32_t {
  constexpr uint32_t kPolynomial = 0xEDB8'8320U;
  crc ^= static_cast<uint32_t>(byte);
  for (int bit = 0; bit < 8; ++bit) {
    const uint32_t mask = -(crc & 1U);
    crc = (crc >> 1U) ^ (kPolynomial & mask);
  }
  return crc;
}

/// @brief Computes the IEEE 802.3 CRC-32 checksum over a span of 32-bit pixels
/// in canonical little-endian byte order.
[[nodiscard]] constexpr auto compute_pixel_crc32(
    std::span<const uint32_t> pixels) noexcept -> uint32_t {
  uint32_t crc = 0xFFFF'FFFFU;
  for (const uint32_t pixel : pixels) {
    crc = crc32_update_byte(crc, static_cast<uint8_t>(pixel & 0xFFU));
    crc = crc32_update_byte(crc, static_cast<uint8_t>((pixel >> 8U) & 0xFFU));
    crc = crc32_update_byte(crc, static_cast<uint8_t>((pixel >> 16U) & 0xFFU));
    crc = crc32_update_byte(crc, static_cast<uint8_t>((pixel >> 24U) & 0xFFU));
  }
  return ~crc;
}

}  // namespace systems::examples
