# Autonomous Curriculum Roadmap: 10 Progressive Systems Modules

An intensive, 10-module systems progression designed for veteran C/C++ systems architects preparing for senior Rust systems interviews. Each module presents an
embedded, operating system, or high-throughput distributed systems challenge, concrete API signatures, 4-tier TDD test criteria, and interview evaluation
anchors.

---

## 1. Curriculum Overview & Progression Matrix

```text
                               10-MODULE SYSTEMS PROGRESSION
 [01: Affine Types & Moves]  --->  [02: Lifetimes & Variance]  --->  [03: Interior Mutability]
             |                                                                     |
             v                                                                     v
 [04: Custom DMA Allocator]  --->  [05: Zero-Copy Slice Parser] --->  [06: Bidirectional Iterators]
             |                                                                     |
             v                                                                     v
 [07: Fearless Concurrency]  --->  [08: Lock-Free Ring Buffer] --->  [09: Trait Objects & Bus]
                                                                                   |
                                                                                   v
                                                                     [10: C++/Rust Interop Bridge]
```

| Mod | Directory & Slug | Core Systems Challenge | Primary Rust 1.99.1 & 2024 Focus |
| :-- | :--- | :--- | :--- |
| **01** | `01_shm_packet_transfer` | Zero-allocation shared memory packet handoff | Bitwise `memcpy` moves, static binding invalidation, drop flags |
| **02** | `02_zero_copy_frame_slicer` | Multiplexed telemetry frame sub-slicing | Covariance of `&'a T`, invariance of `&'a mut T`, `use<..>` |
| **03** | `03_reentrant_irq_context` | Reentrant ISR context status tracking | `UnsafeCell`, `Cell` vs. `RefCell`, reentrant locks, atomic flags |
| **04** | `04_aligned_dma_pool` | 64B cache-line aligned DMA pool without global heap | `core::ptr::NonNull`, custom RAII `DmaHandle<T>`, `Layout` |
| **05** | `05_canfd_telemetry_parser` | Variable payload (up to 64B) CAN-FD frame parsing | Slice sub-slicing, `u16::from_be_bytes`, bounds-check elimination |
| **06** | `06_circular_ring_iter` | Bidirectional ring buffer iterator | `DoubleEndedIterator`, `ExactSizeIterator`, fused guarantees |
| **07** | `07_rendezvous_channel` | Synchronous 1-to-1 thread rendezvous channel | `std::thread::scope`, `Condvar`, `Mutex`, timeout bounds |
| **08** | `08_spsc_lockfree_queue` | SPSC lock-free ring buffer with cache-line padding | `AtomicUsize`, `Acquire`/`Release` ordering, `#[repr(align(64))]` |
| **09** | `09_heterogeneous_bus` | Peripheral bus registry with static/dynamic dispatch | `Send` + `Sync`, trait object fat pointers (`dyn Trait`), vtables |
| **10** | `10_sensor_fusion_bridge` | C++23 Kalman filter state machine bridge | `#[cxx::bridge]`, `Pin<&mut CppClass>`, `UniquePtr`, `panic = "abort"` |

---

## 2. Detailed Module Specifications

### Module 01: Affine Types & Destructive Moves (`01_shm_packet_transfer`)

- **Systems Challenge**: Implement a fixed-capacity shared-memory packet handoff pipeline where packet ownership transfer from producer to consumer is
  strictly enforced at compile time by Rust's affine type system, completely eliminating double-frees and dangling buffer references without runtime reference
  counting.
- **Core Mechanics**: Bitwise `memcpy` move semantics, static invalidation of source bindings in the compiler CFG, runtime stack drop flags, zero sentinel
  values, non-copyable ownership tokens.
- **Code Snippets & Signatures**:

  ```rust
  pub const PACKET_CAPACITY: usize = 1500;

  #[repr(C)]
  pub struct PacketBuffer {
      payload: [u8; PACKET_CAPACITY],
      len: usize,
      channel_id: u16,
  }

  pub struct PacketProducer {
      slot: Option<PacketBuffer>,
  }

  pub struct PacketConsumer {
      slot: Option<PacketBuffer>,
  }

  pub struct ShmHandoffChannel;

  impl ShmHandoffChannel {
      #[must_use]
      pub fn new() -> (PacketProducer, PacketConsumer) {
          todo!("Implement ShmHandoffChannel::new")
      }
  }

  impl PacketProducer {
      pub fn send(&mut self, packet: PacketBuffer) -> Result<(), PacketBuffer> {
          todo!("Implement PacketProducer::send")
      }
  }

  impl PacketConsumer {
      pub fn receive(&mut self) -> Option<PacketBuffer> {
          todo!("Implement PacketConsumer::receive")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Producer sends valid packet, consumer receives identical payload.
  - *Tier 2 (Boundary)*: Send fails when slot is full; receive returns `None` when empty; 0-byte packet handling.
  - *Tier 3 (Invariants & RAII)*: Custom RAII drop tracker proves dropped packets release underlying resources exactly once.
  - *Tier 4 (Ownership Proof)*: Verification that moved packets cannot be accessed in the producer frame.
- **Interview Criteria**:
  - Explains the contrast between C++ move constructors (`T(T&&)`) leaving moved-from objects in valid states vs. Rust static dead-binding drops.
  - Demonstrates why stack drop flags are omitted by the compiler when transfers are unconditional.

---

### Module 02: Lifetimes, Borrow Checking & Variance (`02_zero_copy_frame_slicer`)

- **Systems Challenge**: Parse a multiplexed binary telemetry stream into sub-frame view structs that reference the backing arena without copying, enforcing
  lifetime bounds where sub-frame lifetimes cannot outlive the parent arena.
- **Core Mechanics**: Lifetime subtyping (`'a: 'b`), covariance of `&'a T`, invariance of `&'a mut T`, compiler NLL regions, precise lifetime capturing via
  Rust Edition 2024 `use<..>` syntax.
- **Code Snippets & Signatures**:

  ```rust
  #[derive(Debug, PartialEq, Eq)]
  pub struct TelemetryHeader {
      pub stream_id: u16,
      pub sequence_num: u32,
      pub payload_len: u16,
  }

  pub struct FrameSlice<'buf> {
      pub header: TelemetryHeader,
      pub payload: &'buf [u8],
  }

  pub struct FrameSlicer<'arena> {
      raw_buffer: &'arena [u8],
      cursor: usize,
  }

  impl<'arena> FrameSlicer<'arena> {
      #[must_use]
      pub fn new(buffer: &'arena [u8]) -> Self {
          todo!("Implement FrameSlicer::new")
      }

      pub fn next_frame(&mut self) -> Result<Option<FrameSlice<'arena>>, SlicerError> {
          todo!("Implement FrameSlicer::next_frame")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Sequential extraction of 3 valid telemetry frames from contiguous buffer.
  - *Tier 2 (Boundary)*: Truncated frame header returns `SlicerError::UnexpectedEof`; zero-length payload.
  - *Tier 3 (Invariants & Lifetime Scopes)*: Multiple sub-frames simultaneously borrowed from the same backing slice.
  - *Tier 4 (Variance & Invariance Proof)*: Static assertion proving `&'arena mut T` cannot accept narrowed lifetimes.
- **Interview Criteria**:
  - Articulates why `&'a mut T` must be invariant over `T` to prevent storing short-lived references into long-lived memory.
  - Correctly applies Edition 2024 `use<'buf>` syntax in return-position `impl Trait`.

---

### Module 03: Interior Mutability & Cell Hierarchy (`03_reentrant_irq_context`)

- **Systems Challenge**: Build an interrupt service routine (ISR) context object that allows safe interior mutation of status counters, error flags, and
  callback registrations across shared references (`&self`) without global mutex locking.
- **Core Mechanics**: The `UnsafeCell<T>` core primitive, `Cell<T>` for `Copy` types, `RefCell<T>` dynamic borrow checking, thread-local reentrancy, atomic
  state flags.
- **Code Snippets & Signatures**:

  ```rust
  use core::cell::{Cell, RefCell};

  pub struct IrqStatus {
      pub interrupt_count: Cell<u64>,
      pub last_error_code: Cell<u32>,
      pub handler_log: RefCell<[u32; 16]>,
      pub log_cursor: Cell<usize>,
  }

  impl IrqStatus {
      #[must_use]
      pub const fn new() -> Self {
          todo!("Implement IrqStatus::new")
      }

      pub fn record_irq(&self, vector: u32) {
          todo!("Implement IrqStatus::record_irq")
      }

      pub fn get_metrics(&self) -> (u64, u32) {
          todo!("Implement IrqStatus::get_metrics")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Shared reference `&IrqStatus` records 100 interrupts; counters reflect accurate state.
  - *Tier 2 (Boundary)*: Circular buffer wrap-around on `handler_log` when exceeding 16 entries.
  - *Tier 3 (Borrow Violation Panic)*: Intentional reentrant borrow collision on `RefCell` verifies dynamic borrow panic.
  - *Tier 4 (Single-Thread Concurrency)*: Reentrant callback execution mutating state through `&self`.
- **Interview Criteria**:
  - Contrasts `Cell<T>` (zero-cost `memcpy` of value) with `RefCell<T>` (runtime borrow counter `isize`).
  - Explains why `Cell` and `RefCell` are `!Sync` and how compiler prevents data races across threads.

---

### Module 04: Smart Pointers & Custom Allocators (`04_aligned_dma_pool`)

- **Systems Challenge**: Implement a fixed-size, 64-byte cache-line aligned direct memory access (DMA) pool that provides custom smart-pointer handles
  (`DmaHandle<T>`) returning memory to the pool on drop without invoking the global heap allocator.
- **Core Mechanics**: `core::ptr::NonNull`, `core::alloc::Layout`, `Drop` trait implementation, custom pointer wrappers, alignment calculation, free-list
  management.
- **Code Snippets & Signatures**:

  ```rust
  use core::ptr::NonNull;

  pub struct DmaHandle<T> {
      ptr: NonNull<T>,
      pool_slot_index: usize,
      pool: NonNull<DmaPoolStorage>,
  }

  pub struct DmaPool<const BLOCKS: usize, const BLOCK_SIZE: usize> {
      storage: Box<DmaPoolStorage>,
  }

  impl<const BLOCKS: usize, const BLOCK_SIZE: usize> DmaPool<BLOCKS, BLOCK_SIZE> {
      #[must_use]
      pub fn new() -> Result<Self, DmaPoolError> {
          todo!("Implement DmaPool::new")
      }

      pub fn allocate<T>(&mut self, value: T) -> Result<DmaHandle<T>, DmaPoolError> {
          todo!("Implement DmaPool::allocate")
      }
  }

  impl<T> core::ops::Deref for DmaHandle<T> {
      type Target = T;
      fn deref(&self) -> &Self::Target {
          todo!("Implement DmaHandle::deref")
      }
  }

  impl<T> Drop for DmaHandle<T> {
      fn drop(&mut self) {
          todo!("Implement DmaHandle::drop")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Allocate, dereference, mutate, and drop a DMA buffer block.
  - *Tier 2 (Boundary)*: Exhaust pool blocks; assert `DmaPoolError::PoolExhausted`.
  - *Tier 3 (Alignment & Zero Heap)*: Assert `(ptr as usize) % 64 == 0` for every block; verify zero global heap allocations during steady state.
  - *Tier 4 (RAII Reclamation)*: Dropping `DmaHandle<T>` releases slot back to pool; slot immediately reusable.
- **Interview Criteria**:
  - Explains how `NonNull<T>` participates in compiler pointer niche optimization (`Option<DmaHandle<T>>` is 8 bytes).
  - Documents explicit `// SAFETY:` rationale on raw pointer dereferencing and alignment arithmetic.

---

### Module 05: Zero-Copy Parsing & Slices (`05_canfd_telemetry_parser`)

- **Systems Challenge**: Parse high-frequency CAN-FD bus frames with variable payload lengths (up to 64 bytes) zero-copy directly from raw network byte
  buffers with bounds-check elimination.
- **Core Mechanics**: `&[u8]` slicing, `split_at`, `u16::from_be_bytes`, `u32::from_le_bytes`, endianness conversion, SIMD auto-vectorization, panic-free
  parsing.
- **Code Snippets & Signatures**:

  ```rust
  #[derive(Debug, PartialEq, Eq)]
  pub struct CanFdFrame<'a> {
      pub id: u32,
      pub is_extended: bool,
      pub dlc: u8,
      pub payload: &'a [u8],
      pub crc: u16,
  }

  pub fn parse_canfd_frame(buffer: &[u8]) -> Result<(CanFdFrame<'_>, &[u8]), CanParseError> {
      todo!("Implement parse_canfd_frame")
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Parse standard 8-byte and maximum 64-byte CAN-FD frames with accurate field extraction.
  - *Tier 2 (Boundary)*: Buffer smaller than minimum header (5 bytes); invalid DLC code (`> 15`); CRC mismatch.
  - *Tier 3 (Zero Allocation)*: Run parser in loop over 100,000 frames; verify 0 bytes allocated on heap.
  - *Tier 4 (Assembly Inspection / Stress)*: Verify disassembly emits branchless slice loads without panic landing pads.
- **Interview Criteria**:
  - Explains how compiler eliminates array bounds checks when length preconditions are proven upfront (`if buffer.len() < HEADER_SIZE`).
  - Contrasts zero-copy slicing with C++ `std::string_view` and `std::span`.

---

### Module 06: Custom Iterators & Pipelines (`06_circular_ring_iter`)

- **Systems Challenge**: Write a custom bidirectional, exact-size iterator over an in-memory circular ring buffer supporting forward and reverse traversal
  without allocating heap collections.
- **Core Mechanics**: `Iterator`, `DoubleEndedIterator`, `ExactSizeIterator`, `size_hint`, fused iterator contract, functional combinator composition (`map`,
  `filter`, `fold`).
- **Code Snippets & Signatures**:

  ```rust
  pub struct CircularBuffer<T, const CAP: usize> {
      buffer: [Option<T>; CAP],
      head: usize,
      tail: usize,
      len: usize,
  }

  pub struct CircularIter<'a, T, const CAP: usize> {
      buffer: &'a [Option<T>; CAP],
      head: usize,
      remaining: usize,
  }

  impl<'a, T, const CAP: usize> Iterator for CircularIter<'a, T, CAP> {
      type Item = &'a T;
      fn next(&mut self) -> Option<Self::Item> {
          todo!("Implement CircularIter::next")
      }
      fn size_hint(&self) -> (usize, Option<usize>) {
          todo!("Implement CircularIter::size_hint")
      }
  }

  impl<'a, T, const CAP: usize> DoubleEndedIterator for CircularIter<'a, T, CAP> {
      fn next_back(&mut self) -> Option<Self::Item> {
          todo!("Implement CircularIter::next_back")
      }
  }

  impl<'a, T, const CAP: usize> ExactSizeIterator for CircularIter<'a, T, CAP> {}
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Forward iteration over wrapped circular buffer yields items in insertion order.
  - *Tier 2 (Boundary)*: Empty buffer; completely full buffer; single element.
  - *Tier 3 (Double-Ended Invariant)*: Alternating `next()` and `next_back()` meets in the middle without duplicate yields.
  - *Tier 4 (Combinator Inlining)*: Compose with `.filter().map().sum()`; verify zero heap overhead.
- **Interview Criteria**:
  - Explains the exact semantics of `size_hint()` and why `ExactSizeIterator` requires exact length tracking.
  - Demonstrates how Rust iterator monomorphization compares to C++20 `std::ranges::views`.

---

### Module 07: Fearless Concurrency & Synchronization (`07_rendezvous_channel`)

- **Systems Challenge**: Construct a synchronous 1-to-1 rendezvous thread handoff channel where sender and receiver threads must meet at a synchronization
  point, supporting timeouts and clean shutdown.
- **Core Mechanics**: `std::thread::scope` (eliminating `'static` lifetime bounds), `std::sync::Mutex`, `Condvar`, `atomic::AtomicBool`, spurious wakeup
  guards, timeout bounds.
- **Code Snippets & Signatures**:

  ```rust
  use std::time::Duration;

  pub struct RendezvousChannel<T> {
      // Internal synchronization primitives
      _marker: core::marker::PhantomData<T>,
  }

  impl<T> RendezvousChannel<T> {
      #[must_use]
      pub fn new() -> Self {
          todo!("Implement RendezvousChannel::new")
      }

      pub fn send(&self, value: T, timeout: Duration) -> Result<(), ChannelError> {
          todo!("Implement RendezvousChannel::send")
      }

      pub fn receive(&self, timeout: Duration) -> Result<T, ChannelError> {
          todo!("Implement RendezvousChannel::receive")
      }

      pub fn close(&self) {
          todo!("Implement RendezvousChannel::close")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Sender handoffs value to receiver; both threads rendezvous cleanly.
  - *Tier 2 (Boundary & Timeout)*: Sender times out when no receiver arrives; receiver times out when no sender arrives.
  - *Tier 3 (Shutdown Invariant)*: Calling `close()` unblocks all parked threads with `ChannelError::Closed`.
  - *Tier 4 (Scoped Thread Contention)*: 10,000 sequential handoffs inside `std::thread::scope` with zero data races.
- **Interview Criteria**:
  - Explains why `std::thread::scope` allows borrowing local stack variables into spawned threads safely.
  - Demonstrates correct handling of condition variable spurious wakeups via `wait_timeout_while`.

---

### Module 08: Lock-Free Ring Buffers & Memory Ordering (`08_spsc_lockfree_queue`)

- **Systems Challenge**: Implement a high-throughput single-producer single-consumer (SPSC) lock-free ring buffer with separated, cache-line padded head and
  tail indices to eliminate false sharing.
- **Core Mechanics**: Hardware memory models (x86 TSO vs. ARM/RISC-V weak ordering), `AtomicUsize`, `Ordering::Acquire`, `Ordering::Release`,
  `Ordering::Relaxed`, `#[repr(align(64))]` cache-line isolation.
- **Code Snippets & Signatures**:

  ```rust
  use core::sync::atomic::{AtomicUsize, Ordering};

  #[repr(align(64))]
  struct CachePadded<T>(T);

  pub struct SpscQueue<T, const CAP: usize> {
      head: CachePadded<AtomicUsize>, // Producer writes, consumer reads
      tail: CachePadded<AtomicUsize>, // Consumer writes, producer reads
      // Ring storage
  }

  impl<T, const CAP: usize> SpscQueue<T, CAP> {
      #[must_use]
      pub fn new() -> Self {
          todo!("Implement SpscQueue::new")
      }

      pub fn push(&self, value: T) -> Result<(), T> {
          todo!("Implement SpscQueue::push")
      }

      pub fn pop(&self) -> Option<T> {
          todo!("Implement SpscQueue::pop")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Single-threaded push and pop FIFO sequence.
  - *Tier 2 (Boundary)*: Queue full condition; queue empty condition; capacity power-of-two wrap.
  - *Tier 3 (False Sharing Verification)*: Static assertion: `align_of::<CachePadded<AtomicUsize>>() == 64` and addresses differ by at least 64 bytes.
  - *Tier 4 (High-Contention MPMC Stress)*: 10,000,000 items transferred between dedicated producer and consumer threads; verify 0 dropped items, 0 corruption.
- **Interview Criteria**:
  - Justifies why `Acquire`/`Release` ordering is sufficient on SPSC ring buffers without requiring expensive `SeqCst` full memory barriers.
  - Explains false sharing at the L1/L2 cache coherency protocol level (MESI/MOESI) and how 64-byte padding prevents cache line bouncing.

---

### Module 09: Advanced Traits, Send/Sync & Subtyping (`09_heterogeneous_bus`)

- **Systems Challenge**: Design a type-safe peripheral device bus registry capable of managing heterogeneous devices either via monomorphized static drivers
  or dynamic trait objects (`dyn Device`), enforcing `Send + Sync` thread-safety bounds.
- **Core Mechanics**: Auto traits `Send` and `Sync`, `PhantomData`, trait object fat pointer vtables, object safety constraints, downcasting with `Any`.
- **Code Snippets & Signatures**:

  ```rust
  pub trait Device: Send + Sync {
      fn device_id(&self) -> u32;
      fn read_status(&self) -> u32;
      fn reset(&self) -> Result<(), DeviceError>;
  }

  pub struct DeviceBus {
      devices: Vec<Box<dyn Device>>,
  }

  impl DeviceBus {
      #[must_use]
      pub fn new() -> Self {
          todo!("Implement DeviceBus::new")
      }

      pub fn register(&mut self, device: Box<dyn Device>) {
          todo!("Implement DeviceBus::register")
      }

      pub fn broadcast_reset(&self) -> Result<(), DeviceError> {
          todo!("Implement DeviceBus::broadcast_reset")
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Register 3 heterogeneous mock devices; dispatch broadcast reset across all devices.
  - *Tier 2 (Boundary)*: Empty bus broadcast; handling devices that report hardware timeout on reset.
  - *Tier 3 (Thread-Safety Bounds)*: Verify that `DeviceBus` is statically proven `Send` and `Sync`.
  - *Tier 4 (Vtable Layout Assertions)*: Assert size and alignment of `&dyn Device` fat pointers (16 bytes).
- **Interview Criteria**:
  - Explains the mathematical relationship: `T: Sync` if and only if `&T: Send`.
  - Analyzes why methods returning `Self` or containing generic parameters break trait object safety.

---

### Module 10: Bidirectional C++/Rust Interoperability & FFI (`10_sensor_fusion_bridge`)

- **Systems Challenge**: Implement a bidirectional bridge connecting a modern C++23 Kalman filter state machine with a Rust sensor telemetry ingestion engine
  via `cxx` and raw C ABI hooks.
- **Core Mechanics**: `#[cxx::bridge]`, `cxx::UniquePtr`, `Pin<&mut CppClass>`, `#[repr(C)]`, `unsafe extern "C"`, `panic = "abort"`, `build.rs` compilation
  via `cxx_build`.
- **Code Snippets & Signatures**:

  ```rust
  #[cxx::bridge(namespace = "systems::fusion")]
  pub mod ffi {
      unsafe extern "C++" {
          include!("cxx_bridge.hpp");
          type KalmanEstimator;

          fn create_estimator(initial_variance: f64) -> UniquePtr<KalmanEstimator>;
          fn update(self: Pin<&mut KalmanEstimator>, measurement: f64) -> f64;
          fn current_estimate(&self) -> f64;
      }

      #[derive(Debug, Clone, Copy)]
      struct FusionPacket {
          timestamp_ns: u64,
          raw_sensor: f64,
          filtered_state: f64,
      }

      extern "Rust" {
          type FusionPipeline;
          fn process_measurement(self: &mut FusionPipeline, raw: f64) -> FusionPacket;
      }
  }
  ```

- **TDD Test Suite Requirements**:
  - *Tier 1 (Canonical)*: Rust calls C++ `create_estimator` and `update`; state estimation converges to expected value.
  - *Tier 2 (Boundary)*: Zero measurement inputs; extreme variance bounds (`f64::MAX`).
  - *Tier 3 (Address Invariance under Pin)*: Proof that C++ mutating methods cannot be invoked on unpinned instances.
  - *Tier 4 (Destructor Verification)*: Dropping `UniquePtr<KalmanEstimator>` invokes C++ destructor without leaking memory.
- **Interview Criteria**:
  - Explains why `Pin<&mut CppClass>` is strictly required when mutating foreign C++ classes across FFI.
  - Discusses allocator segregation and why Rust `Box` must never be deallocated with C++ `free`/`delete`.
