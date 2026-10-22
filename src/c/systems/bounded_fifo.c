/**
 * @file bounded_fifo.c
 * @brief Implementation of fixed-capacity, thread-safe MPMC bounded FIFO in ISO
 * C23.
 */

#include "systems/bounded_fifo.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/errno.h>
#include <time.h>

static void compute_deadline(int64_t timeout_ns,
                             struct timespec* out_deadline) {
  struct timespec now = {};
  clock_gettime(CLOCK_REALTIME, &now);

  const uint64_t u_timeout = (uint64_t)timeout_ns;
  const uint64_t sec_delta = u_timeout / 1000000000ULL;
  const uint64_t nsec_delta = u_timeout % 1000000000ULL;

  uint64_t total_nsec = (uint64_t)now.tv_nsec + nsec_delta;
  uint64_t extra_sec = 0U;
  if (total_nsec >= 1000000000ULL) {
    extra_sec = 1U;
    total_nsec -= 1000000000ULL;
  }

  const uint64_t total_sec = sec_delta + extra_sec;
  uint64_t final_sec = 0U;
  if (ckd_add(&final_sec, (uint64_t)now.tv_sec, total_sec)) {
    out_deadline->tv_sec = (time_t)INT64_MAX;
    out_deadline->tv_nsec = (long)total_nsec;
    return;
  }

  if (final_sec > (uint64_t)INT64_MAX) {
    out_deadline->tv_sec = (time_t)INT64_MAX;
  } else {
    out_deadline->tv_sec = (time_t)final_sec;
  }
  out_deadline->tv_nsec = (long)total_nsec;
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_init(
    struct bounded_fifo* fifo, void* buffer, size_t capacity,
    size_t elem_size) {
  if (fifo == nullptr || buffer == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }
  if (capacity == 0U || elem_size == 0U) {
    return BOUNDED_FIFO_ERR_INVALID_ARG;
  }

  size_t total_bytes = 0U;
  if (ckd_mul(&total_bytes, capacity, elem_size)) {
    return BOUNDED_FIFO_ERR_INVALID_ARG;
  }

  fifo->buffer = (uint8_t*)buffer;
  fifo->capacity = capacity;
  fifo->elem_size = elem_size;
  fifo->head = 0U;
  fifo->tail = 0U;
  fifo->count = 0U;
  fifo->closed = false;

  int rc = pthread_mutex_init(&fifo->mutex, nullptr);
  if (rc != 0) {
    return BOUNDED_FIFO_ERR_INVALID_ARG;
  }

  rc = pthread_cond_init(&fifo->not_empty, nullptr);
  if (rc != 0) {
    pthread_mutex_destroy(&fifo->mutex);
    return BOUNDED_FIFO_ERR_INVALID_ARG;
  }

  rc = pthread_cond_init(&fifo->not_full, nullptr);
  if (rc != 0) {
    pthread_cond_destroy(&fifo->not_empty);
    pthread_mutex_destroy(&fifo->mutex);
    return BOUNDED_FIFO_ERR_INVALID_ARG;
  }

  return BOUNDED_FIFO_OK;
}

void bounded_fifo_destroy(struct bounded_fifo* fifo) {
  if (fifo == nullptr) {
    return;
  }
  pthread_cond_destroy(&fifo->not_full);
  pthread_cond_destroy(&fifo->not_empty);
  pthread_mutex_destroy(&fifo->mutex);
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_try_push(
    struct bounded_fifo* fifo, const void* elem) {
  if (fifo == nullptr || elem == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }

  pthread_mutex_lock(&fifo->mutex);
  if (fifo->closed) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }
  if (fifo->count == fifo->capacity) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_FULL;
  }

  const size_t offset = fifo->tail * fifo->elem_size;
  memcpy(fifo->buffer + offset, elem, fifo->elem_size);
  fifo->tail = (fifo->tail + 1U) % fifo->capacity;
  fifo->count++;

  pthread_mutex_unlock(&fifo->mutex);
  pthread_cond_signal(&fifo->not_empty);
  return BOUNDED_FIFO_OK;
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_push(
    struct bounded_fifo* fifo, const void* elem) {
  if (fifo == nullptr || elem == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }

  pthread_mutex_lock(&fifo->mutex);
  while (!fifo->closed && fifo->count == fifo->capacity) {
    pthread_cond_wait(&fifo->not_full, &fifo->mutex);
  }

  if (fifo->closed) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }

  const size_t offset = fifo->tail * fifo->elem_size;
  memcpy(fifo->buffer + offset, elem, fifo->elem_size);
  fifo->tail = (fifo->tail + 1U) % fifo->capacity;
  fifo->count++;

  pthread_mutex_unlock(&fifo->mutex);
  pthread_cond_signal(&fifo->not_empty);
  return BOUNDED_FIFO_OK;
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_push_for(
    struct bounded_fifo* fifo, const void* elem, int64_t timeout_ns) {
  if (fifo == nullptr || elem == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }

  pthread_mutex_lock(&fifo->mutex);
  if (fifo->closed) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }

  if (fifo->count < fifo->capacity) {
    const size_t offset = fifo->tail * fifo->elem_size;
    memcpy(fifo->buffer + offset, elem, fifo->elem_size);
    fifo->tail = (fifo->tail + 1U) % fifo->capacity;
    fifo->count++;
    pthread_mutex_unlock(&fifo->mutex);
    pthread_cond_signal(&fifo->not_empty);
    return BOUNDED_FIFO_OK;
  }

  if (timeout_ns <= 0) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_TIMED_OUT;
  }

  struct timespec deadline = {};
  compute_deadline(timeout_ns, &deadline);

  while (!fifo->closed && fifo->count == fifo->capacity) {
    int rc = pthread_cond_timedwait(&fifo->not_full, &fifo->mutex, &deadline);
    if (rc == ETIMEDOUT) {
      if (fifo->closed) {
        pthread_mutex_unlock(&fifo->mutex);
        return BOUNDED_FIFO_ERR_CLOSED;
      }
      if (fifo->count == fifo->capacity) {
        pthread_mutex_unlock(&fifo->mutex);
        return BOUNDED_FIFO_ERR_TIMED_OUT;
      }
      break;
    }
  }

  if (fifo->closed) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }

  const size_t offset = fifo->tail * fifo->elem_size;
  memcpy(fifo->buffer + offset, elem, fifo->elem_size);
  fifo->tail = (fifo->tail + 1U) % fifo->capacity;
  fifo->count++;

  pthread_mutex_unlock(&fifo->mutex);
  pthread_cond_signal(&fifo->not_empty);
  return BOUNDED_FIFO_OK;
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_try_pop(
    struct bounded_fifo* fifo, void* out_elem) {
  if (fifo == nullptr || out_elem == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }

  pthread_mutex_lock(&fifo->mutex);
  if (fifo->count == 0U) {
    enum bounded_fifo_status status =
        fifo->closed ? BOUNDED_FIFO_ERR_CLOSED : BOUNDED_FIFO_ERR_EMPTY;
    pthread_mutex_unlock(&fifo->mutex);
    return status;
  }

  const size_t offset = fifo->head * fifo->elem_size;
  memcpy(out_elem, fifo->buffer + offset, fifo->elem_size);
  fifo->head = (fifo->head + 1U) % fifo->capacity;
  fifo->count--;

  pthread_mutex_unlock(&fifo->mutex);
  pthread_cond_signal(&fifo->not_full);
  return BOUNDED_FIFO_OK;
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_pop(
    struct bounded_fifo* fifo, void* out_elem) {
  if (fifo == nullptr || out_elem == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }

  pthread_mutex_lock(&fifo->mutex);
  while (!fifo->closed && fifo->count == 0U) {
    pthread_cond_wait(&fifo->not_empty, &fifo->mutex);
  }

  if (fifo->count == 0U) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }

  const size_t offset = fifo->head * fifo->elem_size;
  memcpy(out_elem, fifo->buffer + offset, fifo->elem_size);
  fifo->head = (fifo->head + 1U) % fifo->capacity;
  fifo->count--;

  pthread_mutex_unlock(&fifo->mutex);
  pthread_cond_signal(&fifo->not_full);
  return BOUNDED_FIFO_OK;
}

[[nodiscard]] enum bounded_fifo_status bounded_fifo_pop_for(
    struct bounded_fifo* fifo, void* out_elem, int64_t timeout_ns) {
  if (fifo == nullptr || out_elem == nullptr) {
    return BOUNDED_FIFO_ERR_NULL_PTR;
  }

  pthread_mutex_lock(&fifo->mutex);
  if (fifo->count > 0U) {
    const size_t offset = fifo->head * fifo->elem_size;
    memcpy(out_elem, fifo->buffer + offset, fifo->elem_size);
    fifo->head = (fifo->head + 1U) % fifo->capacity;
    fifo->count--;
    pthread_mutex_unlock(&fifo->mutex);
    pthread_cond_signal(&fifo->not_full);
    return BOUNDED_FIFO_OK;
  }

  if (fifo->closed) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }

  if (timeout_ns <= 0) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_TIMED_OUT;
  }

  struct timespec deadline = {};
  compute_deadline(timeout_ns, &deadline);

  while (!fifo->closed && fifo->count == 0U) {
    int rc = pthread_cond_timedwait(&fifo->not_empty, &fifo->mutex, &deadline);
    if (rc == ETIMEDOUT) {
      if (fifo->count == 0U) {
        enum bounded_fifo_status status =
            fifo->closed ? BOUNDED_FIFO_ERR_CLOSED : BOUNDED_FIFO_ERR_TIMED_OUT;
        pthread_mutex_unlock(&fifo->mutex);
        return status;
      }
      break;
    }
  }

  if (fifo->count == 0U) {
    pthread_mutex_unlock(&fifo->mutex);
    return BOUNDED_FIFO_ERR_CLOSED;
  }

  const size_t offset = fifo->head * fifo->elem_size;
  memcpy(out_elem, fifo->buffer + offset, fifo->elem_size);
  fifo->head = (fifo->head + 1U) % fifo->capacity;
  fifo->count--;

  pthread_mutex_unlock(&fifo->mutex);
  pthread_cond_signal(&fifo->not_full);
  return BOUNDED_FIFO_OK;
}

void bounded_fifo_close(struct bounded_fifo* fifo) {
  if (fifo == nullptr) {
    return;
  }

  pthread_mutex_lock(&fifo->mutex);
  fifo->closed = true;
  pthread_cond_broadcast(&fifo->not_empty);
  pthread_cond_broadcast(&fifo->not_full);
  pthread_mutex_unlock(&fifo->mutex);
}

[[nodiscard]] bool bounded_fifo_is_closed(const struct bounded_fifo* fifo) {
  if (fifo == nullptr) {
    return false;
  }
  struct bounded_fifo* non_const_fifo = nullptr;
  memcpy(&non_const_fifo, &fifo, sizeof(fifo));
  pthread_mutex_lock(&non_const_fifo->mutex);
  const bool closed = non_const_fifo->closed;
  pthread_mutex_unlock(&non_const_fifo->mutex);
  return closed;
}

[[nodiscard]] size_t bounded_fifo_size(const struct bounded_fifo* fifo) {
  if (fifo == nullptr) {
    return 0U;
  }
  struct bounded_fifo* non_const_fifo = nullptr;
  memcpy(&non_const_fifo, &fifo, sizeof(fifo));
  pthread_mutex_lock(&non_const_fifo->mutex);
  const size_t count = non_const_fifo->count;
  pthread_mutex_unlock(&non_const_fifo->mutex);
  return count;
}

[[nodiscard]] size_t bounded_fifo_capacity(const struct bounded_fifo* fifo) {
  if (fifo == nullptr) {
    return 0U;
  }
  return fifo->capacity;
}

[[nodiscard]] bool bounded_fifo_is_empty(const struct bounded_fifo* fifo) {
  return bounded_fifo_size(fifo) == 0U;
}

[[nodiscard]] bool bounded_fifo_is_full(const struct bounded_fifo* fifo) {
  if (fifo == nullptr) {
    return false;
  }
  return bounded_fifo_size(fifo) == fifo->capacity;
}
