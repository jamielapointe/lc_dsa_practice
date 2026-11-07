# Rust Systems Header, Stub, and Architecture Reference

Used by `systems-setup` when scaffolding Rust systems components. Follow `rust-pro` and ASPICE SWE.1/ISO 26262 principles.

## Safety & Architectural Constraints

1. **Zero Steady-State Heap Allocation**:
   - Runtime operations must not invoke heap allocators (`Box`, `Vec`, `String`).
   - Sizing is specified via const generics: `pub struct <Name><T, const CAPACITY: usize>`.
   - Internal storage uses fixed arrays (`[core::mem::MaybeUninit<T>; CAPACITY]` or inline arrays).
2. **Zero Runtime Panics**:
   - `panic!`, `unwrap()`, `expect()` strictly prohibited in runtime paths. Production compiles with `panic = "abort"`.
   - Operations return `Result<T, <Name>Error>` with `#[must_use]`.
   - Error enum: `#[derive(Debug, Clone, Copy, PartialEq, Eq)] pub enum <Name>Error` containing `NotImplemented`.
3. **Deterministic Triad & Shutdown**:
   - Provide non-blocking `try_*`, timed `*_timeout(..., Duration)`, and blocking `*`.
   - Provide `close(&self)` or `request_stop(&self)` waking all waiting threads and processes.
4. **Concurrency & Shared Memory**:
   - Types placed in shared memory must be `#[repr(C)]`, `Copy`, address-free.
   - Cache-line padding: `#[repr(align(64))]`.
   - Explicit atomic orderings (`Acquire`, `Release`, `Relaxed`).
5. **Strict `// SAFETY:` Comments**:
   - Every `unsafe` block must be documented with a detailed `// SAFETY:` comment.
6. **OS Adapter Isolation**:
   - Direct POSIX syscalls (`shm_open`, `mmap`, `sem_*`) must reside in `<name>_posix.rs`.

## Stub Implementation

Stubs return `Err(<Name>Error::NotImplemented)`, never sleep, never spin, never allocate, and never panic.
Never use panic!(), todo!(), or unimplemented!() in stubs.
Unused parameters are suppressed with `_`.

## 6-Tier Test Suite (`test/rust/systems/<name>_test.rs`)

1. **Traceability**: First comment or statement of each test tags the requirement:

   ```rust
   #[test]
   fn publish_assigns_increasing_sequence_numbers() {
       // requirements: SHMPS-REQ-005
       ...
   }
   ```

2. **Tiers**:
   - Tier 1: Functional nominal behavior.
   - Tier 2: Boundary conditions, full/empty buffer, timeouts, use after close.
   - Tier 3: Zero heap allocation after init, static assertions (`Send`, `Sync`).
   - Tier 4: Multi-thread stress test with `std::thread::scope` and `std::sync::Barrier`.
   - Tier 5: Multi-process test via `std::process::Command` over POSIX shared memory.
   - Tier 6: Fault injection (killing child processes and verifying survivor state).
