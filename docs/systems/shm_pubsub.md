# ShmPubSub — Software Requirements

| Field | Value |
| --- | --- |
| Component | `systems::ShmChannel`, `systems::ShmPublisher`, `systems::ShmSubscriber` (`src/include/systems/shm_pubsub.hpp`) |
| Requirement prefix | `SHMPS` |
| Status | Draft (TDD red; implementation pending) |
| Integrity context | Practice component written with ISO 26262 (ASIL-B-style) rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX (production); macOS (development host, see Portability) |

## 1. Problem statement

> Create a pub/sub interface to some shared memory. This needs to be safe to access across processes and threads (so concurrency protections). The shared
> memory should be setup like a ring buffer; either way, it will have a finite size and will have blocks (0, 1, 2, ..., n) that a publisher can write to and a
> subscriber can read from.

## 2. Context and intent

A sensor driver process (camera, lidar, CAN gateway) publishes fixed-size samples that perception, logging, and diagnostics processes consume at their own
rates. The publisher runs at a hard rate and must never wait for a slow consumer, so the channel behaves like a "latest N samples" ring: old data is
overwritten and each consumer learns how much it missed. All memory is reserved when the channel is created; steady-state publishing and receiving touch no
heap, take no locks on the writer path, and survive the publisher being killed at any instruction.

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | Overflow policy is **overwrite oldest**; the publisher never blocks | Back-pressure from the slowest subscriber (rejected: one stuck consumer would stall a real-time producer) |
| A-2 | Exactly **one publisher** per channel at a time (threads or processes); any number of subscribers | Multiple serialized publishers (out of scope) |
| A-3 | Slot count and slot size are fixed at creation; slot count is a power of two | Compile-time template sizes (rejected: separate processes must agree at run time; `open()` reads the configuration) |
| A-4 | Each subscriber keeps its own cursor in its own process memory; subscribers do not affect each other or the publisher | Shared consumer cursors (queue semantics) |
| A-5 | Payloads are opaque bytes, `0..slot_size` long; the subscriber copies them out | Zero-copy loans (out of scope, see section 13) |
| A-6 | A subscriber that attaches starts at the **next** sample to be published (no history replay) | Replay the whole ring on attach |
| A-7 | Blocking receive may be implemented with a process-shared condition variable, a futex, or bounded polling with back-off, as long as the latency requirements hold | Mandating one mechanism |
| A-8 | A `ShmPublisher` / `ShmSubscriber` handle is used by one thread at a time; the shared channel is safe for concurrent use by many handles | Internally synchronized handles |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| Channel | The named POSIX shared-memory object plus its layout (header, slots) |
| Slot | One fixed-size block in the ring: payload bytes plus metadata |
| Sequence | Monotonic 64-bit sample number assigned by the publisher, starting at 1 |
| Missed | Number of samples a subscriber skipped because they were overwritten before it read them |
| Torn sample | A sample whose bytes mix two different publications |

## 5. Interface overview

- `ShmPubSubConfig` — `slot_count`, `slot_size`; limits `kShmMinSlotCount`, `kShmMaxSlotCount`, `kShmMaxSlotSize`.
- `ShmPubSubError` — error enumeration, `to_string()`.
- `ShmChannel::create(name, config)`, `ShmChannel::open(name)`, `ShmChannel::unlink(name)`, `config()`, `close()`, `is_closed()`.
- `ShmPublisher::attach(channel)`, `publish(bytes)`.
- `ShmSubscriber::attach(channel)`, `try_receive(buffer)`, `receive_for(buffer, timeout)`, `receive(buffer)`; `ShmSample{sequence, size, missed}`.
- `detail::SharedMemoryRegion` — OS adapter (`create`, `open`, `unlink`, `data`, `size`), the only code that calls POSIX.

## 6. Functional requirements

### SHMPS-REQ-001: Create a channel with a validated configuration

`ShmChannel::create(name, config)` shall create and initialize a new channel when `config.slot_count` is a power of two in
`[kShmMinSlotCount, kShmMaxSlotCount]` and `config.slot_size` is in `[1, kShmMaxSlotSize]`; otherwise it shall return `kInvalidArgument` and create nothing.

- **Verification:** Test

### SHMPS-REQ-002: Validate channel names

Channel names shall start with `/`, contain no other `/`, and have at least one character after it; otherwise `kInvalidArgument`. Names longer than
`systems::platform::kMaxShmNameLength` shall return `kNameTooLong`.

- **Verification:** Test
- **Rationale:** Portable names avoid behavior that differs between Linux, QNX, and macOS.

### SHMPS-REQ-003: Refuse to overwrite an existing channel

`create` shall return `kAlreadyExists` when an object with that name already exists, leaving it untouched.

- **Verification:** Test
- **Rationale:** Re-initializing a live channel would corrupt data seen by running processes.

### SHMPS-REQ-004: Open an existing channel

`ShmChannel::open(name)` shall map an existing channel and report the configuration it was created with; a missing name shall return `kNotFound`.

- **Verification:** Test

### SHMPS-REQ-005: Reject incompatible segments

`open` shall return `kLayoutMismatch` when the object is too small, or its magic number, layout version, or configuration is invalid.

- **Verification:** Test
- **Rationale:** Mapping a foreign or older layout would make readers interpret garbage as samples.

### SHMPS-REQ-006: Unlink a channel name

`ShmChannel::unlink(name)` shall remove the name so later `open` calls fail with `kNotFound`, while existing mappings stay usable; unlinking a missing name
shall return `kNotFound`.

- **Verification:** Test

### SHMPS-REQ-007: Exactly one publisher

`ShmPublisher::attach` shall return `kPublisherExists` while another publisher (in any thread or process) is attached and alive. Destroying the publisher
shall release the role so a new `attach` succeeds.

- **Verification:** Test
- **Rationale:** Two writers on a single-writer ring would corrupt slots.

### SHMPS-REQ-008: Publish copies a payload and assigns a sequence

`publish(bytes)` shall copy `bytes` (0 to `slot_size` bytes) into the next slot and return its sequence number: 1 for the first sample on a new channel and
incremented by one for each later sample. Payloads larger than `slot_size` shall return `kPayloadTooLarge` and publish nothing.

- **Verification:** Test

### SHMPS-REQ-009: Publishing never blocks

`publish` shall succeed without waiting regardless of how many subscribers exist or how far behind they are; when the ring is full the oldest slot is
overwritten.

- **Verification:** Test
- **Rationale:** A real-time producer must not inherit the timing of its slowest consumer.

### SHMPS-REQ-010: Subscribers receive samples in order and intact

A subscriber shall start at the next sample published after it attached and receive every later sample in sequence order, with the exact payload bytes and
size, as long as it keeps up (falls behind by fewer than `slot_count` samples).

- **Verification:** Test

### SHMPS-REQ-011: Overrun is detected and reported

When a subscriber falls behind by `slot_count` samples or more, its next successful receive shall return the oldest sample still available, and
`ShmSample::missed` shall equal the number of samples skipped since its previous receive. Sequence numbers make the gap visible.

- **Verification:** Test
- **Rationale:** Consumers must know data was lost (for example to invalidate a filter state) rather than silently processing a discontinuity.

### SHMPS-REQ-012: Non-blocking receive

`try_receive(buffer)` shall return `kWouldBlock` when no unread sample exists, and `kBufferTooSmall` when `buffer` is smaller than the next sample, in which
case the sample is not consumed.

- **Verification:** Test

### SHMPS-REQ-013: Timed and blocking receive

`receive_for(buffer, timeout)` shall return `kTimedOut` no earlier than `timeout` after the call when nothing is published, and shall return a sample within
100 ms after it is published. `receive(buffer)` shall block until a sample is available or the channel is closed.

- **Verification:** Test

### SHMPS-REQ-014: Close wakes everyone and drains

After `close()` (callable from any handle in any process), `publish` shall return `kClosed`; subscribers shall still receive samples published before the
close that they have not read, then `kClosed`; and every blocked `receive` / `receive_for` in any thread or process shall return within 100 ms.

- **Verification:** Test
- **Rationale:** Orderly shutdown must not leave threads blocked forever.

## 7. Concurrency requirements

### SHMPS-REQ-015: No torn samples under concurrency

A subscriber running concurrently with the publisher (different threads) shall never return a sample whose bytes differ from what was published under that
sequence number.

- **Verification:** Test
- **Rationale:** Torn reads produce plausible-looking but wrong data, the most dangerous failure for downstream consumers.

### SHMPS-REQ-016: Works across processes

A publisher and subscribers in different processes, each opening the channel by name, shall satisfy REQ-008 to REQ-015.

- **Verification:** Test

### SHMPS-REQ-017: Independent subscribers

Any number of subscribers shall be able to attach to one channel; each shall keep its own cursor and receive all samples it keeps up with, unaffected by the
others.

- **Verification:** Test

## 8. Resource and memory requirements

### SHMPS-REQ-018: No heap allocation after initialization

`publish`, `try_receive`, `receive_for`, `receive`, `close`, and `is_closed` shall complete without heap allocation. Only `create`, `open`, and `attach` may
allocate.

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Steady-state allocation risks fragmentation, unbounded latency, and memory exhaustion (ISO 26262-6:2018 Table 6 item 1b; MISRA C++:2023
  Rule 21.6.1).

### SHMPS-REQ-019: Handle lifetime and resource release

Channel, publisher, and subscriber handles shall be move-only with `noexcept` moves, and every public operation shall be `noexcept`. Destroying a channel
handle shall unmap memory and release descriptors without unlinking the name.

- **Verification:** Test

## 9. Robustness and failure-model requirements

### SHMPS-REQ-020: Survive a publisher killed at any point

If the publisher process is killed (SIGKILL) at an arbitrary instruction, subscribers shall never receive a torn or partially written sample, a new publisher
shall be able to attach once the old process is dead, and sequence numbers published by the new publisher shall be greater than every sequence published
before the crash.

- **Verification:** Test
- **Rationale:** A crashing driver must not poison consumers or block a restarted driver.

## 10. Portability requirements

### SHMPS-REQ-021: OS calls are isolated

All operating-system calls shall be confined to `src/cpp/systems/shm_pubsub_posix.cpp` (`detail::SharedMemoryRegion` and helpers); `shm_pubsub.cpp` shall not
include POSIX headers, and capability differences shall be handled through `systems/platform.hpp`.

- **Verification:** Review

### SHMPS-REQ-022: Shared layout is address-free

Everything placed in shared memory shall be trivially copyable data or `std::atomic` types that are always lock-free; no pointers, vtables, `std::mutex`, or
`std::condition_variable`.

- **Verification:** Review (`static_assert` on the layout types)

## 11. Failure modes (mini-FMEA)

| Failure mode | Effect | Detection / mitigation | Requirement |
| --- | --- | --- | --- |
| Publisher overwrites a slot while a subscriber copies it | Torn sample delivered | Per-slot sequence counter checked before and after the copy (retry on change) | SHMPS-REQ-015 |
| Subscriber far behind | Reads overwritten data as if contiguous | Sequence gap detection, `missed` count | SHMPS-REQ-011 |
| Publisher dies mid-write | Slot left half-written; role never released | Slot marked "being written" until commit; liveness check on publisher takeover | SHMPS-REQ-020, SHMPS-REQ-007 |
| Second publisher starts | Concurrent writers corrupt slots | Publisher role claimed atomically in the header | SHMPS-REQ-007 |
| Stale or foreign segment | Garbage interpreted as samples | Magic, version, configuration check on open | SHMPS-REQ-005 |
| Shutdown while blocked | Threads hang forever | `closed` flag in shared header plus bounded wake-up | SHMPS-REQ-014 |

## 12. Standards guidance (informative)

| Topic | Reference | How it applies here |
| --- | --- | --- |
| Dynamic memory | ISO 26262-6:2018 Table 6 (1b); MISRA C++:2023 Rule 21.6.1; AUTOSAR C++14 A18-5-1 | Heap only in `create` / `open` / `attach` |
| Exceptions | Project policy (see `safety.md`) | `noexcept` API, `std::expected` errors |
| C arrays | AUTOSAR C++14 A18-1-1 | Shared layout may need flexible storage; justify each NOLINT |
| Process | ASPICE SWE.1 / SWE.4 | This spec, `RecordProperty` tags, `pixi run check-traceability` |

Rule numbers are informative pointers; confirm them against licensed copies of the standards before relying on them.

## 13. Out of scope

- Access control and security of the shared-memory name (permissions are `0600`).
- Zero-copy "loan" APIs, variable-size records, multiple concurrent publishers, and history replay on attach.
- Recovery from a *subscriber* crash (subscribers hold no shared state, so there is nothing to recover).

## 14. Implementation hints (optional, spoiler-free)

Read about: seqlocks (per-slot sequence counters with odd = "writing"), `std::atomic_thread_fence` and acquire / release pairing, cache-line padding between
the header and slots, `ftruncate` + `mmap(MAP_SHARED)`, `O_CREAT | O_EXCL`, checking whether a process is alive with `kill(pid, 0)`, and why
`std::atomic<std::uint64_t>` must be lock-free to be usable across processes.
