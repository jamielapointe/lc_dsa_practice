# ProcessRendezvous — Software Requirements

| Field | Value |
| --- | --- |
| Component | `systems::ProcessRendezvous` (`src/include/systems/process_rendezvous.hpp`) |
| Requirement prefix | `PRV` |
| Status | Implemented (POSIX reference implementation for Linux and macOS Apple Silicon) |
| Integrity context | Practice component written with ISO 26262 (ASIL-B-style) rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX (production); macOS (development host, see Portability) |

## 1. Problem statement

> Coordinate initialization between separate producer and consumer processes using an OS-agnostic API backed by platform-specific primitives on macOS and
> Linux, ensuring the consumer is attached and waiting before the producer begins its periodic publishing loop.

## 2. Context and intent

When a producer process and one or more consumer processes communicate over a lock-free shared-memory channel (`systems::ShmChannel`), startup ordering is
otherwise a race: a consumer launched first cannot open the channel until the producer creates it, while a producer launched first would publish early samples
before the consumer attaches its subscriber cursor. `ProcessRendezvous` provides a two-stage cross-process startup latch (`ProducerReady` and `ConsumerReady`)
plus an orderly `close()` path. It is used exclusively during initialization (and optional teardown) so the steady-state data loop remains completely free of
coordination or system calls.

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | Two monotoniclatch stages (`ProducerReady` and `ConsumerReady`), each transitioning once from `0` to `1` | Multi-phase cyclic barrier (unneeded for startup handshakes and complicates crash recovery) |
| A-2 | Shared state lives in a cache-line-aligned lock-free atomic header in POSIX shared memory; OS wakeup uses POSIX named semaphores (`sem_timedwait` on Linux, `sem_trywait` + bounded backoff on macOS) | `pthread_mutex_t` + `pthread_cond_t` in shared memory (rejected: macOS lacks `PTHREAD_MUTEX_ROBUST`, so a `SIGKILL`ed holder would deadlock survivors) |
| A-3 | Either participant may create or open the rendezvous (`attach(name, timeout)` or explicit `create` / `open`) | Requiring a third supervisor process to pre-create the rendezvous |
| A-4 | Signaling is idempotent and broadcast-capable: once a stage is ready, any number of current or future waiters observe readiness immediately | Single-consumer semaphore decrement without sticky state |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| Rendezvous | Named cross-process synchronization object holding the `ProducerReady`, `ConsumerReady`, and `closed` flags |
| ProducerReady | Stage 1 latch: signaled once the producer has created and initialized the shared-memory data channel |
| ConsumerReady | Stage 2 latch: signaled once the consumer has opened the channel, attached its subscriber, and is ready to receive |

## 5. Interface overview

- `ProcessRendezvousError` — error enumeration (`kNotImplemented`, `kInvalidArgument`, `kNameTooLong`, `kAlreadyExists`, `kNotFound`, `kLayoutMismatch`,
  `kWouldBlock`, `kTimedOut`, `kClosed`, `kMemoryLockFailed`, `kOsError`), `to_string()`.
- `ProcessRendezvous::create(name)`, `ProcessRendezvous::open(name)`, `ProcessRendezvous::attach(name, timeout)`, `ProcessRendezvous::unlink(name)`.
- Producer stage: `signal_producer_ready()`, `is_producer_ready()`, `try_wait_producer_ready()`, `wait_producer_ready_for(timeout)`, `wait_producer_ready()`.
- Consumer stage: `signal_consumer_ready()`, `is_consumer_ready()`, `try_wait_consumer_ready()`, `wait_consumer_ready_for(timeout)`, `wait_consumer_ready()`.
- Lifecycle: `close()`, `is_closed()`.

## 6. Functional requirements

### PRV-REQ-001: Create, open, and attach to a named rendezvous

`ProcessRendezvous::create(name)` shall create and initialize a new rendezvous with both stages unset (`false`) and `is_closed() == false`, returning
`kAlreadyExists` if `name` already exists. `ProcessRendezvous::open(name)` shall map an existing initialized rendezvous or return `kNotFound` when missing and
`kLayoutMismatch` when the segment header is invalid. `ProcessRendezvous::attach(name, timeout)` shall create the rendezvous if absent or open it once
initialized within `timeout`.

- **Verification:** Test
- **Rationale:** Allows producer and consumer processes to start in any order without racing on object creation.

### PRV-REQ-002: Validate rendezvous names

Rendezvous names shall start with `/`, contain no other `/` or NUL characters, and have at least one character after `/`; otherwise `kInvalidArgument` shall be
returned. Names longer than `systems::platform::kMaxShmNameLength` shall return `kNameTooLong`.

- **Verification:** Test
- **Rationale:** Enforces portable POSIX object names across Linux, QNX, and macOS (31-character kernel limit).

### PRV-REQ-003: Unlink a rendezvous name

`ProcessRendezvous::unlink(name)` shall remove the rendezvous name so later `open` calls return `kNotFound`, while already-open handles stay usable; unlinking a
missing name shall return `kNotFound`.

- **Verification:** Test

### PRV-REQ-004: Producer readiness signaling and waiting

`signal_producer_ready()` shall mark the producer stage ready (idempotently) and wake all waiters. `try_wait_producer_ready()` shall return `{}` when ready and
`kWouldBlock` when not ready (or `kClosed` if closed before becoming ready). `wait_producer_ready_for(timeout)` shall return `{}` within 100 ms of signaling or
`kTimedOut` if `timeout` elapses first. `wait_producer_ready()` shall block until ready or closed.

- **Verification:** Test

### PRV-REQ-005: Consumer readiness signaling and waiting

`signal_consumer_ready()` shall mark the consumer stage ready (idempotently) and wake all waiters. `try_wait_consumer_ready()` shall return `{}` when ready and
`kWouldBlock` when not ready (or `kClosed` if closed before becoming ready). `wait_consumer_ready_for(timeout)` shall return `{}` within 100 ms of signaling or
`kTimedOut` if `timeout` elapses first. `wait_consumer_ready()` shall block until ready or closed.

- **Verification:** Test

### PRV-REQ-006: Close wakes waiters and rejects post-close signaling

After `close()` is called on any handle, `is_closed()` shall return `true`, subsequent `signal_producer_ready()` and `signal_consumer_ready()` calls shall
return `kClosed`, and any blocked wait on an unsignaled stage shall return `kClosed` within 100 ms. A stage that was signaled before `close()` shall continue to
report ready (`{}`) to waiters.

- **Verification:** Test
- **Rationale:** Prevents processes from hanging indefinitely during startup if the peer aborts or shuts down early.

## 7. Concurrency requirements

### PRV-REQ-007: Works across threads and processes

Independent threads and separate processes opening the rendezvous by name shall observe `ProducerReady`, `ConsumerReady`, and `close()` transitions without
lost wakeups or data races.

- **Verification:** Test

## 8. Resource and memory requirements

### PRV-REQ-008: No heap allocation after initialization

`signal_producer_ready`, `signal_consumer_ready`, `is_producer_ready`, `is_consumer_ready`, `try_wait_producer_ready`, `try_wait_consumer_ready`,
`wait_producer_ready_for`, `wait_consumer_ready_for`, `wait_producer_ready`, `wait_consumer_ready`, `close`, and `is_closed` shall complete without heap
allocation.

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Avoids dynamic memory allocation after initialization (ISO 26262-6:2018 Table 6 item 1b; MISRA C++:2023 Rule 21.6.1).

### PRV-REQ-009: Handle lifetime and noexcept contract

`ProcessRendezvous` shall be move-only with `noexcept` move construction and assignment, and every public operation shall be `noexcept`. Destroying a handle
shall unmap memory and close OS descriptors without unlinking the name.

- **Verification:** Test

## 9. Robustness and failure-model requirements

### PRV-REQ-010: Survive a participant killed at any point

If a process holding a `ProcessRendezvous` handle is killed (`SIGKILL`) at any instruction, surviving processes shall never deadlock; another process shall
still be able to signal readiness, wait with a bounded timeout, or close the rendezvous.

- **Verification:** Test
- **Rationale:** Lock-free atomic state + kernel semaphore wakeups hold no cross-process lock that could be orphaned on crash.

## 10. Portability requirements

### PRV-REQ-011: OS calls are isolated in the POSIX adapter

All operating-system calls (`shm_open`, `mmap`, `sem_open`, `sem_post`, `sem_timedwait`, `sem_trywait`, `sem_close`, `sem_unlink`) shall be confined to
`src/cpp/systems/process_rendezvous_posix.cpp`; `src/cpp/systems/process_rendezvous.cpp` shall not include POSIX headers, and platform differences shall branch
on `systems/platform.hpp` (`kHasPosixSemTimedwait`).

- **Verification:** Review

### PRV-REQ-012: Shared layout is address-free

Everything placed in shared memory shall be trivially copyable data or `std::atomic` types with `is_always_lock_free == true`, aligned to
`systems::platform::kCacheLineSize`; no pointers, vtables, `std::mutex`, or `std::condition_variable` shall be stored in the shared segment.

- **Verification:** Review (`static_assert` on the layout types)

## 11. Failure modes (mini-FMEA)

| Failure mode | Effect | Detection / mitigation | Requirement |
| --- | --- | --- | --- |
| Consumer starts before producer creates channel | Consumer fails to open channel | `attach()` + `wait_producer_ready_for()` blocks until channel is created | PRV-REQ-001, PRV-REQ-004 |
| Producer publishes before consumer attaches | Early frames missed before subscriber cursor is set | Producer calls `wait_consumer_ready_for()` before entering publish loop | PRV-REQ-005 |
| Peer crashes during startup | Waiter hangs forever | Lock-free state + bounded `wait_*_for(timeout)` and `close()` wakeup | PRV-REQ-006, PRV-REQ-010 |
| Foreign or uninitialized segment | Waiter reads garbage flags | Magic number and layout version checked with acquire/release ordering | PRV-REQ-001, PRV-REQ-012 |

## 12. Standards guidance (informative)

| Topic | Reference | How it applies here |
| --- | --- | --- |
| Dynamic memory | ISO 26262-6:2018 Table 6 (1b); MISRA C++:2023 Rule 21.6.1; AUTOSAR C++14 A18-5-1 | Heap only in init phase |
| Exceptions | Project policy (see `safety.md`) | `noexcept` API, `std::expected` errors |
| Process | ASPICE SWE.1 / SWE.4 | This spec, `RecordProperty` tags, `pixi run check-traceability` |
