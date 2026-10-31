/// @file priority_scheduler.c
/// @brief Deterministic, zero-allocation periodic task scheduler and binary
/// min-heap priority queue in ISO C23 (TDD red stubs).
///
/// Specification: docs/systems/priority_scheduler.md (Prefix SCHED).
///
/// Complexity analysis (SCHED-REQ-012):
///   Let N <= PRIORITY_SCHEDULER_MAX_TASKS (64) be the number of tasks in the
///   binary min-heap:
///   1. Binary min-heap (`priority_queue_*`, Follow-up 6):
///      - `priority_queue_init`: O(1) time, O(1) space.
///      - `priority_queue_push`: O(log N) worst-case time (at most 6 swaps in
///        iterative `sift_up`), O(1) stack space, zero heap allocation.
///      - `priority_queue_peek`: O(1) time, O(1) space.
///      - `priority_queue_pop`: O(log N) worst-case time (at most 6 swaps in
///        iterative `sift_down`), O(1) stack space, zero heap allocation.
///   2. Thread-safe scheduler (`priority_scheduler_*`, Main + Follow-ups 1–5):
///      - `priority_scheduler_schedule`: O(N) duplicate-ID check + O(log N)
///        heap insertion under `sched->mutex`; O(1) stack space.
///      - `priority_scheduler_cancel`: O(N) linear scan for `task_id` +
///        O(log N) `sift_up`/`sift_down` heap restoration under `sched->mutex`.
///      - `priority_scheduler_step`: O(log N) lock-hold duration to pop/update
///        and reschedule the root task, followed by O(T_callback) unlocked
///        callback execution outside `sched->mutex` (Follow-up 4).
///      - `priority_scheduler_run_until_idle`: O(K * log N) total lock-hold
///        time across K <= `max_dispatches` due tasks, with each callback
///        invoked outside `sched->mutex`.

#include "systems/priority_scheduler.h"

#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Zero-initializes a scheduled_task record.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
static void clear_scheduled_task(struct scheduled_task* const task) {
  *task = (struct scheduled_task){
      .next_deadline_ns = 0ULL,
      .period_ns = 0ULL,
      .sequence_id = 0ULL,
      .deadline_misses = 0ULL,
      .task_id = 0U,
      .priority = 0U,
      .callback = nullptr,
      .user_data = nullptr,
  };
}

/**
 * @brief Zero-initializes a dispatch_result record.
 *
 * Time Complexity:  O(1)
 * Space Complexity: O(1)
 */
static void clear_dispatch_result(
    struct priority_dispatch_result* const result) {
  *result = (struct priority_dispatch_result){
      .task_id = 0U,
      .priority = 0U,
      .missed_deadline = false,
      .scheduled_deadline_ns = 0ULL,
      .dispatched_at_ns = 0ULL,
      .next_deadline_ns = 0ULL,
      .executions = 0ULL,
      .deadline_misses = 0ULL,
  };
}

/**
 * @brief Swaps two scheduled_task elements in place.
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
[[nodiscard]] static inline size_t pq_get_parent_idx(size_t child_idx) {
  return (child_idx - 1) / 2;
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
[[nodiscard]] static inline size_t pq_get_left_child_idx(size_t parent_idx) {
  return (2 * parent_idx) + 1;
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
[[nodiscard]] static inline size_t pq_get_right_child_idx(size_t parent_idx) {
  return (2 * parent_idx) + 2;
}

/**
 * @brief Restores min-heap order by sifting element at @p idx toward the root.
 *
 * Time Complexity:  O(log N) where N = pq->size <= 64 (at most 6 swaps)
 * Space Complexity: O(1) auxiliary stack space (iterative, no recursion)
 *
 * As this is an internal function we guarantee that pq is NOT null!
 *
 * Performs a binary search with swaps to restore the min-heap property.
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
    if (!scheduled_task_comes_before(&pq->entries[idx],
                                     &pq->entries[smallest])) {
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
    const struct scheduled_task* restrict const a,
    const struct scheduled_task* restrict const b) {
  // TODO(SCHED-REQ-003):
  // 1. Return false if `a == nullptr` or `b == nullptr`.
  // 2. Compare `a->next_deadline_ns` vs `b->next_deadline_ns` (smaller first).
  // 3. On tie, compare `a->priority` vs `b->priority` (smaller numeric value =
  //    higher urgency first, 0 highest).
  // 4. On tie, compare `a->sequence_id` vs `b->sequence_id` (smaller first).
  // 5. On tie, compare `a->task_id` vs `b->task_id` (smaller first).
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
  // TODO(SCHED-REQ-001):
  // 1. Validate `pq != nullptr` (return `PRIORITY_SCHEDULER_ERR_NULL_PTR`).
  // 2. Set `pq->size = 0U` and return `PRIORITY_SCHEDULER_OK`.
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
  // TODO(SCHED-REQ-001):
  // 1. Validate `pq != nullptr` and `task != nullptr`
  //    (`PRIORITY_SCHEDULER_ERR_NULL_PTR`).
  // 2. Check `pq->size < PRIORITY_SCHEDULER_MAX_TASKS`
  //    (`PRIORITY_SCHEDULER_ERR_QUEUE_FULL`).
  // 3. Place `*task` at `pq->entries[pq->size]`, increment `pq->size`, and
  //    restore the min-heap invariant with an iterative `sift_up`.
  if (pq == nullptr || task == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (pq->size >= PRIORITY_SCHEDULER_MAX_TASKS) {
    return PRIORITY_SCHEDULER_ERR_QUEUE_FULL;
  }
  size_t insert_idx = pq->size;
  pq->entries[insert_idx] = *task;
  ++pq->size;
  pq_sift_up(pq, insert_idx);
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_queue_peek(
    const struct priority_queue* restrict const pq,
    struct scheduled_task* restrict const out_task) {
  // TODO(SCHED-REQ-001):
  // 1. Validate `pq != nullptr` and `out_task != nullptr`
  //    (`PRIORITY_SCHEDULER_ERR_NULL_PTR`).
  // 2. If `pq->size == 0U`, return `PRIORITY_SCHEDULER_ERR_EMPTY`.
  // 3. Copy `pq->entries[0]` to `*out_task` and return `PRIORITY_SCHEDULER_OK`.
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
    struct priority_queue* const pq, struct scheduled_task* const out_task) {
  // TODO(SCHED-REQ-001):
  // 1. Validate `pq != nullptr` and `out_task != nullptr`
  //    (`PRIORITY_SCHEDULER_ERR_NULL_PTR`).
  // 2. If `pq->size == 0U`, return `PRIORITY_SCHEDULER_ERR_EMPTY`.
  // 3. Copy `pq->entries[0]` to `*out_task`, decrement `pq->size`, move the
  //    last entry to index 0 (when `pq->size > 0U`), and restore the min-heap
  //    invariant with an iterative `sift_down`.
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
    pq_sift_down(pq, 0U);
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
  // TODO(SCHED-REQ-002):
  // 1. Validate `sched != nullptr` (`PRIORITY_SCHEDULER_ERR_NULL_PTR`).
  // 2. Initialize `sched->ready_queue`, counters (`next_sequence_id = 0`,
  //    `total_dispatches = 0`, `total_deadline_misses = 0`), and
  //    `pthread_mutex_init(&sched->mutex, nullptr)`.
  // 3. Set `sched->initialized = true` and return `PRIORITY_SCHEDULER_OK`.
  if (sched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  enum priority_scheduler_error err = priority_queue_init(&sched->ready_queue);
  if (err != PRIORITY_SCHEDULER_OK) {
    return err;
  }
  sched->next_sequence_id = 0U;
  sched->total_dispatches = 0U;
  sched->total_deadline_misses = 0U;
  pthread_mutex_init(&sched->mutex, nullptr);
  sched->initialized = true;
  return PRIORITY_SCHEDULER_OK;
}

void priority_scheduler_destroy(struct priority_scheduler* const sched) {
  // TODO(SCHED-REQ-009):
  // 1. If `sched == nullptr` or `!sched->initialized`, return immediately.
  // 2. Destroy `sched->mutex`, reset `sched->ready_queue.size = 0U`, and set
  //    `sched->initialized = false`.
  if (sched == nullptr || !sched->initialized) {
    return;
  }
  pthread_mutex_destroy(&sched->mutex);
  sched->ready_queue.size = 0U;
  sched->initialized = false;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_schedule(
    struct priority_scheduler* const sched,
    const struct priority_task_config* const config) {
  // TODO(SCHED-REQ-002, SCHED-REQ-013, SCHED-REQ-014):
  // 1. Enforce exact validation precedence:
  //    - `sched == nullptr || config == nullptr` -> `ERR_NULL_PTR`
  //    - `!sched->initialized` -> `ERR_UNINITIALIZED`
  //    - `config->task_id == 0U` -> `ERR_INVALID_TASK_ID`
  //    - `config->period_ns == 0ULL` -> `ERR_INVALID_PERIOD`
  //    - `config->callback == nullptr` -> `ERR_NULL_CALLBACK`
  // 2. Lock `sched->mutex`.
  // 3. Reject duplicate `config->task_id` (`ERR_DUPLICATE_TASK_ID`) and full
  //    queue (`ERR_QUEUE_FULL`).
  // 4. Construct a `struct scheduled_task` with `sequence_id =
  //    sched->next_sequence_id++`, push into `sched->ready_queue`, unlock
  //    `sched->mutex`, and return `PRIORITY_SCHEDULER_OK`.
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
  pthread_mutex_lock(&sched->mutex);
  for (size_t i = 0U; i < sched->ready_queue.size; ++i) {
    if (sched->ready_queue.entries[i].task_id == config->task_id) {
      pthread_mutex_unlock(&sched->mutex);
      return PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID;
    }
  }
  if (sched->ready_queue.size >= PRIORITY_SCHEDULER_MAX_TASKS) {
    pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_QUEUE_FULL;
  }
  struct scheduled_task task = {
      .task_id = config->task_id,
      .period_ns = config->period_ns,
      .callback = config->callback,
      .sequence_id = sched->next_sequence_id++,
      .next_deadline_ns = config->period_ns,
  };
  enum priority_scheduler_error err =
      priority_queue_push(&sched->ready_queue, &task);
  pthread_mutex_unlock(&sched->mutex);
  return err;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_cancel(
    struct priority_scheduler* const sched, const uint32_t task_id) {
  // TODO(SCHED-REQ-006, SCHED-REQ-013):
  // 1. Validate `sched != nullptr` (`ERR_NULL_PTR`), `sched->initialized`
  //    (`ERR_UNINITIALIZED`), and `task_id != 0U` (`ERR_INVALID_TASK_ID`).
  // 2. Lock `sched->mutex`, locate `task_id` in `sched->ready_queue`, replace
  //    with the last heap entry, and restore the min-heap invariant using both
  //    `sift_up` and `sift_down`.
  // 3. Unlock `sched->mutex` and return `PRIORITY_SCHEDULER_OK` (or
  //    `ERR_TASK_NOT_FOUND` if absent).
  if (sched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  if (task_id == 0U) {
    return PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID;
  }
  pthread_mutex_lock(&sched->mutex);
  for (size_t i = 0U; i < sched->ready_queue.size; ++i) {
    if (sched->ready_queue.entries[i].task_id == task_id) {
      --sched->ready_queue.size;
      if (sched->ready_queue.size > 0U) {
        sched->ready_queue.entries[i] =
            sched->ready_queue.entries[sched->ready_queue.size];
        pq_sift_up(&sched->ready_queue, i);
        pq_sift_down(&sched->ready_queue, i);
      }
      pthread_mutex_unlock(&sched->mutex);
      return PRIORITY_SCHEDULER_OK;
    }
  }
  pthread_mutex_unlock(&sched->mutex);
  return PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND;
}

[[nodiscard]] enum priority_scheduler_error
priority_scheduler_peek_next_deadline(struct priority_scheduler* const sched,
                                      uint64_t* const out_deadline_ns,
                                      uint32_t* const out_task_id) {
  // TODO(SCHED-REQ-005, SCHED-REQ-013):
  // 1. Validate `sched != nullptr` and `out_deadline_ns != nullptr`
  //    (`ERR_NULL_PTR`), and `sched->initialized` (`ERR_UNINITIALIZED`).
  // 2. Lock `sched->mutex`, check if `ready_queue` is empty (`ERR_EMPTY`),
  //    read `entries[0].next_deadline_ns` (and `entries[0].task_id` if
  //    `out_task_id != nullptr`), unlock `sched->mutex`, and return `OK`.
  if (sched == nullptr || out_deadline_ns == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  pthread_mutex_lock(&sched->mutex);
  if (sched->ready_queue.size == 0) {
    pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_EMPTY;
  }
  *out_deadline_ns = sched->ready_queue.entries[0].next_deadline_ns;
  if (out_task_id != nullptr) {
    *out_task_id = sched->ready_queue.entries[0].task_id;
  }
  pthread_mutex_unlock(&sched->mutex);
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_step(
    struct priority_scheduler* const sched, const uint64_t now_ns,
    struct priority_dispatch_result* const out_result) {
  // TODO(SCHED-REQ-002, SCHED-REQ-004, SCHED-REQ-005, SCHED-REQ-008,
  //      SCHED-REQ-014):
  // 1. Validate `sched != nullptr` (`ERR_NULL_PTR`) and `sched->initialized`
  //    (`ERR_UNINITIALIZED`).
  // 2. Lock `sched->mutex`.
  // 3. If `ready_queue` is empty, unlock and return `ERR_EMPTY`.
  // 4. If `ready_queue.entries[0].next_deadline_ns > now_ns`, populate
  //    `out_result->next_deadline_ns` (if `out_result != nullptr`), unlock, and
  //    return `ERR_NO_TASK_READY`.
  // 5. Compute `next_deadline = now_ns + task.period_ns` using `ckd_add`; if it
  //    overflows, unlock without modifying the queue and return
  //    `ERR_TIME_OVERFLOW`.
  // 6. Pop the root task, record `missed_deadline = (now_ns >
  //    scheduled_deadline_ns)`, update `executions` and `deadline_misses`,
  //    update `task.next_deadline_ns = next_deadline`, and re-insert `task`
  //    into `ready_queue`.
  // 7. Unlock `sched->mutex` BEFORE calling `task.callback(task.user_data,
  //    now_ns)` (Follow-up 4).
  // 8. Write `*out_result` (if non-null) and return `PRIORITY_SCHEDULER_OK`.
  if (out_result != nullptr) {
    clear_dispatch_result(out_result);
  }
  if (sched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  pthread_mutex_lock(&sched->mutex);
  if (sched->ready_queue.size == 0) {
    pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_EMPTY;
  }
  if (sched->ready_queue.entries[0].next_deadline_ns > now_ns) {
    if (out_result != nullptr) {
      out_result->next_deadline_ns =
          sched->ready_queue.entries[0].next_deadline_ns;
    }
    pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_NO_TASK_READY;
  }
  uint64_t next_deadline_ns = 0ULL;
  uint64_t scheduled_deadline_ns =
      sched->ready_queue.entries[0].next_deadline_ns;
  bool ckd_add_result = ckd_add(&next_deadline_ns, now_ns,
                                sched->ready_queue.entries[0].period_ns);
  if (!ckd_add_result) {
    pthread_mutex_unlock(&sched->mutex);
    return PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW;
  }
  struct scheduled_task task_to_run;
  enum priority_scheduler_error pq_pop_err =
      priority_queue_pop(&sched->ready_queue, &task_to_run);
  if (pq_pop_err != PRIORITY_SCHEDULER_OK) {
    pthread_mutex_unlock(&sched->mutex);
    return pq_pop_err;
  }
  if (now_ns > task_to_run.next_deadline_ns) {
    task_to_run.deadline_misses++;
    sched->total_deadline_misses++;
  }
  task_to_run.executions++;
  task_to_run.next_deadline_ns = next_deadline_ns;

  enum priority_scheduler_error pq_push_err =
      priority_queue_push(&sched->ready_queue, &task_to_run);
  if (pq_push_err != PRIORITY_SCHEDULER_OK) {
    pthread_mutex_unlock(&sched->mutex);
    return pq_push_err;
  }
  sched->total_dispatches++;
  sched->next_sequence_id = sched->ready_queue.entries[0].sequence_id;
  pthread_mutex_unlock(&sched->mutex);
  task_to_run.callback(task_to_run.user_data, now_ns);
  if (out_result != nullptr) {
    out_result->task_id = task_to_run.task_id;
    out_result->priority = task_to_run.priority;
    out_result->missed_deadline = now_ns > scheduled_deadline_ns;
    out_result->scheduled_deadline_ns = scheduled_deadline_ns;
    out_result->dispatched_at_ns = now_ns;
    out_result->next_deadline_ns = next_deadline_ns;
    out_result->executions = task_to_run.executions;
    out_result->deadline_misses = task_to_run.deadline_misses;
  }
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] enum priority_scheduler_error priority_scheduler_run_until_idle(
    struct priority_scheduler* const sched, const uint64_t now_ns,
    const size_t max_dispatches, size_t* const out_dispatched,
    size_t* const out_missed) {
  // TODO(SCHED-REQ-007, SCHED-REQ-008, SCHED-REQ-013):
  // 1. Validate `sched != nullptr` and `out_dispatched != nullptr`
  //    (`ERR_NULL_PTR`), and `sched->initialized` (`ERR_UNINITIALIZED`).
  // 2. Loop at most `max_dispatches` times calling `priority_scheduler_step`;
  //    stop cleanly on `ERR_EMPTY` or `ERR_NO_TASK_READY`, propagate any error
  //    such as `ERR_TIME_OVERFLOW`, and store the counts in `*out_dispatched`
  //    and `*out_missed`.
  if (sched == nullptr || out_dispatched == nullptr) {
    return PRIORITY_SCHEDULER_ERR_NULL_PTR;
  }
  if (!sched->initialized) {
    return PRIORITY_SCHEDULER_ERR_UNINITIALIZED;
  }
  if (max_dispatches == 0) {
    return PRIORITY_SCHEDULER_OK;
  }
  struct priority_dispatch_result out_result;
  *out_dispatched = 0U;
  if (out_missed != nullptr) {
    *out_missed = 0U;
  }
  for (size_t i = 0; i < max_dispatches; i++) {
    enum priority_scheduler_error step_err =
        priority_scheduler_step(sched, now_ns, &out_result);
    if (step_err == PRIORITY_SCHEDULER_ERR_EMPTY ||
        step_err == PRIORITY_SCHEDULER_ERR_NO_TASK_READY) {
      break;
    }
    if (step_err != PRIORITY_SCHEDULER_OK) {
      return step_err;
    }
    if (out_result.missed_deadline && out_missed != nullptr) {
      *out_missed = *out_missed + 1U;
    }
    *out_dispatched = *out_dispatched + 1U;
  }
  return PRIORITY_SCHEDULER_OK;
}

[[nodiscard]] size_t priority_scheduler_size(
    struct priority_scheduler* const sched) {
  // TODO(SCHED-REQ-002):
  // Return 0 if `sched == nullptr` or `!sched->initialized`; otherwise lock
  // `sched->mutex`, read `sched->ready_queue.size`, unlock, and return it.
  if (sched == nullptr) {
    return 0U;
  }
  if (!sched->initialized) {
    return 0;
  }
  pthread_mutex_lock(&sched->mutex);
  size_t size = sched->ready_queue.size;
  pthread_mutex_unlock(&sched->mutex);
  return size;
}
