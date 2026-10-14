# ShmPubSub — Software Requirements

| Field | Value |
| --- | --- |
| Component | `systems::ShmChannel`, `systems::ShmPublisher`, `systems::ShmSubscriber` (`src/include/systems/shm_pubsub.hpp`) |
| Requirement prefix | `SHMPS` |
| Status | Implemented (Linux reference implementation; QNX differences documented in section 15, not built) |
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
| A-1 | Overflow policy is **overwrite oldest**; the publisher is hard real-time and **wait-free**: it never reads subscriber state | Back-pressure from the slowest subscriber (rejected: one stuck consumer would stall a real-time producer) |
| A-2 | Exactly **one publisher** per channel at a time (threads or processes); any number of subscribers | Multiple serialized publishers (out of scope) |
| A-3 | Slot count and slot size are fixed at creation; slot count is a power of two | Compile-time template sizes (rejected: separate processes must agree at run time; `open()` reads the configuration) |
| A-4 | Each subscriber keeps its own cursor in its own process memory; subscribers do not affect each other or the publisher | Shared consumer cursors (queue semantics) |
| A-5 | Payloads are opaque bytes, `0..slot_size` long; the subscriber copies them out | Zero-copy loans (out of scope, see section 13) |
| A-6 | A subscriber that attaches starts at the **next** sample to be published (no history replay) | Replay the whole ring on attach |
| A-7 | Blocking receive uses **bounded polling with back-off** (spin 64, yield, then sleep 50 µs doubling to 1 ms), so the publisher never makes a system call | Process-shared condition variable or futex wake-ups (rejected: the publisher would need a syscall or a lock to wake readers) |
| A-8 | A `ShmPublisher` / `ShmSubscriber` handle is used by one thread at a time; the shared channel is safe for concurrent use by many handles | Internally synchronized handles |
| A-9 | A lapped (or torn) subscriber jumps to the **newest** sample, not the oldest still in the ring | Oldest available (rejected: the oldest slot is the next one the publisher overwrites, so the reader would likely tear again) |
| A-10 | Each subscriber picks a read mode at attach: `kInOrder` (default; continuity) or `kLatest` (freshness) | One fixed policy for all consumers |
| A-11 | `create()` / `open()` are boot-phase calls: they fault every page in and `mlock` it; a lock failure is fatal (`kMemoryLockFailed`) | Best-effort locking (rejected: an unwired real-time buffer is a latent timing fault) |
| A-12 | `mlock` is retried only on `EAGAIN`, three times (1, 2, 4 ms back-off); `EPERM` / `ENOMEM` fail at once | No retry; unbounded retry |
| A-13 | Every process that shares a channel runs in the **same PID namespace**; publisher liveness is judged from the pid stored in the header | Store a namespace-independent identity, e.g. the pid plus the process start time or a `pidfd` (out of scope); a holder's pid from another namespace means nothing here, so a live holder can be taken over wrongfully or a dead one never detected (see section 15.4) |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| Channel | The named POSIX shared-memory object plus its layout (header, slots) |
| Slot | One fixed-size block in the ring: payload bytes plus metadata |
| Sequence | Monotonic 64-bit sample number assigned by the publisher, starting at 1 |
| Missed | Number of samples a subscriber skipped because they were overwritten before it read them |
| Torn sample | A sample whose bytes mix two different publications |
| Seqlock | Per-slot 64-bit sequence word: even `2s` = slot stable and holds sample `s`; odd `2s - 1` = publisher is writing sample `s` |
| Read mode | Per-subscriber policy: `kInOrder` (every sample while keeping up) or `kLatest` (always the newest unread sample) |
| Lapped | A subscriber that is `slot_count` or more samples behind: the slot it wants has been overwritten |

## 5. Interface overview

- `ShmPubSubConfig` — `slot_count`, `slot_size`; limits `kShmMinSlotCount`, `kShmMaxSlotCount`, `kShmMaxSlotSize`.
- `ShmPubSubError` — error enumeration (including `kMemoryLockFailed`), `to_string()`.
- `ShmChannel::create(name, config)`, `ShmChannel::open(name)`, `ShmChannel::unlink(name)`, `config()`, `close()`, `is_closed()`.
- `ShmPublisher::attach(channel)`, `publish(bytes)`.
- `ShmSubscriber::attach(channel, mode = ShmReadMode::kInOrder)`, `try_receive(buffer)`, `receive_for(buffer, timeout)`, `receive(buffer)`;
  `ShmSample{sequence, size, missed}`.
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

When a `kInOrder` subscriber falls behind by `slot_count` samples or more (lapped), or a read is torn by the publisher overwriting the slot mid-copy, its
next successful receive shall return the **newest** published sample, and `ShmSample::missed` shall equal the number of samples skipped since its previous
receive. A `kLatest` subscriber shall always skip to the newest sample with the same `missed` accounting. Sequence numbers make every gap visible.

- **Verification:** Test
- **Rationale:** Consumers must know data was lost (for example to invalidate a filter state) rather than silently processing a discontinuity. Jumping to
  the newest sample (A-9) both delivers the freshest data and picks the slot farthest from being overwritten again.

### SHMPS-REQ-027: Per-subscriber read modes

`ShmSubscriber::attach(channel, mode)` shall accept `ShmReadMode::kInOrder` (the default) or `ShmReadMode::kLatest`. A `kInOrder` subscriber shall satisfy
REQ-010 and REQ-011. A `kLatest` subscriber shall return the newest unread sample on every receive, with `missed` counting the unread samples it skipped;
it shall return `kWouldBlock` when nothing new was published. Subscribers with different modes on one channel shall not affect each other.

- **Verification:** Test
- **Rationale:** Continuity consumers (IMU integration, state estimation, logging) and freshness consumers (inference, video, lidar) need different
  policies on the same stream.

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

### SHMPS-REQ-025: Wait-free publisher

`publish` shall complete in a bounded number of steps (plus the payload copy) regardless of the number, state, or speed of subscribers, including
subscribers that are preempted, stopped (`SIGSTOP`), or killed in the middle of a read. It shall use no locks, no read-modify-write atomics, no loops on
shared state, and no system calls; it shall derive each slot's sequence values from its private counter instead of loading them from shared memory.

- **Verification:** Test (publisher throughput while a subscriber process is stopped), Review (`publish()` body)
- **Rationale:** A hard real-time producer's worst-case execution time must not depend on soft real-time consumers.

### SHMPS-REQ-026: Correct on weakly ordered memory models

The publish and receive protocols shall be correct under the C++ memory model alone, without relying on x86-TSO ordering, so that they are correct on
AArch64 and other weakly ordered CPUs: the odd mark shall become visible before any payload store, the even mark only after every payload store, and a
subscriber's second sequence check shall not be satisfied by loads reordered before its payload copy. Payload bytes shared between the writer and readers
shall be accessed only through atomic operations (no data races).

- **Verification:** Test (multi-thread and multi-process torn-read stress, ThreadSanitizer), Review (ordering table in section 15)
- **Rationale:** x86 hides most store-store and load-load reordering bugs; target ECUs and the Raspberry Pi 5 / Neoverse hosts do not.

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

### SHMPS-REQ-023: Memory is resident before steady state

On Linux, every successful `create` and `open` shall return with every page of the mapping present in the calling process's page tables and locked into
RAM, so that `publish` and `receive` take no page faults. `create` shall fault pages in by writing a dummy byte to every page of the new, zero-filled
object; `open` shall never write (the segment may be live) and shall use `MAP_POPULATE`, `mlock`, and a read-touch of every page instead.

- **Verification:** Test (`VmLck` accounting; zero per-thread page faults over two laps of publish + receive)
- **Rationale:** Linux allocates and maps pages lazily per process; a first-touch fault inside `publish` would stall a hard real-time thread.
- **Note:** The ASan and TSan runtimes replace `mlock` with a successful no-op, so lock accounting is verified only in release, coverage, and UBSan builds.

### SHMPS-REQ-024: Memory-lock failure is fatal and leaves nothing behind

If the pages cannot be locked (`EPERM`, `ENOMEM`, or `EAGAIN` after three retries with 1, 2, and 4 ms back-off), `create` and `open` shall return
`kMemoryLockFailed`. A failed `create` shall unmap and unlink the object it created; a failed `open` shall unmap its mapping and never unlink the name.

- **Verification:** Test (child process with `RLIMIT_MEMLOCK = 0`)
- **Rationale:** Running with an unwired buffer would turn a configuration error into sporadic timing faults in the field.

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
| Publisher overwrites a slot while a subscriber copies it | Torn sample delivered | Per-slot seqlock checked before and after the copy; on change the copy is discarded and the reader jumps to the newest sample | SHMPS-REQ-015, SHMPS-REQ-011 |
| Subscriber far behind | Reads overwritten data as if contiguous | Sequence gap detection, jump to newest, `missed` count | SHMPS-REQ-011 |
| Publisher dies mid-write | Slot left half-written; role never released | Slot stays odd ("being written") forever, so readers never accept it; liveness check (`getpgid(pid)` + zombie check) on publisher takeover; the new publisher resumes after the highest sequence found in the header or any slot | SHMPS-REQ-020, SHMPS-REQ-007 |
| Second publisher starts | Concurrent writers corrupt slots | Publisher role claimed with a CAS on the pid in the header | SHMPS-REQ-007 |
| Dead holder's pid recycled for an unrelated live process (same PID namespace) | Takeover delayed until that process exits; no corruption | None in the implementation: the probe cannot tell the processes apart; accepted limit (section 15.4) | SHMPS-REQ-020 |
| Live holder in another PID namespace, its pid unused in ours (e.g. a host publisher seen from a container) | The probe returns `ESRCH`; the role is taken over wrongfully and two live publishers corrupt the ring | None in the implementation: excluded by assumption A-13 (all processes on a channel share one PID namespace); deployment must enforce it | SHMPS-REQ-007 |
| Dead holder in another PID namespace, its pid used by an unrelated process in ours (e.g. a container's pid 1 seen from the host) | The holder is never detected as dead; takeover stays blocked while that process lives (forever for pid 1) | None in the implementation: excluded by assumption A-13; deployment must enforce it | SHMPS-REQ-020 |
| Stale or foreign segment | Garbage interpreted as samples | Magic, version, configuration check on open | SHMPS-REQ-005 |
| Shutdown while blocked | Threads hang forever | `closed` flag in shared header; pollers re-check it at least every 1 ms | SHMPS-REQ-014 |
| First touch of a page inside `publish` | Microsecond stall of the real-time thread | Pre-fault + `mlock` in `create` / `open` | SHMPS-REQ-023 |
| `RLIMIT_MEMLOCK` too low / no `CAP_IPC_LOCK` | Buffer silently pageable | `kMemoryLockFailed` at init; nothing left behind | SHMPS-REQ-024 |
| Subscriber preempted, stopped, or killed mid-read | Publisher delayed | Publisher never reads subscriber state (wait-free) | SHMPS-REQ-025 |
| CPU or compiler reorders seqlock and payload accesses (ARM) | Torn sample accepted as valid | Release fence after the odd mark, release store of the even mark, acquire fence before the second check | SHMPS-REQ-026 |

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
the header and slots, `ftruncate` + `mmap(MAP_SHARED)`, `O_CREAT | O_EXCL`, checking whether a process is alive with `getpgid(pid)`, and why
`std::atomic<std::uint64_t>` must be lock-free to be usable across processes.

## 15. Design (Linux reference implementation)

Read the code in this order: the design walkthrough at the top of `src/include/systems/shm_pubsub.hpp`, then `ShmPublisher::publish()` and `read_slot()` /
`ShmSubscriber::try_receive()` in `src/cpp/systems/shm_pubsub.cpp`, then the residency code in `src/cpp/systems/shm_pubsub_posix.cpp`.

### 15.1 Shared layout

| Region | Contents | Notes |
| --- | --- | --- |
| Header, cache line 0 | `magic`, `layout_version`, `slot_count`, `slot_size`, `slot_stride` | Written once by `create`; `magic` is stored last (release) so `open` never sees a half-initialized header |
| Header, cache line 1 | `published` (newest committed sequence), `publisher_pid` | Written by the publisher; read by subscribers and by `attach` |
| Header, cache line 2 | `closed` | Written by `close()` from any process |
| Slot `i` (`slot_stride` bytes, cache-line multiple) | `seq` (seqlock), `size`, `reserved`, payload words | Sample `s` lives in slot `(s - 1) & (slot_count - 1)` |

Every shared field is a lock-free `std::atomic` or is accessed through `std::atomic_ref` (payload words); the layout holds no pointers (SHMPS-REQ-022).

### 15.2 Protocol and memory ordering

| Step | Operation | Order | x86-64 | AArch64 | Why |
| --- | --- | --- | --- | --- | --- |
| W1 | `seq = 2s - 1` | relaxed | `mov` | `str` | Mark the slot as being written; value computed from the publisher's private counter (no load) |
| W2 | fence | release | (compiler barrier only) | `dmb ish` | The odd mark becomes visible before any payload store |
| W3 | payload words, `size` | relaxed | `mov` | `str` | No data race: readers use atomic loads too |
| W4 | `seq = 2s` | release | `mov` | `stlr` | The even mark becomes visible only after every payload store |
| W5 | `published = s` | release | `mov` | `stlr` | Subscribers find the head without scanning slots |
| R1 | `v1 = seq` | acquire | `mov` | `ldar` / `ldapr` | Payload loads cannot move above the first check; reject if `v1 != 2s` |
| R2 | `size`, payload words | relaxed | `mov` | `ldr` | Copy into the caller's buffer |
| R3 | fence | acquire | (compiler barrier only) | `dmb ishld` | Payload loads complete before the second check |
| R4 | `v2 = seq` | relaxed | `mov` | `ldr` | Accept only if `v2 == v1`; otherwise the copy was torn and is discarded |

If a reader's payload loads (R2) saw any word written by a later publication, the acquire fence (R3) synchronizes with that publication's release fence
(W2), so R4 must observe its odd mark or something newer, and the copy is rejected. The publisher executes W1–W5 with no loop, no load of shared state, no
read-modify-write, and no system call (SHMPS-REQ-025). x86-64 needs no fence instructions because TSO already forbids store-store and load-load reordering;
AArch64 needs the barriers shown, which is why every ordering is stated in the C++ source rather than assumed.

### 15.3 Subscriber policy

- `try_receive` loads `closed` before `published`, so a reader that sees the channel closed also sees every sample committed before the close (drain).
- When lag > 0 and the reader is lapped, its previous read tore, or it is in `kLatest` mode, the cursor jumps to `published` and `missed` grows by the
  skipped count. The cursor is committed only after a successful read, so `kBufferTooSmall` consumes nothing.
- `receive_for` / `receive` poll `try_receive` with back-off (A-7); the deadline saturates, so `nanoseconds::max()` means "forever".

### 15.4 Publisher takeover

`attach` claims the role with a CAS on `publisher_pid`. If the holder is dead (`getpgid(pid)` fails with `ESRCH`, or the OS reports a zombie via
`/proc/<pid>/stat` on Linux or `sysctl` `KERN_PROC_PID` `SZOMB` on macOS), a second CAS takes the role over. The new publisher resumes at
`max(published, max over slots of seq / 2) + 1`, which is greater than any sequence the dead publisher committed or started (SHMPS-REQ-020). Release is
fork-safe: a handle releases the role only in the process that claimed it.

The liveness check trusts the pid alone, which has two limits that behave differently:

- **Recycled pid (same PID namespace):** if the kernel reuses a dead holder's pid for an unrelated live process, the probe reports the holder alive and the
  takeover is only *delayed* until that process exits. The ring is not harmed.
- **Holder in another PID namespace:** the stored pid belongs to the holder's namespace, not ours, so the probe answers for whatever process has that number
  here. If none has it (typical for a host publisher seen from a container), `getpgid` fails with `ESRCH` and a *live* publisher is taken over wrongfully;
  two publishers then write the same ring and corrupt it. If an unrelated process has it (a container's small pids, above all pid 1, which on the host is
  `init`), a *dead* publisher is never detected and takeover stays blocked while that process lives. Assumption A-13 (every process sharing a channel runs in
  the same PID namespace) excludes both outcomes; containers that share a channel must also share a PID namespace.

## 16. Platform differences (Linux, macOS Apple Silicon, QNX notes)

The portable layer (`shm_pubsub.cpp`) is unchanged across targets; only the OS adapter (`shm_pubsub_posix.cpp`) and `systems/platform.hpp` differ. Supported
targets enforced by CMake and preprocessor guards are Linux (`x86_64`, `aarch64`) and macOS (`arm64`).

| Concern | Linux (`x86_64`, `aarch64`) | macOS (`arm64` Apple Silicon) | QNX Neutrino (port notes, not built) |
| --- | --- | --- | --- |
| Allocation | `ftruncate`; tmpfs pages allocated lazily on first touch | `ftruncate`; XNU rounds segment size up to the 16 KiB VM page size | `shm_ctl(fd, SHMCTL_ANON \| SHMCTL_PHYS, 0, bytes)` allocates physically contiguous memory immediately, or map a typed-memory pool with `posix_typed_mem_open(..., POSIX_TYPED_MEM_ALLOCATE_CONTIG)` |
| Pre-fault | `create` writes every page; `open` uses `MAP_POPULATE` + read-touch | `create` writes every page; `open` uses `mlock` + read-touch (no `MAP_POPULATE`) | Not needed for `SHMCTL_PHYS` memory; avoid `MAP_LAZY` |
| Locking | `mlock` (fatal on failure) | `mlock` (fatal on failure) | No swapping, but keep `mlock` / `procnto -mL` superlocking per the target's safety manual |
| Privileges | `RLIMIT_MEMLOCK` or `CAP_IPC_LOCK` | `RLIMIT_MEMLOCK` | `PROCMGR_AID_MEM_PHYS` and `PROCMGR_AID_MEM_LOCK` abilities via `procmgr_ability()` |
| Zombie check | `/proc/<pid>/stat` state `Z` / `X` | `sysctl({CTL_KERN, KERN_PROC, KERN_PROC_PID, pid})` state `SZOMB` | `/proc/<pid>/as` with `devctl(DCMD_PROC_INFO)` |
| Cache line | 64 B (`kCacheLineSize`) | 128 B (`kCacheLineSize`) | 64 B (`kCacheLineSize`) |
| Capability flags | `platform::kHasMapPopulate` | `platform::kIsMacos && platform::kIsAarch64` | `platform::kHasPhysicalShmCtl` |

A QNX port would add `shm_pubsub_qnx.cpp` (or narrow `#if defined(__QNX__)` blocks around QNX-only symbols) and select it through `systems/platform.hpp`.
