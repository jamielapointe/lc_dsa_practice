#pragma once

/// @file shm_pubsub.h
/// @brief Single-publisher/multi-subscriber (SPMS) channel over a POSIX
/// shared-memory ring buffer in ISO C23, safe across threads and processes.
///
/// Requirements: docs/systems/shm_pubsub.md (prefix SHMPS).
/// Dual-language safe: can be included by both ISO C23 and C++23 translation
/// units.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "systems/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Error codes reported by the shared-memory pub/sub channel.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum shm_pubsub_error : int32_t {
  SHM_PUBSUB_OK = 0,
  SHM_PUBSUB_ERR_NOT_IMPLEMENTED = 1,
  SHM_PUBSUB_ERR_INVALID_ARGUMENT = 2,
  SHM_PUBSUB_ERR_NAME_TOO_LONG = 3,
  SHM_PUBSUB_ERR_ALREADY_EXISTS = 4,
  SHM_PUBSUB_ERR_NOT_FOUND = 5,
  SHM_PUBSUB_ERR_LAYOUT_MISMATCH = 6,
  SHM_PUBSUB_ERR_PUBLISHER_EXISTS = 7,
  SHM_PUBSUB_ERR_PAYLOAD_TOO_LARGE = 8,
  SHM_PUBSUB_ERR_BUFFER_TOO_SMALL = 9,
  SHM_PUBSUB_ERR_WOULD_BLOCK = 10,
  SHM_PUBSUB_ERR_TIMED_OUT = 11,
  SHM_PUBSUB_ERR_CLOSED = 12,
  SHM_PUBSUB_ERR_MEMORY_LOCK_FAILED = 13,
  SHM_PUBSUB_ERR_OS_ERROR = 14,
} shm_pubsub_error;

/// @brief Human-readable description of a shm_pubsub_error.
[[nodiscard]] const char* shm_pubsub_error_to_string(
    enum shm_pubsub_error error);

/// @brief Smallest allowed number of slots.
static constexpr uint32_t SHM_MIN_SLOT_COUNT = 2U;
/// @brief Largest allowed number of slots.
static constexpr uint32_t SHM_MAX_SLOT_COUNT = 4096U;
/// @brief Largest allowed payload size per slot, in bytes.
static constexpr uint32_t SHM_MAX_SLOT_SIZE = 64U * 1024U;

/// @brief Channel geometry, fixed at creation (SHMPS-REQ-001).
struct shm_pubsub_config {
  uint32_t slot_count;
  uint32_t slot_size;
};

/// @brief Metadata of a received sample.
struct shm_sample {
  uint64_t sequence;
  uint32_t size;
  uint64_t missed;
};

/// @brief How a subscriber treats unread samples (SHMPS-REQ-027).
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum shm_read_mode : uint8_t {
  SHM_READ_MODE_IN_ORDER = 0U,
  SHM_READ_MODE_LATEST = 1U,
} shm_read_mode;

/// @brief Cached view of a mapped channel's base address and ring geometry.
struct shm_channel_view {
  uint8_t* base;
  uint32_t slot_mask;
  uint32_t slot_size;
  uint32_t slot_stride;
};

/// @brief A process's handle to a named shared-memory channel.
struct shm_channel {
  uint8_t* base;
  size_t size;
};

/// @brief The single wait-free writer of a channel.
struct shm_publisher {
  struct shm_channel_view view;
  uint64_t next_sequence;
  int64_t owner_pid;
};

/// @brief A lock-free reader of a channel with its own cursor.
struct shm_subscriber {
  struct shm_channel_view view;
  uint64_t next_sequence;
  enum shm_read_mode mode;
};

/// @brief Creates, initializes, pre-faults, and mlocks a new channel.
[[nodiscard]] enum shm_pubsub_error shm_channel_create(
    struct shm_channel* channel, const char* name,
    struct shm_pubsub_config config);

/// @brief Opens an existing channel created by any process, pre-faulting and
/// locking its pages into this process.
[[nodiscard]] enum shm_pubsub_error shm_channel_open(
    struct shm_channel* channel, const char* name);

/// @brief Removes the channel name from the filesystem; existing handles stay
/// usable.
[[nodiscard]] enum shm_pubsub_error shm_channel_unlink(const char* name);

/// @brief Returns the geometry the channel was created with (zeroed if
/// invalid).
[[nodiscard]] struct shm_pubsub_config shm_channel_config(
    const struct shm_channel* channel);

/// @brief Closes the channel for every process. Idempotent.
void shm_channel_close(struct shm_channel* channel);

/// @brief Returns true once any process has called shm_channel_close().
[[nodiscard]] bool shm_channel_is_closed(const struct shm_channel* channel);

/// @brief Unmaps the channel memory without unlinking the shared-memory name.
void shm_channel_destroy(struct shm_channel* channel);

/// @brief Claims the publisher role on channel, recovering from a dead holder
/// if needed (SHMPS-REQ-007, SHMPS-REQ-020).
[[nodiscard]] enum shm_pubsub_error shm_publisher_attach(
    struct shm_publisher* publisher, struct shm_channel* channel);

/// @brief Copies payload into the next slot (wait-free).
[[nodiscard]] enum shm_pubsub_error shm_publisher_publish(
    struct shm_publisher* publisher, const void* payload, size_t payload_size,
    uint64_t* out_sequence);

/// @brief Releases the publisher role if owned by the current process.
void shm_publisher_destroy(struct shm_publisher* publisher);

/// @brief Attaches a subscriber starting at the next published sample.
[[nodiscard]] enum shm_pubsub_error shm_subscriber_attach(
    struct shm_subscriber* subscriber, struct shm_channel* channel,
    enum shm_read_mode mode);

/// @brief Copies the next unread sample into buffer without blocking.
[[nodiscard]] enum shm_pubsub_error shm_subscriber_try_receive(
    struct shm_subscriber* subscriber, void* buffer, size_t buffer_size,
    struct shm_sample* out_sample);

/// @brief Waits up to timeout_ns for a sample.
[[nodiscard]] enum shm_pubsub_error shm_subscriber_receive_for(
    struct shm_subscriber* subscriber, void* buffer, size_t buffer_size,
    int64_t timeout_ns, struct shm_sample* out_sample);

/// @brief Waits until a sample is available or the channel is closed.
[[nodiscard]] enum shm_pubsub_error shm_subscriber_receive(
    struct shm_subscriber* subscriber, void* buffer, size_t buffer_size,
    struct shm_sample* out_sample);

/// @brief Resets a subscriber handle.
void shm_subscriber_destroy(struct shm_subscriber* subscriber);

#ifdef __cplusplus
}  // extern "C"
#endif
