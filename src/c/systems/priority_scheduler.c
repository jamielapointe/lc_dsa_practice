/// @file priority_scheduler.c
/// @brief Deterministic, zero-allocation periodic task scheduler and binary
/// min-heap priority queue in ISO C23.
///
/// Specification: docs/systems/priority_scheduler.md (Prefix SCHED).
///
/// Complexity analysis (SCHED-REQ-012):
///   Let N <= PRIORITY_SCHEDULER_MAX_TASKS (64) be the number of tasks in the
///   binary min-heap:
///   1. Binary min-heap (`priority_queue_*`, Follow-up 6):
///      - `priority_queue_init`: O(capacity) = O(1) bounded time (64 slots),
///        O(1) space.
///      - `priority_queue_push`: O(log N) worst-case time (at most 6 swaps in
///        iterative `pq_sift_up`), O(1) stack space, zero heap allocation.
///      - `priority_queue_peek`: O(1) time, O(1) space.
///      - `priority_queue_pop`: O(log N) worst-case time (at most 6 swaps in
///        iterative `pq_sift_down`), O(1) stack space, zero heap allocation.
///   2. Thread-safe scheduler (`priority_scheduler_*`, Main + Follow-ups 1–5):
///      - `priority_scheduler_schedule`: O(N) duplicate-ID check + O(log N)
///        heap insertion under `sched->mutex`; O(1) stack space.
///      - `priority_scheduler_cancel`: O(N) linear scan for `task_id` +
///        O(log N) `pq_sift_up`/`pq_sift_down` heap restoration under
///        `sched->mutex`; O(1) stack space.
///      - `priority_scheduler_step`: O(log N) lock-hold duration to update root
///        telemetry/deadline in place and `pq_sift_down` from index 0, followed
///        by O(T_callback) unlocked callback execution outside `sched->mutex`
///        (Follow-up 4); O(1) stack space.
///      - `priority_scheduler_run_until_idle`: O(K * log N) total lock-hold
///        time across K <= `max_dispatches` due tasks, with each callback
///        invoked outside `sched->mutex`; O(1) stack space.

#include "systems/priority_scheduler.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Zero-initializes a scheduled_task record (including padding bits).
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
static void clear_scheduled_task(struct scheduled_task* const task) {
  *task = (struct scheduled_task){};
}

/**
 * @brief Zero-initializes a priority_dispatch_result record.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
static void clear_dispatch_result(
    struct priority_dispatch_result* const result) {
  *result = (struct priority_dispatch_result){};
}

/**
 * @brief Swaps two distinct scheduled_task elements in place.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
static void swap_tasks(struct scheduled_task* restrict const a,
                       struct scheduled_task* restrict const b) {
  const struct scheduled_task tmp = *a;
  *a = *b;
  *b = tmp;
}

// ============================================================================
// Binary Min-Heap Invariants and 0-Indexed Complete Binary Tree Mapping
// ============================================================================
// A binary min-heap backed by `pq->entries` (length n = pq->size) enforces two
// invariants simultaneously:
//
// 1. Shape Invariant (Complete Binary Tree):
//    All levels of the tree are completely filled except possibly the bottom
//    level, which is filled strictly from left to right. Valid nodes occupy
//    contiguous array indices 0 through n - 1 with no gaps:
//      - Root node:       index 0 (highest-precedence task when n > 0)
//      - Parent(i):       (i - 1) / 2   for 0 < i < n
//      - Left child(i):   (2 * i) + 1   (present iff (2 * i) + 1 < n)
//      - Right child(i):  (2 * i) + 2   (present iff (2 * i) + 2 < n)
//
// 2. Heap Order Invariant:
//    Every parent node has higher or equal scheduling precedence than both of
//    its children according to `scheduled_task_comes_before` (earliest
//    `next_deadline_ns`, then smallest `priority`, then smallest `sequence_id`
//    / `task_id`). Formally, for every index i where 0 < i < n:
//      !scheduled_task_comes_before(&entries[i], &entries[parent(i)])

/**
 * @brief Computes the 0-indexed parent array index for a non-root heap node.
 *
 * In a 0-indexed complete binary tree, both the left child `(2 * p) + 1` and
 * the right child `(2 * p) + 2` map back to parent index `p` under unsigned
 * integer division `(child_idx - 1) / 2`.
 *
 * @pre `child_idx > 0U`. Calling this helper on the root (`child_idx == 0U`)
 *      underflows unsigned `size_t` arithmetic (`0U - 1U == SIZE_MAX`).
 * @param child_idx Array index of the child node in `pq->entries` (`> 0`).
 * @return 0-indexed array position of the parent node: `(child_idx - 1) / 2`.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
[[nodiscard]] static inline size_t pq_get_parent_idx(const size_t child_idx) {
  return (child_idx - 1U) / 2U;
}

/**
 * @brief Computes the 0-indexed left-child array index for a heap node.
 *
 * By the complete binary tree shape invariant (nodes packed contiguously in
 * `0 .. pq->size - 1` from left to right), the left child of `parent_idx`
 * resides at `(2 * parent_idx) + 1`. The returned index refers to a valid node
 * if and only if it is strictly less than `pq->size`; otherwise `parent_idx`
 * is a leaf node (and therefore has no right child either). For bounded heap
 * indices `parent_idx < PRIORITY_SCHEDULER_MAX_TASKS` (64),
 * `(2 * parent_idx) + 1 <= 127` and cannot overflow `size_t`.
 *
 * @param parent_idx Array index of the parent node in `pq->entries`.
 * @return Candidate 0-indexed position of the left child:
 *         `(2 * parent_idx) + 1`.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
[[nodiscard]] static inline size_t pq_get_left_child_idx(
    const size_t parent_idx) {
  return (2U * parent_idx) + 1U;
}

/**
 * @brief Computes the 0-indexed right-child array index for a heap node.
 *
 * In a 0-indexed complete binary tree, the right child immediately follows the
 * left child at `(2 * parent_idx) + 2`. The returned index refers to a valid
 * node if and only if it is strictly less than `pq->size` (which, by the
 * left-to-right shape invariant, also guarantees that the left child at
 * `(2 * parent_idx) + 1` exists). For bounded heap indices
 * `parent_idx < PRIORITY_SCHEDULER_MAX_TASKS` (64),
 * `(2 * parent_idx) + 2 <= 128` and cannot overflow `size_t`.
 *
 * @param parent_idx Array index of the parent node in `pq->entries`.
 * @return Candidate 0-indexed position of the right child:
 *         `(2 * parent_idx) + 2`.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
[[nodiscard]] static inline size_t pq_get_right_child_idx(
    const size_t parent_idx) {
  return (2U * parent_idx) + 2U;
}

/**
 * @brief Restores min-heap order by sifting element at @p insert_idx toward the
 * root.
 *
 * Time Complexity:  O(log N) where N = pq->size <= 64 (at most 6 swaps)
 * Space Complexity: O(1) auxiliary stack space (iterative, no recursion)
 *
 * Walks ancestor indices along the rootward path and swaps whenever the child
 * precedes its parent according to `scheduled_task_comes_before`.
 */
static void pq_sift_up(struct priority_queue* const pq, size_t insert_idx) {
  while (insert_idx > 0U) {
    const size_t parent_idx = pq_get_parent_idx(insert_idx);
    if (!scheduled_task_comes_before(&pq->entries[insert_idx],
                                     &pq->entries[parent_idx])) {
      break;
    }
    swap_tasks(&pq->entries[insert_idx], &pq->entries[parent_idx]);
    insert_idx = parent_idx;
  }
}

/**
 * @brief Restores min-heap order by sifting element at @p idx toward the
 * leaves.
 *
 * Time Complexity:  O(log N) where N = pq->size <= 64 (at most 6 swaps)
 * Space Complexity: O(1) auxiliary stack space (iterative, no recursion)
 *
 * At each level, selects the higher-precedence child (`smallest`) and swaps if
 * `smallest` precedes the current node `idx` under
 * `scheduled_task_comes_before`.
 */
static void pq_sift_down(struct priority_queue* const pq, size_t idx) {
  for (;;) {
    const size_t left = pq_get_left_child_idx(idx);
    if (left >= pq->size) {
      break;
    }
    const size_t right = pq_get_right_child_idx(idx);
    size_t smallest = left;
    if (right < pq->size &&
        scheduled_task_comes_before(&pq->entries[right], &pq->entries[left])) {
      smallest = right;
    }
    if (!scheduled_task_comes_before(&pq->entries[smallest],
                                     &pq->entries[idx])) {
      break;
    }
    swap_tasks(&pq->entries[idx], &pq->entries[smallest]);
    idx = smallest;
  }
}

[[nodiscard]] const char* priority_scheduler_error_to_string(
    const enum priority_scheduler_error error) {
  switch (error) {
    case PRIORITY_SCHEDULER_OK:
      return "ok";
    case PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED:
      return "not implemented";
    case PRIORITY_SCHEDULER_ERR_NULL_PTR:
      return "null pointer argument";
    case PRIORITY_SCHEDULER_ERR_UNINITIALIZED:
      return "scheduler is not initialized";
    case PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID:
      return "invalid task_id (must be non-zero)";
    case PRIORITY_SCHEDULER_ERR_INVALID_PERIOD:
      return "invalid period_ns (must be > 0)";
    case PRIORITY_SCHEDULER_ERR_NULL_CALLBACK:
      return "task callback must not be null";
    case PRIORITY_SCHEDULER_ERR_QUEUE_FULL:
      return "priority queue is at maximum capacity (64)";
    case PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID:
      return "task_id is already scheduled";
    case PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND:
      return "task_id not found in scheduler";
    case PRIORITY_SCHEDULER_ERR_EMPTY:
      return "priority queue is empty";
    case PRIORITY_SCHEDULER_ERR_NO_TASK_READY:
      return "no scheduled task is due at now_ns";
    case PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW:
      return "timestamp addition overflowed uint64_t";
  }
  return "unknown priority_scheduler_error";
}

[[nodiscard]] bool scheduled_task_comes_before(
    const struct scheduled_task* const a,
    const struct scheduled_task* const b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  if (a->next_deadline_ns != b->next_deadline_ns) {
    return a->next_deadline_ns < b->next_deadline_ns;
  }
  if (a->priority != b->priority) {
    return a->priority < b->priority;
  }
  if (a->sequence_id != b->sequence_id) {
    return a->sequence_id < b->sequence_id;
  }
  return a->task_id < b->task_id;
}

[[nodiscard]] enum priority_scheduler_error priority_queue_init(
    struct priority_queue* const pq) {
  if (pq == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  for (size_t i = 0U; i < PRIORITY_SCHEDULER_MAX_TASKS; ++i) {
    clear_scheduled_task(&pq->entries[i]);
  }
  pq->size = 0U;
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_queue_push(
    struct priority_queue* restrict const pq,
    const struct scheduled_task* restrict const task) {
  if (pq == nullptr || task == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (pq->size >= PRIORITY_SCHEDULER_MAX_TASKS) {
    return PRIORITY_SCHEDULER_ERR_QUEUE_FULL;
  }
  const size_t insert_idx = pq->size;
  pq->entries[insert_idx] = *task;
  ++pq->size;
  pq_sift_up(pq, insert_idx);
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_queue_peek(
    const struct priority_queue* restrict const pq,
    struct scheduled_task* restrict const out_task) {
  if (pq == nullptr || out_task == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (pq->size == 0U) {
    return PRIORITY_SCHEDULER_ERR_EMPTY;
  }
  *out_task = pq->entries[0];
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_queue_pop(
    struct priority_queue* restrict const pq,
    struct scheduled_task* restrict const out_task) {
  if (pq == nullptr || out_task == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (pq->size == 0U) {
    return PRIORITY_SCHEDULER_ERR_EMPTY;
  }
  *out_task = pq->entries[0];
  --pq->size;
  if (pq->size > 0U) {
    pq->entries[0] = pq->entries[pq->size];
    clear_scheduled_task(&pq->entries[pq->size]);
    pq_sift_down(pq, 0U);
  } else {
    clear_scheduled_task(&pq->entries[0]);
  }
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] size_t priority_queue_size(
    const struct priority_queue* const pq) {
  return (pq == nullptr) ? 0U : pq->size;
}

[[nodiscard]] bool priority_queue_is_empty(
    const struct priority_queue* const pq) {
  return priority_queue_size(pq) == 0U;
}

[[nodiscard]] bool priority_queue_is_full(
    const struct priority_queue* const pq) {
  return priority_queue_size(pq) >= PRIORITY_SCHEDULER_MAX_TASKS;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_init(
    struct priority_scheduler* const sched) {
  if (sched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  sched->initialized = false;
  (void)priority_queue_init(&sched->ready_queue);
  sched->next_sequence_id = 0ULL;
  sched->total_dispatches = 0ULL;
  sched->total_deadline_misses = 0ULL;
  if (pthread_mutex_init(&sched->mutex, nullptr) != 0) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  sched->initialized = true;
  return PRIORITY_SCHEDULER_OK;
}

void priority_scheduler_destroy(struct priority_scheduler* const sched) {
  if (sched == nullptr || !sched->initialized) {
    return;
  }
  (void)pthread_mutex_destroy(&sched->mutex);
  (void)priority_queue_init(&sched->ready_queue);
  sched->next_sequence_id = 0ULL;
  sched->total_dispatches = 0ULL;
  sched->total_deadline_misses = 0ULL;
  sched->initialized = false;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_schedule(
    struct priority_scheduler* restrict const sched,
    const struct priority_task_config* restrict const config) {
  if (sched == nullptr || config == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  if (config->task_id == 0U) {
    return PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID;
  }
  if (config->period_ns == 0ULL) {
    return PRIORITY_SCHEDULER_ERR_INVALID_PERIOD;
  }
  if (config->callback == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_CALLBACK;
  }

  (void)pthread_mutex_lock(&sched->mutex);
  for (size_t i = 0U; i < sched->ready_queue.size; ++i) {
    if (sched->ready_queue.entries[i].task_id == config->task_id) {
      (void)pthread_mutex_unlock(&sched->mutex);
      return PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID;
    }
  }
  if (sched->ready_queue.size >= PRIORITY_SCHEDULER_MAX_TASKS) {
    (void)pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_QUEUE_FULL;
  }

  const struct scheduled_task task = {
      .next_deadline_ns = config->first_deadline_ns,
      .period_ns = config->period_ns,
      .sequence_id = sched->next_sequence_id++,
      .executions = 0ULL,
      .deadline_misses = 0ULL,
      .task_id = config->task_id,
      .priority = config->priority,
      .callback = config->callback,
      .user_data = config->user_data,
  };
  const enum priority_scheduler_error err =
      priority_queue_push(&sched->ready_queue, &task);
  (void)pthread_mutex_unlock(&sched->mutex);
  return err;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_cancel(
    struct priority_scheduler* const sched, const uint32_t task_id) {
  if (sched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  if (task_id == 0U) {
    return PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID;
  }

  (void)pthread_mutex_lock(&sched->mutex);
  for (size_t i = 0U; i < sched->ready_queue.size; ++i) {
    if (sched->ready_queue.entries[i].task_id == task_id) {
      --sched->ready_queue.size;
      if (i < sched->ready_queue.size) {
        sched->ready_queue.entries[i] =
            sched->ready_queue.entries[sched->ready_queue.size];
        clear_scheduled_task(
            &sched->ready_queue.entries[sched->ready_queue.size]);
        pq_sift_up(&sched->ready_queue, i);
        pq_sift_down(&sched->ready_queue, i);
      } else {
        clear_scheduled_task(&sched->ready_queue.entries[i]);
      }
      (void)pthread_mutex_unlock(&sched->mutex);
      return PRIORITY_SCHEDULER_OK;
    }
  }
  (void)pthread_mutex_unlock(&sched->mutex);
  return PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND;
}

[[nodiscard]] enum priority_scheduler_error
priority_scheduler_peek_next_deadline(
    struct priority_scheduler* restrict const sched,
    uint64_t* restrict const out_deadline_ns,
    uint32_t* restrict const out_task_id) {
  if (sched == nullptr || out_deadline_ns == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }

  (void)pthread_mutex_lock(&sched->mutex);
  if (sched->ready_queue.size == 0U) {
    (void)pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_EMPTY;
  }
  *out_deadline_ns = sched->ready_queue.entries[0].next_deadline_ns;
  if (out_task_id != nullptr) {
    *out_task_id = sched->ready_queue.entries[0].task_id;
  }
  (void)pthread_mutex_unlock(&sched->mutex);
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_step(
    struct priority_scheduler* restrict const sched, const uint64_t now_ns,
    struct priority_dispatch_result* restrict const out_result) {
  if (out_result != nullptr) {
    clear_dispatch_result(out_result);
  }
  if (sched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }

  (void)pthread_mutex_lock(&sched->mutex);
  if (sched->ready_queue.size == 0U) {
    (void)pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_EMPTY;
  }

  const uint64_t scheduled_deadline_ns =
      sched->ready_queue.entries[0].next_deadline_ns;
  if (scheduled_deadline_ns > now_ns) {
    if (out_result != nullptr) {
      out_result->next_deadline_ns = scheduled_deadline_ns;
    }
    (void)pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_NO_TASK_READY;
  }

  uint64_t next_deadline_ns = 0ULL;
  if (ckd_add(&next_deadline_ns, now_ns,
              sched->ready_queue.entries[0].period_ns)) {
    (void)pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW;
  }

  struct scheduled_task* const root = &sched->ready_queue.entries[0];
  const bool missed_deadline = (now_ns > scheduled_deadline_ns);
  if (missed_deadline) {
    ++root->deadline_misses;
    ++sched->total_deadline_misses;
  }
  ++root->executions;
  root->next_deadline_ns = next_deadline_ns;
  ++sched->total_dispatches;

  const priority_task_fn callback = root->callback;
  void* const user_data = root->user_data;
  const uint32_t task_id = root->task_id;
  const uint8_t priority = root->priority;
  const uint64_t executions = root->executions;
  const uint64_t deadline_misses = root->deadline_misses;

  pq_sift_down(&sched->ready_queue, 0U);
  (void)pthread_mutex_unlock(&sched->mutex);

  callback(user_data, now_ns);

  if (out_result != nullptr) {
    out_result->task_id = task_id;
    out_result->priority = priority;
    out_result->missed_deadline = missed_deadline;
    out_result->scheduled_deadline_ns = scheduled_deadline_ns;
    out_result->dispatched_at_ns = now_ns;
    out_result->next_deadline_ns = next_deadline_ns;
    out_result->executions = executions;
    out_result->deadline_misses = deadline_misses;
  }
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_run_until_idle(
    struct priority_scheduler* restrict const sched, const uint64_t now_ns,
    const size_t max_dispatches, size_t* restrict const out_dispatched,
    size_t* restrict const out_missed) {
  if (sched == nullptr || out_dispatched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  *out_dispatched = 0U;
  if (out_missed != nullptr) {
    *out_missed = 0U;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }

  struct priority_dispatch_result step_result = {};
  for (size_t i = 0U; i < max_dispatches; ++i) {
    const enum priority_scheduler_error step_err =
        priority_scheduler_step(sched, now_ns, &step_result);
    if (step_err == PRIORITY_SCHEDULER_ERR_EMPTY ||
        step_err == PRIORITY_SCHEDULER_ERR_NO_TASK_READY) {
      break;
    }
    if (step_err != PRIORITY_SCHEDULER_OK) {
      return step_err;
    }
    if (step_result.missed_deadline && out_missed != nullptr) {
      ++(*out_missed);
    }
    ++(*out_dispatched);
  }
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] size_t priority_scheduler_size(
    struct priority_scheduler* const sched) {
  if (sched == nullptr || !sched->initialized) {
    return 0U;
  }
  (void)pthread_mutex_lock(&sched->mutex);
  const size_t size = sched->ready_queue.size;
  (void)pthread_mutex_unlock(&sched->mutex);
  return size;
}
