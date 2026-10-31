#pragma once

/// @file priority_scheduler.h
/// @brief Deterministic, zero-allocation periodic task scheduler and binary
/// min-heap priority queue in ISO C23 for embedded/robotics OS environments.
///
/// Specification: docs/systems/priority_scheduler.md (Prefix SCHED).
///
/// Problem summary:
///   Manage multiple periodic firmware/robotics tasks running at different
///   frequencies (e.g., 10 ms motor control, 100 ms sensor acquisition, 1 s
///   display update) with varying deadlines and static priorities on a
///   resource-constrained embedded system:
///   - Represent tasks and their deadlines deterministically in fixed memory
///     (`.bss`, stack, or startup-allocated heap arena; SCHED-REQ-010).
///   - Implement a 0-indexed binary min-heap priority queue from scratch in
///     strict ISO C23 (`struct priority_queue`; Follow-up 6, SCHED-REQ-001).
///   - Select the next task to execute by Earliest Deadline First
///     (`next_deadline_ns`), breaking equal-deadline ties by smaller numeric
///     `priority` (`0` = highest urgency, `255` = lowest; Follow-ups 1 & 5,
///     SCHED-REQ-003) and then by registration `sequence_id` / `task_id`.
///   - Detect and handle missed deadlines (`now_ns > next_deadline_ns`;
///     Follow-up 2, SCHED-REQ-004) by executing the late task, recording
///     telemetry (`deadline_misses`), and advancing `next_deadline_ns` to
///     `now_ns + period_ns` so transient stalls never trigger catch-up bursts.
///   - Support thread-safe concurrent task scheduling, cancellation, and
///     dispatch across threads (`pthread_mutex_t`; Follow-up 3, SCHED-REQ-009)
///     while minimizing lock scope by releasing the mutex before executing the
///     task callback (Follow-up 4, SCHED-REQ-008).
///
/// Time model (non-flaky deterministic testing):
///   - Rather than querying host wall-clock timers, dispatch functions accept
///     an explicit monotonic timestamp `uint64_t now_ns` from the caller.
///   - In production firmware, the tick loop passes `clock_gettime` or hardware
///     timer counter values; in unit tests, `now_ns` is stepped synthetically
///     so tests are 100% deterministic and insensitive to CI runner load.
///
/// Validation precedence for `priority_scheduler_schedule` (SCHED-REQ-013,
/// SCHED-REQ-014):
///   1. `PRIORITY_SCHEDULER_ERR_NULL_PTR` (`sched == nullptr` or
///      `config == nullptr`)
///   2. `PRIORITY_SCHEDULER_ERR_UNINITIALIZED` (`!sched->initialized`)
///   3. `PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID` (`config->task_id == 0U`)
///   4. `PRIORITY_SCHEDULER_ERR_INVALID_PERIOD` (`config->period_ns == 0ULL`)
///   5. `PRIORITY_SCHEDULER_ERR_NULL_CALLBACK` (`config->callback == nullptr`)
///   6. `PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID` (`config->task_id` already
///      present in `sched->ready_queue`)
///   7. `PRIORITY_SCHEDULER_ERR_QUEUE_FULL`
///      (`sched->ready_queue.size >= PRIORITY_SCHEDULER_MAX_TASKS`)
///
/// Thread safety:
///   - `priority_queue_*` functions operate on a standalone `priority_queue`
///     without locking (caller-synchronized or single-threaded).
///   - `priority_scheduler_*` functions are thread-safe and synchronize access
///     via `sched->mutex` (SCHED-REQ-009).
///   - `priority_scheduler_step` and `priority_scheduler_run_until_idle`
///     release `sched->mutex` before invoking `task.callback` (SCHED-REQ-008).
///
/// Memory model:
///   - Zero internal heap allocation (`malloc`/`free` forbidden;
///     SCHED-REQ-010).
///   - All storage (`entries[64]`) is embedded directly inside
///     `struct priority_queue` and `struct priority_scheduler`, supporting
///     `.bss`, stack, or startup-heap allocation uniformly.
///   - Iterative algorithms only; no recursion (`misc-no-recursion`) and no
///     VLAs (`-Wvla`; SCHED-REQ-011).
///
/// Implementation outline:
///   1. Implement `scheduled_task_comes_before(a, b)` with the 3-key total
///      order: `(next_deadline_ns, priority, sequence_id, task_id)`.
///   2. Implement the binary min-heap primitives (`priority_queue_init`,
///      `priority_queue_push`, `priority_queue_peek`, `priority_queue_pop`,
///      `priority_queue_size`, `priority_queue_is_empty`,
///      `priority_queue_is_full`) using iterative `sift_up` and `sift_down`.
///   3. Implement `priority_scheduler_init` and `priority_scheduler_destroy`
///      initializing and destroying `sched->mutex` and `sched->ready_queue`.
///   4. Implement `priority_scheduler_schedule` and `priority_scheduler_cancel`
///      with strict precondition ordering and $O(\log N)$ heap restoration on
///      removal.
///   5. Implement `priority_scheduler_step` and
///      `priority_scheduler_run_until_idle`: check `now_ns` against the
///      min-heap root under `sched->mutex`, check `ckd_add(&next_deadline,
///      now_ns, task.period_ns)` for overflow, update counters and reschedule
///      the task in the heap, unlock `sched->mutex`, and then invoke
///      `callback(user_data, now_ns)` outside the lock.
///   Pitfalls to avoid:
///   - Holding `sched->mutex` while calling `task.callback` (causes deadlock
///     when a callback calls `priority_scheduler_schedule` / `cancel` and
///     violates Follow-up 4).
///   - Rescheduling a missed deadline (`now_ns > deadline`) as
///     `deadline + period_ns` instead of `now_ns + period_ns`, which causes
///     stale catch-up bursts after a stall.
///   - Only running `sift_down` (and forgetting `sift_up`) when replacing an
///     arbitrary element during `priority_scheduler_cancel`.
///   - Unchecked `now_ns + period_ns` addition wrapping around `UINT64_MAX`.
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

/// @brief Maximum number of concurrent periodic tasks in a queue or scheduler.
static constexpr size_t PRIORITY_SCHEDULER_MAX_TASKS = 64U;

/// @brief Highest task urgency level (smallest numeric value wins ties).
static constexpr uint8_t PRIORITY_HIGHEST = 0U;

/// @brief Lowest task urgency level.
static constexpr uint8_t PRIORITY_LOWEST = 255U;

/// @brief Status and error codes returned by priority queue and scheduler APIs.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum priority_scheduler_error : int32_t {
  PRIORITY_SCHEDULER_OK = 0,
  PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED = 1,
  PRIORITY_SCHEDULER_ERR_NULL_PTR = 2,
  PRIORITY_SCHEDULER_ERR_UNINITIALIZED = 3,
  PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID = 4,
  PRIORITY_SCHEDULER_ERR_INVALID_PERIOD = 5,
  PRIORITY_SCHEDULER_ERR_NULL_CALLBACK = 6,
  PRIORITY_SCHEDULER_ERR_QUEUE_FULL = 7,
  PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID = 8,
  PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND = 9,
  PRIORITY_SCHEDULER_ERR_EMPTY = 10,
  PRIORITY_SCHEDULER_ERR_NO_TASK_READY = 11,
  PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW = 12,
} priority_scheduler_error;

static_assert(sizeof(enum priority_scheduler_error) == 4U,
              "ABI requires 32-bit priority_scheduler_error");

/// @brief Task callback invoked when a scheduled task is dispatched.
/// @param user_data Caller-supplied context pointer from
/// `priority_task_config`.
/// @param now_ns Monotonic timestamp (nanoseconds) at which the task was
/// dispatched.
typedef void (*priority_task_fn)(void* user_data, uint64_t now_ns);

/// @brief Configuration parameters for registering a periodic task.
struct priority_task_config {
  uint32_t task_id;    ///< Non-zero unique task identifier.
  uint8_t priority;    ///< Static urgency (0 = highest, 255 = lowest).
  uint64_t period_ns;  ///< Execution period in nanoseconds (must be > 0).
  uint64_t first_deadline_ns;  ///< Monotonic timestamp (ns) of first deadline.
  priority_task_fn callback;   ///< Non-null callback executed on dispatch.
  void* user_data;             ///< Opaque caller context passed to `callback`.
};

/// @brief In-memory representation of a scheduled periodic task inside the
/// binary min-heap.
struct scheduled_task {
  uint64_t
      next_deadline_ns;      ///< Primary sort key: next absolute deadline (ns).
  uint64_t period_ns;        ///< Execution period in nanoseconds (> 0).
  uint64_t sequence_id;      ///< Registration order tie-breaker (FIFO).
  uint64_t executions;       ///< Total times this task has been dispatched.
  uint64_t deadline_misses;  ///< Times dispatched with `now_ns > deadline`.
  uint32_t task_id;          ///< Non-zero unique task identifier.
  uint8_t priority;          ///< Secondary sort key (0 = highest urgency).
  priority_task_fn callback;  ///< Non-null callback function.
  void* user_data;            ///< Opaque caller context pointer.
};

/// @brief Telemetry snapshot produced by a single `priority_scheduler_step`
/// call.
struct priority_dispatch_result {
  uint32_t task_id;  ///< Dispatched task ID (0 if none ran).
  uint8_t priority;  ///< Priority of the dispatched task.
  bool
      missed_deadline;  ///< True if `dispatched_at_ns > scheduled_deadline_ns`.
  uint64_t scheduled_deadline_ns;  ///< Deadline for which the task was due.
  uint64_t dispatched_at_ns;       ///< `now_ns` when the task was dispatched.
  uint64_t next_deadline_ns;  ///< Next deadline (or earliest queued deadline on
                              ///< `NO_TASK_READY`).
  uint64_t executions;        ///< Cumulative executions for this task.
  uint64_t deadline_misses;   ///< Cumulative deadline misses for this task.
};

/// @brief Fixed-capacity array-backed 0-indexed binary min-heap of tasks
/// (Follow-up 6).
/// @details Embeds all storage inside the struct so it can reside in `.bss`,
/// on the stack, or inside a startup-allocated heap block with zero internal
/// dynamic allocation.
struct priority_queue {
  size_t size;
  struct scheduled_task entries[PRIORITY_SCHEDULER_MAX_TASKS];
};

/// @brief Thread-safe priority-queue-based periodic task scheduler.
struct priority_scheduler {
  struct priority_queue ready_queue;
  uint64_t next_sequence_id;
  uint64_t total_dispatches;
  uint64_t total_deadline_misses;
  bool initialized;
  pthread_mutex_t mutex;
};

/// @brief Returns a human-readable string describing a
/// `priority_scheduler_error`.
/// @param error Status or error code.
/// @return Non-null static string literal describing `error`.
/// @par Thread safety
/// Pure function; safe to call concurrently from any thread.
[[nodiscard]] const char* priority_scheduler_error_to_string(
    enum priority_scheduler_error error);

/// @brief Returns `true` if task `a` has strictly higher scheduling precedence
/// than task `b` in the min-heap.
/// @details Ordering keys (SCHED-REQ-003):
///   1. Smaller `next_deadline_ns` (Earliest Deadline First).
///   2. Smaller numeric `priority` (`0` = highest urgency, `255` = lowest).
///   3. Smaller `sequence_id` (earlier registration order).
///   4. Smaller `task_id` (deterministic fallback when `sequence_id` matches).
/// @param a First task pointer (returns `false` if `a == nullptr` or
/// `b == nullptr`).
/// @param b Second task pointer.
/// @return `true` if `*a` should be ordered before `*b`.
/// @par Requirements
/// SCHED-REQ-001, SCHED-REQ-003, SCHED-REQ-013
[[nodiscard]] bool scheduled_task_comes_before(
    const struct scheduled_task* restrict a,
    const struct scheduled_task* restrict b);

/// @brief Initializes an empty binary min-heap priority queue (Follow-up 6).
/// @param pq Pointer to the priority queue to initialize.
/// @return `PRIORITY_SCHEDULER_OK` on success, or
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `pq == nullptr`.
/// @par Requirements
/// SCHED-REQ-001, SCHED-REQ-010, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error priority_queue_init(
    struct priority_queue* pq);

/// @brief Inserts a task into the binary min-heap in O(log N) time
/// (Follow-up 6).
/// @param pq Pointer to the priority queue.
/// @param task Pointer to the task entry to copy into the heap.
/// @return `PRIORITY_SCHEDULER_OK` on success,
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `pq == nullptr` or `task == nullptr`,
/// or `PRIORITY_SCHEDULER_ERR_QUEUE_FULL` if `pq->size >= 64`.
/// @par Requirements
/// SCHED-REQ-001, SCHED-REQ-003, SCHED-REQ-010, SCHED-REQ-011, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error priority_queue_push(
    struct priority_queue* restrict pq,
    const struct scheduled_task* restrict task);

/// @brief Copies the highest-precedence task at the root of the min-heap
/// without removing it in O(1) time (Follow-up 6).
/// @param pq Pointer to the priority queue.
/// @param out_task Destination for the root task.
/// @return `PRIORITY_SCHEDULER_OK` on success,
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `pq == nullptr` or
/// `out_task == nullptr`, or `PRIORITY_SCHEDULER_ERR_EMPTY` if `pq->size == 0`.
/// @par Requirements
/// SCHED-REQ-001, SCHED-REQ-003, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error priority_queue_peek(
    const struct priority_queue* restrict pq,
    struct scheduled_task* restrict out_task);

/// @brief Removes and copies the highest-precedence task from the root of the
/// min-heap in O(log N) time (Follow-up 6).
/// @param pq Pointer to the priority queue.
/// @param out_task Destination for the removed root task.
/// @return `PRIORITY_SCHEDULER_OK` on success,
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `pq == nullptr` or
/// `out_task == nullptr`, or `PRIORITY_SCHEDULER_ERR_EMPTY` if `pq->size == 0`.
/// @par Requirements
/// SCHED-REQ-001, SCHED-REQ-003, SCHED-REQ-010, SCHED-REQ-011, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error priority_queue_pop(
    struct priority_queue* restrict pq,
    struct scheduled_task* restrict out_task);

/// @brief Returns the number of tasks currently in `pq` (or `0` if `pq` is
/// `nullptr`).
/// @par Requirements
/// SCHED-REQ-001
[[nodiscard]] size_t priority_queue_size(const struct priority_queue* pq);

/// @brief Returns `true` if `pq` is `nullptr` or contains zero tasks.
/// @par Requirements
/// SCHED-REQ-001
[[nodiscard]] bool priority_queue_is_empty(const struct priority_queue* pq);

/// @brief Returns `true` if `pq` is non-null and `pq->size == 64`.
/// @par Requirements
/// SCHED-REQ-001
[[nodiscard]] bool priority_queue_is_full(const struct priority_queue* pq);

/// @brief Initializes a thread-safe priority scheduler in caller-provided
/// storage (`.bss`, stack, or startup heap).
/// @param sched Pointer to the scheduler instance to initialize.
/// @return `PRIORITY_SCHEDULER_OK` on success, or
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `sched == nullptr`.
/// @par Requirements
/// SCHED-REQ-002, SCHED-REQ-009, SCHED-REQ-010, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error priority_scheduler_init(
    struct priority_scheduler* sched);

/// @brief Destroys the synchronization primitives inside `sched` and marks it
/// uninitialized. Safe to call with `nullptr` or an uninitialized struct.
/// @param sched Pointer to the scheduler instance to destroy.
/// @par Requirements
/// SCHED-REQ-009, SCHED-REQ-010
void priority_scheduler_destroy(struct priority_scheduler* sched);

/// @brief Registers a periodic task in the scheduler in O(log N) time.
/// @param sched Pointer to an initialized scheduler.
/// @param config Pointer to the task configuration.
/// @return `PRIORITY_SCHEDULER_OK` on success, or the first applicable error in
/// validation precedence order (`NULL_PTR`, `UNINITIALIZED`, `INVALID_TASK_ID`,
/// `INVALID_PERIOD`, `NULL_CALLBACK`, `DUPLICATE_TASK_ID`, `QUEUE_FULL`).
/// @par Thread safety
/// Thread-safe; acquires `sched->mutex`.
/// @par Requirements
/// SCHED-REQ-002, SCHED-REQ-003, SCHED-REQ-009, SCHED-REQ-010, SCHED-REQ-011,
/// SCHED-REQ-013, SCHED-REQ-014
[[nodiscard]] enum priority_scheduler_error priority_scheduler_schedule(
    struct priority_scheduler* restrict sched,
    const struct priority_task_config* restrict config);

/// @brief Cancels and removes a scheduled task by its `task_id`.
/// @param sched Pointer to an initialized scheduler.
/// @param task_id Non-zero task identifier to remove.
/// @return `PRIORITY_SCHEDULER_OK` on success,
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `sched == nullptr`,
/// `PRIORITY_SCHEDULER_ERR_UNINITIALIZED` if not initialized,
/// `PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID` if `task_id == 0`, or
/// `PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND` if `task_id` is not queued.
/// @par Thread safety
/// Thread-safe; acquires `sched->mutex`.
/// @par Requirements
/// SCHED-REQ-006, SCHED-REQ-009, SCHED-REQ-011, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error priority_scheduler_cancel(
    struct priority_scheduler* sched, uint32_t task_id);

/// @brief Queries the earliest `next_deadline_ns` (and optionally `task_id`) in
/// the scheduler without removing the task.
/// @param sched Pointer to an initialized scheduler.
/// @param out_deadline_ns Non-null output pointer for the earliest deadline.
/// @param out_task_id Optional output pointer for the earliest task's ID (may
/// be `nullptr`).
/// @return `PRIORITY_SCHEDULER_OK` on success,
/// `PRIORITY_SCHEDULER_ERR_NULL_PTR` if `sched == nullptr` or
/// `out_deadline_ns == nullptr`, `PRIORITY_SCHEDULER_ERR_UNINITIALIZED` if not
/// initialized, or `PRIORITY_SCHEDULER_ERR_EMPTY` if no tasks are queued.
/// @par Thread safety
/// Thread-safe; acquires `sched->mutex`.
/// @par Requirements
/// SCHED-REQ-005, SCHED-REQ-009, SCHED-REQ-013
[[nodiscard]] enum priority_scheduler_error
priority_scheduler_peek_next_deadline(struct priority_scheduler* restrict sched,
                                      uint64_t* restrict out_deadline_ns,
                                      uint32_t* restrict out_task_id);

/// @brief Dispatches at most one highest-precedence task that is due at or
/// before `now_ns` (`next_deadline_ns <= now_ns`).
/// @details Lock scope minimization (Follow-up 4, SCHED-REQ-008):
///   1. Acquires `sched->mutex`.
///   2. Checks for an empty queue (`PRIORITY_SCHEDULER_ERR_EMPTY`) or future
///      root deadline (`next_deadline_ns > now_ns` ->
///      `PRIORITY_SCHEDULER_ERR_NO_TASK_READY`).
///   3. Verifies `now_ns + period_ns` does not overflow `uint64_t` via
///      `ckd_add` (`PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW`).
///   4. Updates execution and missed-deadline counters, advances the task's
///      `next_deadline_ns = now_ns + period_ns`, and re-inserts the task into
///      `sched->ready_queue`.
///   5. **Releases `sched->mutex`** and invokes `callback(user_data, now_ns)`
///      outside the lock.
/// @param sched Pointer to an initialized scheduler.
/// @param now_ns Current monotonic timestamp in nanoseconds.
/// @param out_result Optional output telemetry snapshot (may be `nullptr`).
/// @return `PRIORITY_SCHEDULER_OK` when a due task was dispatched,
/// `PRIORITY_SCHEDULER_ERR_NO_TASK_READY` when queued tasks exist but none has
/// `next_deadline_ns <= now_ns`, `PRIORITY_SCHEDULER_ERR_EMPTY` when no tasks
/// are queued, or an error code (`NULL_PTR`, `UNINITIALIZED`, `TIME_OVERFLOW`).
/// @par Thread safety
/// Thread-safe; releases `sched->mutex` before executing `callback`.
/// @par Requirements
/// SCHED-REQ-002, SCHED-REQ-003, SCHED-REQ-004, SCHED-REQ-005, SCHED-REQ-008,
/// SCHED-REQ-009, SCHED-REQ-010, SCHED-REQ-011, SCHED-REQ-013, SCHED-REQ-014
[[nodiscard]] enum priority_scheduler_error priority_scheduler_step(
    struct priority_scheduler* restrict sched, uint64_t now_ns,
    struct priority_dispatch_result* restrict out_result);

/// @brief Repeatedly dispatches due tasks (`next_deadline_ns <= now_ns`) up to
/// `max_dispatches` times at timestamp `now_ns`.
/// @param sched Pointer to an initialized scheduler.
/// @param now_ns Current monotonic timestamp in nanoseconds.
/// @param max_dispatches Maximum number of task executions to perform in this
/// call (may be `0`).
/// @param out_dispatched Non-null output count of tasks executed during this
/// call.
/// @param out_missed Optional output count of executed tasks that missed their
/// deadline during this call (may be `nullptr`).
/// @return `PRIORITY_SCHEDULER_OK` when all due tasks (up to `max_dispatches`)
/// have been dispatched or when the queue is empty / has no ready tasks, or an
/// error code (`NULL_PTR`, `UNINITIALIZED`, `TIME_OVERFLOW`).
/// @par Thread safety
/// Thread-safe; each dispatched callback runs with `sched->mutex` unlocked.
/// @par Requirements
/// SCHED-REQ-002, SCHED-REQ-003, SCHED-REQ-004, SCHED-REQ-007, SCHED-REQ-008,
/// SCHED-REQ-009, SCHED-REQ-010, SCHED-REQ-011, SCHED-REQ-013, SCHED-REQ-014
[[nodiscard]] enum priority_scheduler_error priority_scheduler_run_until_idle(
    struct priority_scheduler* restrict sched, uint64_t now_ns,
    size_t max_dispatches, size_t* restrict out_dispatched,
    size_t* restrict out_missed);

/// @brief Returns a thread-safe snapshot of the number of scheduled tasks in
/// `sched` (or `0` if `sched == nullptr` or uninitialized).
/// @par Requirements
/// SCHED-REQ-002, SCHED-REQ-008, SCHED-REQ-009
[[nodiscard]] size_t priority_scheduler_size(struct priority_scheduler* sched);

#ifdef __cplusplus
}  // extern "C"
#endif
