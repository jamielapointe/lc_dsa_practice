#pragma once

/// @file shm_pubsub_posix.h
/// @brief Internal POSIX OS adapter declarations for shm_pubsub in ISO C23.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "systems/shm_pubsub.h"

#ifdef __cplusplus
extern "C" {
#endif

[[nodiscard]] enum shm_pubsub_error shm_pubsub_posix_create(
    struct shm_channel* channel, const char* name, size_t bytes);

[[nodiscard]] enum shm_pubsub_error shm_pubsub_posix_open(
    struct shm_channel* channel, const char* name);

[[nodiscard]] enum shm_pubsub_error shm_pubsub_posix_unlink(const char* name);

void shm_pubsub_posix_unmap(struct shm_channel* channel);

[[nodiscard]] bool shm_pubsub_posix_process_alive(int64_t pid);

[[nodiscard]] int64_t shm_pubsub_posix_current_pid();

void shm_pubsub_posix_yield();

void shm_pubsub_posix_sleep_ns(int64_t sleep_ns);

#ifdef __cplusplus
}  // extern "C"
#endif
