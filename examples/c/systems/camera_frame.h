#pragma once

/// @file camera_frame.h
/// @brief Fixed-size synthetic monochrome 32-bit camera frame payload and
/// endian-independent IEEE 802.3 CRC-32 checksum for the ISO C23 shm_pubsub
/// multi-process example.

#include <stddef.h>
#include <stdint.h>

#include "systems/shm_pubsub.h"

#ifdef __cplusplus
extern "C" {
#endif

static constexpr uint32_t CAMERA_FRAME_WIDTH = 64U;
static constexpr uint32_t CAMERA_FRAME_HEIGHT = 64U;
static constexpr size_t CAMERA_PIXEL_COUNT =
    (size_t)CAMERA_FRAME_WIDTH * (size_t)CAMERA_FRAME_HEIGHT;

struct camera_frame_payload {
  uint64_t frame_id;
  uint64_t timestamp_ns;
  uint32_t width;
  uint32_t height;
  uint32_t pixel_crc32;
  uint32_t reserved;
  uint32_t pixels[CAMERA_PIXEL_COUNT];
};

static_assert(sizeof(struct camera_frame_payload) <= SHM_MAX_SLOT_SIZE,
              "camera_frame_payload must fit inside a single ShmPubSub slot");

[[nodiscard]] static inline uint32_t camera_crc32_update_byte(uint32_t crc,
                                                              uint8_t byte) {
  static constexpr uint32_t kPolynomial = 0xEDB8'8320U;
  crc ^= (uint32_t)byte;
  for (int bit = 0; bit < 8; ++bit) {
    const uint32_t mask = (crc & 1U) != 0U ? 0xFFFF'FFFFU : 0U;
    crc = (crc >> 1U) ^ (kPolynomial & mask);
  }
  return crc;
}

[[nodiscard]] static inline uint32_t camera_compute_pixel_crc32(
    const uint32_t* pixels, size_t count) {
  uint32_t crc = 0xFFFF'FFFFU;
  for (size_t i = 0U; i < count; ++i) {
    const uint32_t px = pixels[i];
    crc = camera_crc32_update_byte(crc, (uint8_t)(px & 0xFFU));
    crc = camera_crc32_update_byte(crc, (uint8_t)((px >> 8U) & 0xFFU));
    crc = camera_crc32_update_byte(crc, (uint8_t)((px >> 16U) & 0xFFU));
    crc = camera_crc32_update_byte(crc, (uint8_t)((px >> 24U) & 0xFFU));
  }
  return ~crc;
}

#ifdef __cplusplus
}  // extern "C"
#endif
