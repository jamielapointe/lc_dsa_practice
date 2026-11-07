# BoundedFifo — Software Requirements

| Field | Value |
| --- | --- |
| Component | `systems::BoundedFifo` (`src/include/systems/bounded_fifo.hpp`, `src/rust/systems/src/bounded_fifo.rs`) |
| Requirement prefix | `FIFO` |
| Status | Implemented (C++23 & Rust 1.99.1+) |
| Integrity context | Practice component written with ISO 26262 / MISRA C++:2023 / safety-critical systems rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX, bare-metal / RTOS; macOS (development host) |

## 1. Problem statement

> Implement a fixed-capacity, thread-safe (multi-producer multi-consumer) first-in first-out (FIFO) queue with zero dynamic memory allocation
> during steady-state operations, deterministic triad operations (`try_*`, timed `*_timeout`, and blocking `*`), graceful waking shutdown via `close()`,
> and rigorous RAII resource destruction.

## 2. Context and intent

In safety-critical avionics, automotive ECUs, and real-time robotics pipelines (such as NVIDIA Halos and Holoscan Sensor Bridge), dynamic heap allocation
on real-time execution threads introduces unpredictable non-deterministic latency and risks heap fragmentation or memory exhaustion. `BoundedFifo` provides
a statically sized circular ring buffer where capacity is fixed at compile-time and elements reside inline within the queue structure. Thread-safe
multi-producer multi-consumer synchronization is achieved using mutex and condition variable primitives, guaranteeing strictly bounded memory consumption,
predictable latency, and reliable cancellation during system teardown.

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | Storage capacity is established at compile-time via template parameters or const generics (`BoundedFifo<T, const CAPACITY: usize>`), storing slots inline | Runtime-allocated ring buffer (rejected: violates zero steady-state heap constraint) |
| A-2 | Thread synchronization is provided via standard mutual exclusion and dual condition variables (`not_empty` and `not_full`) | Lock-free atomics (requires specific memory layout or SPSC restrictions; MPMC requires complex lock-free algorithms) |
| A-3 | Operations are provided as a complete triad: non-blocking `try_*`, timed `*_timeout`, and indefinite blocking `*` | Only blocking or only non-blocking operations |
| A-4 | On push rejection (due to full queue or shutdown), ownership of the rejected item is returned to the caller | Dropping the rejected item implicitly (violates zero-loss ownership contracts) |
| A-5 | `close()` idempotently latches shutdown state and wakes all threads blocked on `not_empty` or `not_full` condition variables | Leaving waiters blocked until explicit cancellation |
| A-6 | Unconsumed items are safely dropped and cleaned up when the `BoundedFifo` instance is dropped (RAII) | Requiring manual queue drainage before destruction |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| MPMC | Multi-Producer Multi-Consumer concurrency pattern |
| Const generics | Compile-time constant parameters defining array dimensions in Rust |
| Triad API | Complementary set of non-blocking, timed-blocking, and indefinite-blocking operations |
| Rejection contract | Invariant guaranteeing that rejected elements are returned intact to the caller without data loss |

## 5. Interface overview

- Configuration: `BoundedFifo<T, const CAPACITY: usize>` (compile-time capacity `CAPACITY > 0`).
- Non-blocking operations: `try_push(item) -> Result<(), PushError<T>>`, `try_pop() -> Result<T, PopError>`.
- Timed operations: `push_timeout(item, timeout) -> Result<(), PushTimeoutError<T>>`, `pop_timeout(timeout) -> Result<T, PopTimeoutError>`.
- Blocking operations: `push(item) -> Result<(), PushError<T>>`, `pop() -> Result<T, PopError>`.
- State inspection: `len() -> usize`, `is_empty() -> bool`, `is_full() -> bool`, `is_closed() -> bool`, `capacity() -> usize`.
- Teardown: `close()`.

## 6. Functional requirements

### FIFO-REQ-001: Fixed-capacity compile-time storage without steady-state heap allocations

The `BoundedFifo` component shall allocate all element storage inline at compile-time according to the configured capacity parameter. Steady-state push,
pop, and query operations shall perform zero dynamic heap allocations.

- **Verification:** Test
- **Rationale:** Guarantees deterministic execution time and prevents runtime out-of-memory faults in safety-critical threads.

### FIFO-REQ-002: Thread-safe multi-producer multi-consumer (MPMC) synchronization

The `BoundedFifo` component shall support concurrent access by an arbitrary number of producer and consumer threads without data races, corruption, or
deadlock. Element ordering shall strictly preserve FIFO sequencing with respect to the order in which push operations acquire internal mutual exclusion.

- **Verification:** Test
- **Rationale:** Ensures reliable multi-threaded communication across heterogeneous tasks and priorities.

### FIFO-REQ-003: Deterministic triad API

The `BoundedFifo` component shall provide a complete triad of enqueue and dequeue operations:

1. Non-blocking `try_push` and `try_pop` returning immediately with success or error.
2. Timed `push_timeout` and `pop_timeout` blocking until room/element is available or the specified duration expires.
3. Indefinite blocking `push` and `pop` suspending the calling thread until the operation can proceed or the queue is closed.

- **Verification:** Test
- **Rationale:** Grants callers flexibility to adapt blocking semantics to hard real-time deadlines or background work.

### FIFO-REQ-004: Rejection contracts retaining rejected values on push error

When a push operation fails due to a full queue or a closed queue, the operation shall return the rejected element intact to the caller in the error payload,
preserving ownership without dropping or leaking the element.

- **Verification:** Test
- **Rationale:** Prevents loss of telemetry, actuator packets, or heap-allocated payloads upon buffer saturation or shutdown.

### FIFO-REQ-005: Waking shutdown via close unblocking all waiting producers and consumers

Calling `close()` shall idempotently latch the closed state, wake all threads currently blocked on enqueue or dequeue operations, cause subsequent push
operations to fail immediately with a closed error, and permit consumer threads to drain all remaining queued elements until the queue is empty.

- **Verification:** Test
- **Rationale:** Enables clean, predictable system shutdown and thread termination without resource leaks or thread hangs.

### FIFO-REQ-006: Proper RAII cleanup and drop of unconsumed elements

When a `BoundedFifo` instance is destroyed or dropped, any remaining unconsumed elements in the queue shall be dropped cleanly in accordance with standard
RAII invariants without memory or resource leaks.

- **Verification:** Test
- **Rationale:** Eliminates resource leaks when terminating components or resetting subsystems.
