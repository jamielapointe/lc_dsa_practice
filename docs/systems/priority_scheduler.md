# PriorityScheduler — Software Requirements

| Field | Value |
| --- | --- |
| Component | `priority_scheduler` (`src/include/systems/priority_scheduler.h`, `src/c/systems/priority_scheduler.c`) |
| Requirement prefix | `SCHED` |
| Status | Draft (TDD red; implementation pending) |
| Integrity context | Practice component written with ISO 26262 / MISRA C:2023 / NVIDIA Halos rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX (production); macOS (development host) |

## 1. Problem statement

> **Priority Queue Based Scheduler**
>
> ## Question
>
> Let's implement a system which can run multiple tasks at different frequencies
>
> ## Detail
>
> Imagine you're developing firmware for an embedded system that needs to handle multiple periodic tasks with varying deadlines and importance. Due to limited
> resources, you need to ensure that critical tasks are executed promptly while still allowing less critical tasks to run.
>
> For example:
>
> - **Task A**: Needs to run every 10 milliseconds to control a motor, with a strict deadline.
> - **Task B**: Should run every 100 milliseconds to collect sensor data.
> - **Task C**: Needs to run every 1 second to update a display.
>
> How would you design a task scheduler that can handle these different frequencies, keeping in mind the constraints of an embedded system? We are looking for
> something that is deterministic and that allows the most important tasks to run on time.
>
> ### Main things interviewers are looking for
>
> - How would the candidate (TC) represent the tasks and their deadlines in memory?
> - How would they determine which task to execute next?
> - What data structure would they use to efficiently manage the tasks and their deadlines?
>
> ## Follow Up Questions
>
> 1. How would TC handle tasks with the same deadline?
> 2. How would they handle a task missing its deadline?
> 3. How would they handle the multithreading and multiple requests at the same time?
> 4. How can they minimize the scope of the lock?
> 5. What if each task also has a priority?
> 6. We can also ask TC how to implement a priority queue from scratch (for C, they probably will need to)

## 2. Context and intent

Real-time robotics safety controllers and embedded executives (such as NVIDIA Halos safety islands, IGX Thor functional safety supervisors, and MCU
motor-control loops) must multiplex periodic control, sensing, and telemetry tasks running at harmonic or non-harmonic frequencies (e.g., 100 Hz motor
actuation, 10 Hz sensor fusion, 1 Hz HMI/diagnostics). On resource-constrained targets, the scheduler must determine the next runnable task in $O(1)$ peek /
$O(\log N)$ update time using a fixed-capacity binary min-heap without dynamic memory allocation (`malloc`/`free`), recursion, or variable-length arrays, break
deadline ties deterministically by task urgency and registration order, recover cleanly from missed deadlines without catch-up burst storms, and release its
internal mutex before executing task callbacks so long-running tasks never block concurrent producers or worker threads.

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | Track focus is strict ISO C23 (`src/include/systems/priority_scheduler.h`, `src/c/systems/priority_scheduler.c`, `test/c/systems/priority_scheduler_c_test.cpp`, and `test/c/systems/systems_c23_test.c`) | Separate C++23 wrapper class (`extern "C"` header is already dual-language safe) |
| A-2 | Both the custom binary min-heap (`struct priority_queue`, Follow-up 6) and the thread-safe scheduler (`struct priority_scheduler`, Main + Follow-ups 1–5) are exposed in `src/include/systems/priority_scheduler.h` | Hiding the binary min-heap as `static` helpers inside `priority_scheduler.c` (prevents direct unit testing of Follow-up 6 heap invariants) |
| A-3 | Fixed capacity `PRIORITY_SCHEDULER_MAX_TASKS = 64U` is embedded directly inside `struct priority_queue` / `struct priority_scheduler`, allowing placement in `.bss`, on the stack, or in a startup-allocated heap block with zero internal `malloc`/`free` calls | Internal `malloc` during `init` (embedding the array supports `.bss`, stack, and startup heap uniformly without heap fragmentation) |
| A-4 | Heap ordering uses a 3-key total order (`scheduled_task_comes_before`): (1) earliest `next_deadline_ns` (EDF), (2) smaller numeric `priority` (`0` = highest urgency, `255` = lowest, RTOS/Halos convention), and (3) smaller `sequence_id` (registration FIFO order, falling back to `task_id`) | Larger numeric value for higher priority |
| A-5 | When a task is dispatched late (`now_ns > scheduled_deadline_ns`, Follow-up 2), the scheduler still executes the task, sets `missed_deadline = true`, increments `deadline_misses`, and advances `next_deadline_ns` to `now_ns + period_ns` (checked via `ckd_add`) to prevent stale catch-up bursts | Setting `next_deadline_ns = old_deadline_ns + period_ns` (causes back-to-back catch-up bursts after a stall) or dropping the task |
| A-6 | Time is passed explicitly as a monotonic `uint64_t now_ns` parameter to `priority_scheduler_step` and `priority_scheduler_run_until_idle` so unit tests are 100% deterministic and never depend on host OS wall-clock load or sleeps | Calling `clock_gettime` inside the scheduler (causes flaky timing tests on loaded CI runners) |
| A-7 | To minimize lock scope (Follow-up 4), `priority_scheduler_step` pops the due task and re-inserts its rescheduled entry under `sched->mutex`, then **unlocks `sched->mutex` before invoking `task.callback(task.user_data, now_ns)`** | Holding `sched->mutex` across `task.callback` (blocks concurrent callers and deadlocks if a callback calls scheduler APIs) |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| EDF (Earliest Deadline First) | Scheduling policy that selects the task with the smallest `next_deadline_ns` timestamp |
| Static priority | Fixed urgency level `uint8_t priority` (`0` = highest urgency, `255` = lowest) used to break ties when deadlines match |
| Sequence ID | Monotonically increasing registration number (`sequence_id`) used to break ties when both deadline and priority match |
| Missed deadline | Dispatch occurring when `now_ns > scheduled_deadline_ns` |
| Lock scope minimization | Releasing `sched->mutex` prior to executing user callback code so the critical section covers only $O(\log N)$ heap updates |

## 5. Interface overview

The header `src/include/systems/priority_scheduler.h` is authoritative for signatures:

- Constants: `PRIORITY_SCHEDULER_MAX_TASKS = 64U`, `PRIORITY_HIGHEST = 0U`, `PRIORITY_LOWEST = 255U`.
- `enum priority_scheduler_error : int32_t` — status and error codes (`PRIORITY_SCHEDULER_OK`, `PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED`,
  `PRIORITY_SCHEDULER_ERR_NULL_PTR`, `PRIORITY_SCHEDULER_ERR_UNINITIALIZED`, `PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID`,
  `PRIORITY_SCHEDULER_ERR_INVALID_PERIOD`, `PRIORITY_SCHEDULER_ERR_NULL_CALLBACK`, `PRIORITY_SCHEDULER_ERR_QUEUE_FULL`,
  `PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID`, `PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND`, `PRIORITY_SCHEDULER_ERR_EMPTY`,
  `PRIORITY_SCHEDULER_ERR_NO_TASK_READY`, `PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW`) and `priority_scheduler_error_to_string()`.
- `priority_task_fn` — `void (*)(void* user_data, uint64_t now_ns)` callback signature.
- `struct priority_task_config` —
  `{uint32_t task_id; uint8_t priority; uint64_t period_ns; uint64_t first_deadline_ns; priority_task_fn callback; void* user_data;}`.
- `struct scheduled_task` — in-heap task record holding `next_deadline_ns`, `period_ns`, `sequence_id`, `executions`, `deadline_misses`, `task_id`, `priority`,
  `callback`, and `user_data`.
- `struct priority_dispatch_result` — telemetry returned by `priority_scheduler_step` (`task_id`, `priority`, `missed_deadline`, `scheduled_deadline_ns`,
  `dispatched_at_ns`, `next_deadline_ns`, `executions`, `deadline_misses`).
- Binary min-heap primitives (Follow-up 6):
  - `scheduled_task_comes_before(a, b)`
  - `priority_queue_init(pq)`, `priority_queue_push(pq, task)`, `priority_queue_peek(pq, out_task)`, `priority_queue_pop(pq, out_task)`
  - `priority_queue_size(pq)`, `priority_queue_is_empty(pq)`, `priority_queue_is_full(pq)`
- Thread-safe scheduler operations (Main + Follow-ups 1–5):
  - `priority_scheduler_init(sched)` and `priority_scheduler_destroy(sched)`
  - `priority_scheduler_schedule(sched, config)` and `priority_scheduler_cancel(sched, task_id)`
  - `priority_scheduler_peek_next_deadline(sched, out_deadline_ns, out_task_id)`
  - `priority_scheduler_step(sched, now_ns, out_result)`
  - `priority_scheduler_run_until_idle(sched, now_ns, max_dispatches, out_dispatched, out_missed)`
  - `priority_scheduler_size(sched)`

## 6. Functional requirements

### SCHED-REQ-001: Follow-up 6 — Custom binary min-heap priority queue operations

`priority_queue_init`, `priority_queue_push`, `priority_queue_peek`, `priority_queue_pop`, `priority_queue_size`, `priority_queue_is_empty`, and
`priority_queue_is_full` shall implement an array-backed 0-indexed binary min-heap of capacity `PRIORITY_SCHEDULER_MAX_TASKS` (`64`) ordered by
`scheduled_task_comes_before`:

- `priority_queue_init(pq)` sets `pq->size = 0` and returns `PRIORITY_SCHEDULER_OK`.
- `priority_queue_push(pq, task)` inserts `*task` in $O(\log N)$ time via iterative `sift_up`, or returns `PRIORITY_SCHEDULER_ERR_QUEUE_FULL` when
  `pq->size == PRIORITY_SCHEDULER_MAX_TASKS`.
- `priority_queue_peek(pq, out_task)` copies the highest-precedence element (`pq->entries[0]`) into `*out_task` in $O(1)$ time without modifying `pq`, or
  returns `PRIORITY_SCHEDULER_ERR_EMPTY` when `pq->size == 0`.
- `priority_queue_pop(pq, out_task)` copies `pq->entries[0]` into `*out_task`, removes it in $O(\log N)$ time via iterative `sift_down`, and returns
  `PRIORITY_SCHEDULER_OK`, or returns `PRIORITY_SCHEDULER_ERR_EMPTY` when `pq->size == 0`.
- **Verification:** Test

### SCHED-REQ-002: Multi-frequency periodic task scheduling and EDF dispatch

`priority_scheduler_schedule`, `priority_scheduler_step`, and `priority_scheduler_run_until_idle` shall schedule and execute periodic tasks running at
different frequencies (e.g., 10 ms motor control, 100 ms sensor acquisition, and 1 s display update) in Earliest Deadline First (`next_deadline_ns`) order. When
 a task is due (`next_deadline_ns <= now_ns`), `priority_scheduler_step` shall invoke `task.callback(task.user_data, now_ns)`, increment `task.executions` and
`sched->total_dispatches`, and automatically re-enqueue the periodic task for its next deadline.

- **Verification:** Test

### SCHED-REQ-003: Follow-ups 1 & 5 — Deterministic tie-breaking by priority and sequence order

When two or more tasks have the exact same `next_deadline_ns`, `scheduled_task_comes_before(a, b)` and the scheduler shall select the task with the smaller
numeric `priority` (`0` = highest urgency, `255` = lowest urgency). When both `next_deadline_ns` and `priority` are equal, the task with the smaller
registration `sequence_id` (and if `sequence_id` is equal in direct `priority_queue` usage, the smaller `task_id`) shall be selected first.

- **Verification:** Test
- **Rationale:** Deterministic tie-breaking ensures high-criticality control loops (e.g., motor actuation) always preempt lower-criticality tasks (e.g., display
  updates) that land on the same harmonic tick boundary (100 ms, 1000 ms).

### SCHED-REQ-004: Follow-up 2 — Missed-deadline detection, telemetry, and non-bursting reschedule

When `priority_scheduler_step(sched, now_ns, out_result)` dispatches a task at `now_ns > scheduled_deadline_ns`:

1. The task callback shall still be executed with `now_ns`.
2. The task's `deadline_misses` counter and `sched->total_deadline_misses` shall each be incremented by `1`.
3. The task's next deadline shall be advanced to `now_ns + period_ns` (rather than `scheduled_deadline_ns + period_ns`) so that a transient system stall does
   not trigger back-to-back catch-up bursts.
4. When `out_result != nullptr`, `out_result->missed_deadline` shall be `true`, `out_result->scheduled_deadline_ns` shall record the missed deadline,
   `out_result->dispatched_at_ns` shall record `now_ns`, and `out_result->next_deadline_ns` shall record `now_ns + period_ns`.

When dispatched on time (`now_ns == scheduled_deadline_ns`), `missed_deadline` shall be `false` and `next_deadline_ns` shall be
`scheduled_deadline_ns + period_ns`.

- **Verification:** Test
- **Rationale:** Naive `deadline += period` rescheduling after a bus or interrupt stall causes a burst of back-to-back executions that starves other real-time
  tasks and saturates actuators.

### SCHED-REQ-005: Future-task non-dispatch and next-wakeup query

When `sched->ready_queue` is non-empty but the earliest task has `next_deadline_ns > now_ns`, `priority_scheduler_step(sched, now_ns, out_result)` shall not
remove any task or invoke any callback, shall set `out_result->next_deadline_ns = earliest.next_deadline_ns` (and zero all other fields of `*out_result` when
`out_result != nullptr`), and shall return `PRIORITY_SCHEDULER_ERR_NO_TASK_READY`. `priority_scheduler_peek_next_deadline(sched, out_deadline_ns, out_task_id)`
shall store the earliest `next_deadline_ns` (and `task_id` when `out_task_id != nullptr`) and return `PRIORITY_SCHEDULER_OK`, or return
`PRIORITY_SCHEDULER_ERR_EMPTY` when no tasks are queued.

- **Verification:** Test

### SCHED-REQ-006: Task cancellation by ID

`priority_scheduler_cancel(sched, task_id)` shall remove the task with `task_id` from `sched->ready_queue`, restore the binary min-heap invariant among the
remaining tasks, and return `PRIORITY_SCHEDULER_OK`, or return `PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND` when no queued task has `task_id`.

- **Verification:** Test

### SCHED-REQ-007: Bounded batch dispatch (`priority_scheduler_run_until_idle`)

`priority_scheduler_run_until_idle(sched, now_ns, max_dispatches, out_dispatched, out_missed)` shall repeatedly dispatch due tasks
(`next_deadline_ns <= now_ns`) up to `max_dispatches` iterations, stopping as soon as no queued task is due at `now_ns`, the queue becomes empty, or
`max_dispatches` dispatches have completed. It shall store the number of executed tasks in `*out_dispatched`, store the count of those executions that missed
their deadline in `*out_missed` (when `out_missed != nullptr`), and return `PRIORITY_SCHEDULER_OK`.

- **Verification:** Test
- **Rationale:** Bounding the maximum number of dispatches per tick (`max_dispatches`) guarantees a statically analyzable worst-case execution time (WCET) per
  scheduler tick.

## 7. Concurrency requirements

### SCHED-REQ-008: Follow-up 4 — Minimized lock scope (unlocked callback execution)

`priority_scheduler_step` and `priority_scheduler_run_until_idle` shall hold `sched->mutex` only while reading and updating `sched->ready_queue` and internal
telemetry counters, and **shall release `sched->mutex` before invoking `task.callback(task.user_data, now_ns)`**. A task callback executing inside
`priority_scheduler_step` shall be able to call `priority_scheduler_schedule`, `priority_scheduler_cancel`, `priority_scheduler_peek_next_deadline`, and
`priority_scheduler_size` on the same `struct priority_scheduler` instance without deadlocking.

- **Verification:** Test
- **Rationale:** Holding the scheduler lock across arbitrary task callbacks causes priority inversion, blocks concurrent task submissions, and deadlocks on
  re-entrant scheduler calls.

### SCHED-REQ-009: Follow-up 3 — Thread-safe concurrent scheduling and dispatching

All `priority_scheduler_*` operations on an initialized `struct priority_scheduler` shall be thread-safe. Concurrent calls to `priority_scheduler_schedule`,
`priority_scheduler_cancel`, `priority_scheduler_step`, `priority_scheduler_peek_next_deadline`, and `priority_scheduler_size` across multiple producer and
worker threads shall be free of data races and invariant violations under ThreadSanitizer.

- **Verification:** Test (`PrioritySchedulerConcurrencyCTest`, run under TSan)
- **Rationale:** In an embedded robotics OS, sensor/telemetry threads may dynamically register or cancel tasks while real-time worker threads dispatch due
  tasks.

## 8. Resource and memory requirements

### SCHED-REQ-010: Zero internal heap allocation and BSS/stack/startup-heap placement

No function in `priority_scheduler` shall call `malloc`, `calloc`, `realloc`, or `free`. All heap storage (`entries[PRIORITY_SCHEDULER_MAX_TASKS]`) is embedded
directly within `struct priority_queue` and `struct priority_scheduler`, allowing callers to allocate the structure in `.bss`, on the stack, or in a
startup-allocated heap arena with zero runtime heap allocations.

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Dynamic allocation in real-time scheduling paths risks heap fragmentation, non-deterministic latency, and out-of-memory faults (ISO
  26262-6:2018 Table 6 item 1b; MISRA C:2023 Dir 4.12 / Rule 21.3).

### SCHED-REQ-011: Bounded stack, no recursion, and no VLAs

All functions in `priority_scheduler` (including binary min-heap `sift_up` and `sift_down`) shall execute iteratively without recursion (`misc-no-recursion`) or
variable-length arrays (`-Wvla`), using $O(1)$ stack space.

- **Verification:** Review (enforced by `clang-tidy` `misc-no-recursion` and `-Werror=vla`)
- **Rationale:** Recursion and VLAs risk stack overflow on fixed-size RTOS task stacks.

### SCHED-REQ-012: Documented complexity analysis

`src/c/systems/priority_scheduler.c` shall document the worst-case time complexity, auxiliary space complexity, and lock-hold duration of `priority_queue_push`,
`priority_queue_peek`, `priority_queue_pop`, `priority_scheduler_schedule`, `priority_scheduler_cancel`, and `priority_scheduler_step`.

- **Verification:** Review

## 9. Robustness and failure-model requirements

### SCHED-REQ-013: Null pointer and uninitialized state rejection

Every function shall return `PRIORITY_SCHEDULER_ERR_NULL_PTR` when a required pointer parameter is `nullptr` (`pq`, `task`, `out_task`, `sched`, `config`,
`out_deadline_ns`, or `out_dispatched`). Every operational scheduler function (`schedule`, `cancel`, `peek_next_deadline`, `step`, `run_until_idle`) shall
return `PRIORITY_SCHEDULER_ERR_UNINITIALIZED` when `sched->initialized == false`.

- **Verification:** Test

### SCHED-REQ-014: Deterministic validation precedence, capacity, duplicate ID, and overflow checks

`priority_scheduler_schedule(sched, config)` shall validate arguments and state in this exact precedence order:

1. `PRIORITY_SCHEDULER_ERR_NULL_PTR` (`sched == nullptr` or `config == nullptr`)
2. `PRIORITY_SCHEDULER_ERR_UNINITIALIZED` (`!sched->initialized`)
3. `PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID` (`config->task_id == 0U`)
4. `PRIORITY_SCHEDULER_ERR_INVALID_PERIOD` (`config->period_ns == 0ULL`)
5. `PRIORITY_SCHEDULER_ERR_NULL_CALLBACK` (`config->callback == nullptr`)
6. `PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID` (`config->task_id` already exists in `sched->ready_queue`)
7. `PRIORITY_SCHEDULER_ERR_QUEUE_FULL` (`sched->ready_queue.size >= PRIORITY_SCHEDULER_MAX_TASKS`)

When computing a task's next deadline (`now_ns + period_ns`) in `priority_scheduler_step`, if `ckd_add` detects 64-bit unsigned integer overflow, the scheduler
shall leave the task in `sched->ready_queue` unmodified, not invoke the callback, and return `PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW`.

- **Verification:** Test
- **Rationale:** Preventing `period_ns == 0` avoids infinite immediate rescheduling loops; checked addition (`<stdckdint.h>`) prevents 64-bit timestamp
  wrap-around from scheduling a future task at timestamp `0`.

## 10. Portability requirements

### SCHED-REQ-015: Pure ISO C23 portability and dual-language header safety

The component shall be written in strict ISO C23 (`-std=c23`) using `<stdbool.h>`, `<stdckdint.h>`, `<stddef.h>`, `<stdint.h>`, and `<pthread.h>`, and the
public header `src/include/systems/priority_scheduler.h` shall compile cleanly in both ISO C23 and C++23 translation units.

- **Verification:** Test (`systems_c23_test` and `priority_scheduler_c_test`)

## 11. Failure modes (mini-FMEA)

| Failure mode | Effect | Detection / mitigation | Requirement |
| --- | --- | --- | --- |
| Harmonic deadline collision (e.g., 10 ms, 100 ms, 1 s all due at $t = 1000\text{ ms}$) | Low-priority display task delays critical motor control loop | 3-key comparator breaks deadline ties by static `priority` (`0` highest) and `sequence_id` | SCHED-REQ-003 |
| Transient stall delays tick past task deadline (`now_ns > deadline`) | Naive `deadline += period` fires a burst of back-to-back catch-up runs | Advance `next_deadline_ns` to `now_ns + period_ns` and record `deadline_misses` | SCHED-REQ-004 |
| Task registered with `period_ns == 0` | Task reschedules at `now_ns + 0` forever, livelocking `run_until_idle` | Reject `period_ns == 0` with `PRIORITY_SCHEDULER_ERR_INVALID_PERIOD` | SCHED-REQ-014 |
| Holding `sched->mutex` while executing `task.callback` | Long callback blocks other threads; callback calling `schedule`/`cancel` deadlocks | Release `sched->mutex` before calling `task.callback` | SCHED-REQ-008 |
| `now_ns + period_ns` exceeds `UINT64_MAX` | Timestamp wraps around to small value, corrupting EDF order | Use `<stdckdint.h>` `ckd_add` and return `PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW` | SCHED-REQ-014 |

## 12. Standards guidance (informative)

| Topic | Reference | How it applies here |
| --- | --- | --- |
| Dynamic memory | ISO 26262-6:2018 Table 6 (1b); MISRA C:2023 Dir 4.12, Rule 21.3 | Fixed-capacity array embedded in `struct priority_queue`; zero `malloc`/`free` |
| Recursion and bounded stack | ISO 26262-6:2018 Table 6 (1e); MISRA C:2023 Rule 17.2, Rule 18.8 | Iterative binary heap `sift_up` / `sift_down`; no VLAs |
| Integer overflow | CERT C `INT30-C`; ISO C23 `<stdckdint.h>` | `ckd_add` on `now_ns + period_ns` deadline calculations |
| Concurrency & lock scope | CERT C `CON37-C`, `POS54-C`; NVIDIA Halos real-time execution guidance | Short $O(\log N)$ critical sections; callbacks invoked outside mutex |
| Process traceability | ASPICE SWE.1 / SWE.4 | Requirement IDs traced to GoogleTest cases via `RecordProperty` |

Rule numbers are informative pointers; confirm them against licensed copies of the standards before relying on them.

## 13. Out of scope

- Preemptive hardware timer interrupts or OS thread context switching (this component models a deterministic cooperative / worker-polled tick scheduler driven
  by an explicit monotonic `now_ns` clock so it can run deterministically on any host or RTOS).
- Dynamic priority inheritance across external mutexes held by user callbacks.
- Persistent storage of task schedules across power cycles.

## 14. Implementation hints (optional, spoiler-free)

- **0-indexed binary min-heap index math (Follow-up 6):** For a node at index $i \in [0, \text{size})$:
  - $\text{parent}(i) = (i - 1) / 2$ (for $i > 0$)
  - $\text{left}(i) = 2i + 1$
  - $\text{right}(i) = 2i + 2$ In `sift_up(pq, idx)`, loop while `idx > 0` and swap `entries[idx]` with `entries[parent]` whenever
    `scheduled_task_comes_before(&entries[idx], &entries[parent])`. In `sift_down(pq, idx)`, loop while `2 * idx + 1 < pq->size`, pick the higher-precedence
    child (`smallest`), and swap if `scheduled_task_comes_before(&entries[smallest], &entries[idx])`.
- **Arbitrary removal in `priority_scheduler_cancel`:** After finding the index $i$ matching `task_id` via linear scan over `0 .. size - 1`, decrement `size`,
  and if $i < \text{size}$, move `entries[size]` into `entries[i]` and run *both* `sift_up(pq, i)` and `sift_down(pq, i)` (at most one of the two will move the
  element, restoring the heap invariant in $O(\log N)$ steps whether the replacement element had an earlier or later deadline than the removed element).
- **Single-sift reschedule in `priority_scheduler_step`:** When the root task `entries[0]` is due (`entries[0].next_deadline_ns <= now_ns`), you can either
  `priority_queue_pop` + update + `priority_queue_push`, or update `entries[0]` in-place with its new `next_deadline_ns = now_ns + period_ns` and call
  `sift_down` once from index `0` before unlocking `sched->mutex`.
- **Minimizing lock scope (Follow-up 4):** Copy the function pointer `callback`, `user_data`, and the `priority_dispatch_result` fields into local stack
  variables while holding `sched->mutex`, call `pthread_mutex_unlock(&sched->mutex)`, and only then call `callback(user_data, now_ns)`.
