#pragma once

/// @file process_rendezvous.h
/// @brief OS-agnostic cross-process initialization handshake in ISO C23
/// (`ProducerReady` and `ConsumerReady` latches) with low-level POSIX / Darwin
/// wakeup adapters.
///
/// Dual-language safe: can be included by both ISO C23 and C++23 translation
/// units.
/// Requirements: docs/systems/process_rendezvous.md (prefix PRV).

#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "systems/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Status codes returned by process_rendezvous operations.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum process_rendezvous_error : int32_t {
  PROCESS_RENDEZVOUS_OK = 0,
  PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT = 1,
  PROCESS_RENDEZVOUS_ERR_NAME_TOO_LONG = 2,
  PROCESS_RENDEZVOUS_ERR_ALREADY_EXISTS = 3,
  PROCESS_RENDEZVOUS_ERR_NOT_FOUND = 4,
  PROCESS_RENDEZVOUS_ERR_TIMED_OUT = 5,
  PROCESS_RENDEZVOUS_ERR_CLOSED = 6,
  PROCESS_RENDEZVOUS_ERR_WOULD_BLOCK = 7,
  PROCESS_RENDEZVOUS_ERR_LAYOUT_MISMATCH = 8,
  PROCESS_RENDEZVOUS_ERR_OS = 9,
} process_rendezvous_error;

/// @brief Returns a human-readable description of the given error code.
[[nodiscard]] const char* process_rendezvous_error_to_string(
    enum process_rendezvous_error err);

/// @brief "PRV1" in ASCII (0x50525631). Identifies a ProcessRendezvous shared
/// segment.
static constexpr uint32_t PROCESS_RENDEZVOUS_MAGIC = 0x50525631U;

/// @brief Layout version checked by process_rendezvous_open.
static constexpr uint32_t PROCESS_RENDEZVOUS_VERSION = 1U;

/// @brief Cache-aligned lock-free shared-memory layout for the two-stage
/// startup rendezvous (PRV-REQ-012).
struct process_rendezvous_shared {
  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint32_t) magic;
  uint32_t version;
  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint32_t) producer_ready;
  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint32_t) consumer_ready;
  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint32_t) closed;
};

static_assert(
    sizeof(struct process_rendezvous_shared) % SYSTEMS_CACHE_LINE_SIZE == 0,
    "process_rendezvous_shared must be a multiple of cache line size");
static_assert(alignof(struct process_rendezvous_shared) ==
                  SYSTEMS_CACHE_LINE_SIZE,
              "process_rendezvous_shared must be aligned to cache line size");

/// @brief Process rendezvous handle structure.
struct process_rendezvous {
  char name[SYSTEMS_MAX_SHM_NAME_LENGTH + 1];
  int shm_fd;
  struct process_rendezvous_shared* shared;
  sem_t* sem;
  bool is_creator;
};

/// @brief Creates and initializes a new named rendezvous.
[[nodiscard]] enum process_rendezvous_error process_rendezvous_create(
    struct process_rendezvous* rdv, const char* name);

/// @brief Opens an existing named rendezvous created by any process.
[[nodiscard]] enum process_rendezvous_error process_rendezvous_open(
    struct process_rendezvous* rdv, const char* name);

/// @brief Creates the rendezvous if absent, or opens it once initialized within
/// timeout.
[[nodiscard]] enum process_rendezvous_error process_rendezvous_attach(
    struct process_rendezvous* rdv, const char* name, int64_t timeout_ns);

/// @brief Removes the rendezvous name (existing open handles stay usable).
[[nodiscard]] enum process_rendezvous_error process_rendezvous_unlink(
    const char* name);

/// @brief Closes the rendezvous and wakes all blocked waiters. Idempotent.
void process_rendezvous_close(struct process_rendezvous* rdv);

/// @brief Destroys the handle, unmapping shared memory and closing OS handles
/// without unlinking.
void process_rendezvous_destroy(struct process_rendezvous* rdv);

/// @brief Marks the producer stage ready and wakes all waiters. Idempotent.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_signal_producer_ready(struct process_rendezvous* rdv);

/// @brief Waits until producer readiness is signaled or close is called.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_wait_producer_ready(struct process_rendezvous* rdv);

/// @brief Waits up to timeout_ns for producer readiness.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_wait_producer_ready_for(struct process_rendezvous* rdv,
                                           int64_t timeout_ns);

/// @brief Non-blocking check for producer readiness.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_try_wait_producer_ready(
    const struct process_rendezvous* rdv);

/// @brief Marks the consumer stage ready and wakes all waiters. Idempotent.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_signal_consumer_ready(struct process_rendezvous* rdv);

/// @brief Waits until consumer readiness is signaled or close is called.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_wait_consumer_ready(struct process_rendezvous* rdv);

/// @brief Waits up to timeout_ns for consumer readiness.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_wait_consumer_ready_for(struct process_rendezvous* rdv,
                                           int64_t timeout_ns);

/// @brief Non-blocking check for consumer readiness.
[[nodiscard]] enum process_rendezvous_error
process_rendezvous_try_wait_consumer_ready(
    const struct process_rendezvous* rdv);

/// @brief Returns true once any handle has called close.
[[nodiscard]] bool process_rendezvous_is_closed(
    const struct process_rendezvous* rdv);

/// @brief Returns true once signal_producer_ready has been called.
[[nodiscard]] bool process_rendezvous_producer_is_ready(
    const struct process_rendezvous* rdv);

/// @brief Returns true once signal_consumer_ready has been called.
[[nodiscard]] bool process_rendezvous_consumer_is_ready(
    const struct process_rendezvous* rdv);

#ifdef __cplusplus
}  // extern "C"
#endif
