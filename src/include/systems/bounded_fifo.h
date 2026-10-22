#pragma once

/// @file bounded_fifo.h
/// @brief Fixed-capacity, heap-free, thread-safe (MPMC) FIFO queue in ISO C23.
///
/// Thread safety:
///   - Any number of producers and consumers may call operations concurrently.
///   - FIFO order is preserved with respect to the order in which pushes
///   acquire the internal lock.
///
/// Shutdown:
///   - bounded_fifo_close() wakes every blocked thread. After close, pushes
///   fail (BOUNDED_FIFO_ERR_CLOSED)
///     while pops keep draining the remaining elements until empty, then return
///     BOUNDED_FIFO_ERR_CLOSED.
///
/// Dual-language safe: can be included by both ISO C23 and C++23 translation
/// units.

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Status codes returned by bounded_fifo operations.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum bounded_fifo_status : int32_t {
  BOUNDED_FIFO_OK = 0,
  BOUNDED_FIFO_ERR_FULL = 1,
  BOUNDED_FIFO_ERR_EMPTY = 2,
  BOUNDED_FIFO_ERR_CLOSED = 3,
  BOUNDED_FIFO_ERR_TIMED_OUT = 4,
  BOUNDED_FIFO_ERR_INVALID_ARG = 5,
  BOUNDED_FIFO_ERR_NULL_PTR = 6,
} bounded_fifo_status;

/// @brief Fixed-capacity bounded FIFO ring buffer representation.
struct bounded_fifo {
  size_t capacity;
  size_t elem_size;
  size_t head;
  size_t tail;
  size_t count;
  bool closed;
  uint8_t* buffer;
  pthread_mutex_t mutex;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
};

/// @brief Initializes a bounded FIFO with caller-supplied storage.
/// @param fifo Pointer to the bounded_fifo struct to initialize.
/// @param buffer Pointer to contiguous storage buffer of at least capacity *
/// elem_size bytes.
/// @param capacity Maximum number of elements the queue can hold; must be > 0.
/// @param elem_size Size in bytes of a single element; must be > 0.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_NULL_PTR if fifo or
/// buffer is null,
///         BOUNDED_FIFO_ERR_INVALID_ARG if capacity or elem_size is 0 or
///         calculation overflows.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_init(
    struct bounded_fifo* fifo, void* buffer, size_t capacity, size_t elem_size);

/// @brief Destroys the synchronization primitives inside the bounded FIFO.
/// @param fifo Pointer to the bounded_fifo struct to destroy.
void bounded_fifo_destroy(struct bounded_fifo* fifo);

/// @brief Copies an element into the queue if space is available; never blocks.
/// @param fifo Pointer to the initialized FIFO.
/// @param elem Pointer to the element to copy into the queue.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_FULL if full,
///         BOUNDED_FIFO_ERR_CLOSED if closed, BOUNDED_FIFO_ERR_NULL_PTR if
///         null.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_try_push(
    struct bounded_fifo* fifo, const void* elem);

/// @brief Copies an element into the queue, blocking while the queue is full.
/// @param fifo Pointer to the initialized FIFO.
/// @param elem Pointer to the element to copy into the queue.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_CLOSED if closed during
/// or prior to wait.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_push(
    struct bounded_fifo* fifo, const void* elem);

/// @brief Copies an element into the queue, blocking up to timeout_ns if full.
/// @param fifo Pointer to the initialized FIFO.
/// @param elem Pointer to the element to copy into the queue.
/// @param timeout_ns Maximum duration to wait in nanoseconds.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_TIMED_OUT if timeout
/// expired,
///         BOUNDED_FIFO_ERR_CLOSED if closed.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_push_for(
    struct bounded_fifo* fifo, const void* elem, int64_t timeout_ns);

/// @brief Removes the oldest element if available; never blocks.
/// @param fifo Pointer to the initialized FIFO.
/// @param out_elem Destination buffer of at least elem_size bytes.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_EMPTY if empty,
///         BOUNDED_FIFO_ERR_CLOSED if empty and closed.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_try_pop(
    struct bounded_fifo* fifo, void* out_elem);

/// @brief Removes the oldest element, blocking while the queue is empty.
/// @param fifo Pointer to the initialized FIFO.
/// @param out_elem Destination buffer of at least elem_size bytes.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_CLOSED once closed and
/// drained.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_pop(
    struct bounded_fifo* fifo, void* out_elem);

/// @brief Removes the oldest element, blocking up to timeout_ns if empty.
/// @param fifo Pointer to the initialized FIFO.
/// @param out_elem Destination buffer of at least elem_size bytes.
/// @param timeout_ns Maximum duration to wait in nanoseconds.
/// @return BOUNDED_FIFO_OK on success, BOUNDED_FIFO_ERR_TIMED_OUT if timeout
/// expired,
///         BOUNDED_FIFO_ERR_CLOSED if closed and drained.
[[nodiscard]] enum bounded_fifo_status bounded_fifo_pop_for(
    struct bounded_fifo* fifo, void* out_elem, int64_t timeout_ns);

/// @brief Stops accepting new elements and wakes all waiting producers and
/// consumers.
/// @param fifo Pointer to the initialized FIFO.
void bounded_fifo_close(struct bounded_fifo* fifo);

/// @brief Returns true if the FIFO is closed (snapshot).
/// @param fifo Pointer to the initialized FIFO.
[[nodiscard]] bool bounded_fifo_is_closed(const struct bounded_fifo* fifo);

/// @brief Returns the current number of queued elements (snapshot).
/// @param fifo Pointer to the initialized FIFO.
[[nodiscard]] size_t bounded_fifo_size(const struct bounded_fifo* fifo);

/// @brief Returns the maximum capacity of the FIFO.
/// @param fifo Pointer to the initialized FIFO.
[[nodiscard]] size_t bounded_fifo_capacity(const struct bounded_fifo* fifo);

/// @brief Returns true if the FIFO has no queued elements (snapshot).
/// @param fifo Pointer to the initialized FIFO.
[[nodiscard]] bool bounded_fifo_is_empty(const struct bounded_fifo* fifo);

/// @brief Returns true if the FIFO is at maximum capacity (snapshot).
/// @param fifo Pointer to the initialized FIFO.
[[nodiscard]] bool bounded_fifo_is_full(const struct bounded_fifo* fifo);

#ifdef __cplusplus
}  // extern "C"
#endif
