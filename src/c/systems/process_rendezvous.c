/// @file process_rendezvous.c
/// @brief Portable cross-process startup handshake logic in ISO C23.
///
/// Free of POSIX headers (PRV-REQ-011).

#include "systems/process_rendezvous.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "process_rendezvous_posix.h"
#include "systems/platform.h"

[[nodiscard]] const char* process_rendezvous_error_to_string(
    enum process_rendezvous_error err) {
  switch (err) {
    case PROCESS_RENDEZVOUS_OK:
      return "ok";
    case PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT:
      return "invalid argument";
    case PROCESS_RENDEZVOUS_ERR_NAME_TOO_LONG:
      return "name too long";
    case PROCESS_RENDEZVOUS_ERR_ALREADY_EXISTS:
      return "already exists";
    case PROCESS_RENDEZVOUS_ERR_NOT_FOUND:
      return "not found";
    case PROCESS_RENDEZVOUS_ERR_TIMED_OUT:
      return "timed out";
    case PROCESS_RENDEZVOUS_ERR_CLOSED:
      return "closed";
    case PROCESS_RENDEZVOUS_ERR_WOULD_BLOCK:
      return "would block";
    case PROCESS_RENDEZVOUS_ERR_LAYOUT_MISMATCH:
      return "layout mismatch";
    case PROCESS_RENDEZVOUS_ERR_OS:
      return "operating-system error";
  }
  return "unknown error";
}

[[nodiscard]] static enum process_rendezvous_error validate_name(
    const char* name) {
  if (name == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  const size_t len = strlen(name);
  if (len < 2U || name[0] != '/' || strchr(name + 1, '/') != nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  if (len > SYSTEMS_MAX_SHM_NAME_LENGTH) {
    return PROCESS_RENDEZVOUS_ERR_NAME_TOO_LONG;
  }
  return PROCESS_RENDEZVOUS_OK;
}

[[nodiscard]] static int64_t monotonic_now_ns() {
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec * 1'000'000'000LL) + (int64_t)ts.tv_nsec;
}

[[nodiscard]] static enum process_rendezvous_error try_wait_flag(
    const struct process_rendezvous* rdv, const _Atomic(uint32_t)* flag) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  const bool closed =
      atomic_load_explicit(&rdv->shared->closed, memory_order_acquire) != 0U;
  if (atomic_load_explicit(flag, memory_order_acquire) != 0U) {
    return PROCESS_RENDEZVOUS_OK;
  }
  return closed ? PROCESS_RENDEZVOUS_ERR_CLOSED
                : PROCESS_RENDEZVOUS_ERR_WOULD_BLOCK;
}

[[nodiscard]] static enum process_rendezvous_error wait_flag_until(
    struct process_rendezvous* rdv, const _Atomic(uint32_t)* flag,
    int64_t timeout_ns) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  const int64_t start_ns = monotonic_now_ns();
  int64_t deadline_ns = 0;
  if (ckd_add(&deadline_ns, start_ns, timeout_ns)) {
    deadline_ns = INT64_MAX;
  }

  constexpr int64_t kMaxSliceNs = 2'000'000LL;  // 2 ms slice

  for (;;) {
    const enum process_rendezvous_error status = try_wait_flag(rdv, flag);
    if (status != PROCESS_RENDEZVOUS_ERR_WOULD_BLOCK) {
      return status;
    }
    const int64_t now_ns = monotonic_now_ns();
    if (now_ns >= deadline_ns) {
      return PROCESS_RENDEZVOUS_ERR_TIMED_OUT;
    }
    const int64_t remaining_ns = deadline_ns - now_ns;
    const int64_t slice_ns =
        remaining_ns < kMaxSliceNs ? remaining_ns : kMaxSliceNs;
    process_rendezvous_posix_wait_slice(rdv, slice_ns);
  }
}

enum process_rendezvous_error process_rendezvous_create(
    struct process_rendezvous* rdv, const char* name) {
  if (rdv == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  const enum process_rendezvous_error err = validate_name(name);
  if (err != PROCESS_RENDEZVOUS_OK) {
    return err;
  }

  memset(rdv, 0, sizeof(*rdv));
  rdv->shm_fd = -1;

  const enum process_rendezvous_error os_err = process_rendezvous_posix_create(
      rdv, name, sizeof(struct process_rendezvous_shared));
  if (os_err != PROCESS_RENDEZVOUS_OK) {
    return os_err;
  }

  struct process_rendezvous_shared* s = rdv->shared;
  s->version = PROCESS_RENDEZVOUS_VERSION;
  atomic_store_explicit(&s->producer_ready, 0U, memory_order_relaxed);
  atomic_store_explicit(&s->consumer_ready, 0U, memory_order_relaxed);
  atomic_store_explicit(&s->closed, 0U, memory_order_relaxed);
  atomic_store_explicit(&s->magic, PROCESS_RENDEZVOUS_MAGIC,
                        memory_order_release);
  return PROCESS_RENDEZVOUS_OK;
}

enum process_rendezvous_error process_rendezvous_open(
    struct process_rendezvous* rdv, const char* name) {
  if (rdv == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  const enum process_rendezvous_error err = validate_name(name);
  if (err != PROCESS_RENDEZVOUS_OK) {
    return err;
  }

  memset(rdv, 0, sizeof(*rdv));
  rdv->shm_fd = -1;

  const enum process_rendezvous_error os_err =
      process_rendezvous_posix_open(rdv, name);
  if (os_err != PROCESS_RENDEZVOUS_OK) {
    return os_err;
  }

  struct process_rendezvous_shared* s = rdv->shared;
  const uint32_t magic = atomic_load_explicit(&s->magic, memory_order_acquire);
  if (magic != PROCESS_RENDEZVOUS_MAGIC ||
      s->version != PROCESS_RENDEZVOUS_VERSION) {
    process_rendezvous_destroy(rdv);
    return PROCESS_RENDEZVOUS_ERR_LAYOUT_MISMATCH;
  }
  return PROCESS_RENDEZVOUS_OK;
}

enum process_rendezvous_error process_rendezvous_attach(
    struct process_rendezvous* rdv, const char* name, int64_t timeout_ns) {
  if (rdv == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  const enum process_rendezvous_error err = validate_name(name);
  if (err != PROCESS_RENDEZVOUS_OK) {
    return err;
  }

  const int64_t start_ns = monotonic_now_ns();
  int64_t deadline_ns = 0;
  if (ckd_add(&deadline_ns, start_ns, timeout_ns)) {
    deadline_ns = INT64_MAX;
  }

  for (;;) {
    enum process_rendezvous_error create_err =
        process_rendezvous_create(rdv, name);
    if (create_err == PROCESS_RENDEZVOUS_OK) {
      return PROCESS_RENDEZVOUS_OK;
    }
    if (create_err != PROCESS_RENDEZVOUS_ERR_ALREADY_EXISTS) {
      return create_err;
    }

    enum process_rendezvous_error open_err = process_rendezvous_open(rdv, name);
    if (open_err == PROCESS_RENDEZVOUS_OK) {
      return PROCESS_RENDEZVOUS_OK;
    }
    if (open_err != PROCESS_RENDEZVOUS_ERR_LAYOUT_MISMATCH &&
        open_err != PROCESS_RENDEZVOUS_ERR_NOT_FOUND) {
      return open_err;
    }

    if (monotonic_now_ns() >= deadline_ns) {
      return PROCESS_RENDEZVOUS_ERR_TIMED_OUT;
    }
    process_rendezvous_posix_sleep_us(100U);
  }
}

enum process_rendezvous_error process_rendezvous_unlink(const char* name) {
  const enum process_rendezvous_error err = validate_name(name);
  if (err != PROCESS_RENDEZVOUS_OK) {
    return err;
  }
  return process_rendezvous_posix_unlink(name);
}

void process_rendezvous_close(struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return;
  }
  atomic_store_explicit(&rdv->shared->closed, 1U, memory_order_release);
  process_rendezvous_posix_notify(rdv);
}

void process_rendezvous_destroy(struct process_rendezvous* rdv) {
  process_rendezvous_posix_cleanup(rdv);
}

enum process_rendezvous_error process_rendezvous_signal_producer_ready(
    struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  if (atomic_load_explicit(&rdv->shared->closed, memory_order_acquire) != 0U) {
    return PROCESS_RENDEZVOUS_ERR_CLOSED;
  }
  atomic_store_explicit(&rdv->shared->producer_ready, 1U, memory_order_release);
  process_rendezvous_posix_notify(rdv);
  return PROCESS_RENDEZVOUS_OK;
}

enum process_rendezvous_error process_rendezvous_wait_producer_ready(
    struct process_rendezvous* rdv) {
  return process_rendezvous_wait_producer_ready_for(rdv, INT64_MAX);
}

enum process_rendezvous_error process_rendezvous_wait_producer_ready_for(
    struct process_rendezvous* rdv, int64_t timeout_ns) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  return wait_flag_until(rdv, &rdv->shared->producer_ready, timeout_ns);
}

enum process_rendezvous_error process_rendezvous_try_wait_producer_ready(
    const struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  return try_wait_flag(rdv, &rdv->shared->producer_ready);
}

enum process_rendezvous_error process_rendezvous_signal_consumer_ready(
    struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  if (atomic_load_explicit(&rdv->shared->closed, memory_order_acquire) != 0U) {
    return PROCESS_RENDEZVOUS_ERR_CLOSED;
  }
  atomic_store_explicit(&rdv->shared->consumer_ready, 1U, memory_order_release);
  process_rendezvous_posix_notify(rdv);
  return PROCESS_RENDEZVOUS_OK;
}

enum process_rendezvous_error process_rendezvous_wait_consumer_ready(
    struct process_rendezvous* rdv) {
  return process_rendezvous_wait_consumer_ready_for(rdv, INT64_MAX);
}

enum process_rendezvous_error process_rendezvous_wait_consumer_ready_for(
    struct process_rendezvous* rdv, int64_t timeout_ns) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  return wait_flag_until(rdv, &rdv->shared->consumer_ready, timeout_ns);
}

enum process_rendezvous_error process_rendezvous_try_wait_consumer_ready(
    const struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT;
  }
  return try_wait_flag(rdv, &rdv->shared->consumer_ready);
}

bool process_rendezvous_is_closed(const struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return false;
  }
  return atomic_load_explicit(&rdv->shared->closed, memory_order_acquire) != 0U;
}

bool process_rendezvous_producer_is_ready(
    const struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return false;
  }
  return atomic_load_explicit(&rdv->shared->producer_ready,
                              memory_order_acquire) != 0U;
}

bool process_rendezvous_consumer_is_ready(
    const struct process_rendezvous* rdv) {
  if (rdv == nullptr || rdv->shared == nullptr) {
    return false;
  }
  return atomic_load_explicit(&rdv->shared->consumer_ready,
                              memory_order_acquire) != 0U;
}
