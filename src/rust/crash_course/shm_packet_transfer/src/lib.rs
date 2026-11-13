//! # Module 01: Affine Types & Destructive Moves (`shm_packet_transfer`)
//!
//! This module provides a zero-allocation, compile-time verified shared-memory packet
//! handoff channel designed for real-time and safety-critical systems.
//!
//! ## Core Systems Challenge
//!
//! In high-performance systems engineering (robotics, networking, IPC), passing data
//! across boundaries must minimize latency, avoid runtime dynamic allocations (`malloc`/`new`),
//! and prevent concurrency hazards such as double-frees and use-after-move defects.
//!
//! Traditional C++ relies on non-destructive `std::move` casts (`T&&`), requiring custom
//! or compiler-generated move constructors (`T(T&&)`) that leave the source object in a
//! "valid but unspecified" state. In C++, the source destructor (`~T()`) still executes at
//! scope exit, mandating defensive pointer nulling and risking use-after-move logic errors.
//!
//! In Python, assignment and function calls merely rebind names and increment reference counts
//! (`ob_refcnt`), relying on asynchronous cyclic garbage collection sweeps that introduce
//! unpredictable latency spikes.
//!
//! ## Rust Affine Types & Destructive Moves
//!
//! Rust solves this deterministically at compile time using **affine logic** and **destructive moves**:
//!
//! - **Bitwise `memcpy` Moves**: Moving a struct transfers its raw bytes (`memcpy` of `size_of::<T>()`),
//!   which LLVM optimizes into register moves or elides completely through return-value optimization.
//! - **Static Binding Invalidation**: The borrow checker marks the source variable statically dead
//!   in the compiler's Control Flow Graph (CFG). Any subsequent access triggers compile error `E0382`.
//! - **Drop Glue Omission**: Because the moved-from binding is dead, the compiler omits destructor
//!   invocation for the source. `Drop::drop` runs exactly once—for the destination owner.
//! - **Stack Drop Flags**: For conditional moves across branches, the compiler injects a 1-byte
//!   boolean drop flag to track initialization dynamically, eliding it when moves are unconditional.
//! - **Zero Runtime Overhead**: Ownership handoff incurs zero heap allocations, zero atomic reference
//!   counting, and zero garbage collection overhead.

#![deny(unsafe_op_in_unsafe_fn)]
#![warn(missing_docs)]

use core::cell::UnsafeCell;
use core::mem::MaybeUninit;
use core::sync::atomic::AtomicBool;
use core::sync::atomic::Ordering;
use std::sync::Arc;

/// Maximum inline payload capacity in bytes (standard Ethernet MTU frame payload).
pub const PACKET_CAPACITY: usize = 1500;

/// Header metadata prepended to each shared-memory packet buffer.
///
/// Layout is guaranteed C-compatible (`#[repr(C)]`) for direct POSIX shared memory
/// and FFI interoperability without field reordering.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PacketHeader {
    /// Logical communication channel identifier.
    pub channel_id: u16,
    /// Monotonically increasing sequence number for ordering and loss detection.
    pub sequence_num: u32,
    /// Length of active payload data in bytes (must be `<= PACKET_CAPACITY`).
    pub payload_len: u16,
    /// Protocol control flags and status bits.
    pub flags: u8,
}

/// Fixed-capacity, zero-allocation packet buffer stored entirely inline.
///
/// `PacketBuffer` demonstrates Rust's affine type mechanics: passing `PacketBuffer`
/// transfers ownership destructively via bitwise move, invalidating the sender's binding.
#[repr(C)]
#[derive(Debug, PartialEq, Eq)]
pub struct PacketBuffer {
    header: PacketHeader,
    payload: [u8; PACKET_CAPACITY],
}

/// Errors that can occur during packet buffer construction or channel transfer.
#[derive(thiserror::Error, Debug, Clone, PartialEq, Eq)]
pub enum PacketError {
    /// The requested payload slice exceeds [`PACKET_CAPACITY`].
    #[error("payload length {requested} exceeds capacity {capacity}")]
    PayloadTooLarge {
        /// Requested payload length in bytes.
        requested: usize,
        /// Maximum allowable capacity in bytes.
        capacity: usize,
    },
    /// The shared memory channel slot is currently full.
    #[error("shared memory channel slot is full")]
    ChannelFull,
    /// The shared memory channel slot is empty.
    #[error("shared memory channel is empty")]
    ChannelEmpty,
    /// The shared memory channel has been closed.
    #[error("channel has been closed")]
    Closed,
}

/// Factory and coordinator for the shared memory packet handoff channel.
#[derive(Debug, Default)]
pub struct ShmHandoffChannel;

/// 64-byte cache-line wrapper preventing MESI false sharing between atomic control
/// state and inline packet payload storage.
#[repr(align(64))]
#[derive(Debug)]
struct CachePadded<T>(T);

/// Private single-slot SPSC shared state backing PacketProducer & Packet Consumer.
///
/// State protocol (`occupied`):
/// - `false` → empty; producer can write
///    - `slot` is uninitialized (undefined behavior if read)
/// - `true` → occupied; consumer can read
///    - `slot` holds a valid, initialized `PacketBuffer`
#[repr(C)]
#[derive(Debug)]
struct SharedSlot {
    occupied: CachePadded<AtomicBool>,
    slot: UnsafeCell<MaybeUninit<PacketBuffer>>,
}

// SAFETY:
// 1. `PacketBuffer` is `Send` (plain `#[repr(C)]` integers and `[u8; 1500]`).
// 2. `ShmHandoffChannel::new` creates at most one `PacketProducer` and one
//    `PacketConsumer` per `SharedSlot`, and neither endpoint implements `Clone`.
// 3. `PacketProducer::send(&mut self, ...)` and `PacketConsumer::receive(&mut self)`
//    require `&mut self`, statically serializing all producer accesses and all
//    consumer accesses without locks.
// 4. Shared `&self` queries (`is_ready`, `has_data`) access only `occupied` via
//    `AtomicBool::load(Ordering::Acquire)` and never dereference `slot`.
// 5. Ownership of `slot` transfers strictly via `Release`/`Acquire` synchronization
//    on `occupied`: producer writes `slot` only when `occupied == false` before a
//    `Release` store of `true`; consumer reads `slot` only when `occupied == true`
//    before a `Release` store of `false`.
unsafe impl Sync for SharedSlot {}

impl SharedSlot {
    fn new() -> Self {
        Self {
            occupied: CachePadded(AtomicBool::new(false)),
            slot: UnsafeCell::new(MaybeUninit::uninit()),
        }
    }
}

impl Drop for SharedSlot {
    fn drop(&mut self) {
        if *self.occupied.0.get_mut() {
            // SAFETY:
            // 1. `&mut self` in `Drop::drop` guarantees the enclosing `Arc<SharedSlot>`
            //     has reached zero strong/weak references, giving exclusive access with
            //     no concurrent threads.
            // 2. `*self.occupied.0.get_mut()` is `true`, proving a `PacketBuffer` was
            //     written by `send` and never moved out by `receive`.
            unsafe {
                self.slot.get_mut().assume_init_drop();
            }
        }
    }
}

/// Producer endpoint for transferring packet buffers into the shared memory channel.
#[derive(Debug)]
pub struct PacketProducer {
    shared_mem: Arc<SharedSlot>,
}

/// Consumer endpoint for receiving packet buffers from the shared memory channel.
#[derive(Debug)]
pub struct PacketConsumer {
    shared_mem: Arc<SharedSlot>,
}

impl PacketBuffer {
    /// Constructs a new packet buffer with the given channel ID, sequence number, and payload.
    ///
    /// Copies `data` into the inline buffer up to `PACKET_CAPACITY`.
    ///
    /// # Errors
    ///
    /// Returns [`PacketError::PayloadTooLarge`] if `data.len() > PACKET_CAPACITY`.
    pub fn new(channel_id: u16, sequence_num: u32, data: &[u8]) -> Result<Self, PacketError> {
        if data.len() > PACKET_CAPACITY {
            return Err(PacketError::PayloadTooLarge {
                requested: data.len(),
                capacity: PACKET_CAPACITY,
            });
        }
        let header = PacketHeader {
            channel_id,
            sequence_num,
            payload_len: data.len() as u16,
            flags: 0,
        };
        let mut payload = [0u8; PACKET_CAPACITY];
        payload[..data.len()].copy_from_slice(data);
        Ok(PacketBuffer { header, payload })
    }

    /// Returns a reference to the packet's header metadata.
    #[must_use]
    pub fn header(&self) -> &PacketHeader {
        &self.header
    }

    /// Returns a slice over the active payload data.
    #[must_use]
    pub fn payload(&self) -> &[u8] {
        &self.payload[..self.header.payload_len as usize]
    }

    /// Returns the length of the active payload data in bytes.
    #[must_use]
    pub fn len(&self) -> usize {
        self.header.payload_len as usize
    }

    /// Returns `true` if the packet payload has zero length.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.header.payload_len == 0
    }

    /// Returns the logical channel identifier of the packet.
    #[must_use]
    pub fn channel_id(&self) -> u16 {
        self.header.channel_id
    }

    /// Returns the sequence number of the packet.
    #[must_use]
    pub fn sequence_num(&self) -> u32 {
        self.header.sequence_num
    }
}

impl ShmHandoffChannel {
    /// Creates a connected pair of [`PacketProducer`] and [`PacketConsumer`] endpoints.
    #[must_use]
    #[expect(
        clippy::new_ret_no_self,
        reason = "Factory method creates connected producer and consumer channel endpoints"
    )]
    pub fn new() -> (PacketProducer, PacketConsumer) {
        let shared_mem = Arc::new(SharedSlot::new());
        let producer = PacketProducer {
            shared_mem: Arc::clone(&shared_mem),
        };
        let consumer = PacketConsumer { shared_mem };
        (producer, consumer)
    }
}

impl PacketProducer {
    /// Transfers ownership of `packet` into the shared channel slot.
    ///
    /// If the channel slot is already occupied, ownership is NOT consumed; the original
    /// packet buffer is returned in [`Err`].
    ///
    /// # Errors
    ///
    /// Returns [`Err(packet)`] if the slot is full, yielding the unsent buffer back to caller.
    #[expect(
        clippy::result_large_err,
        reason = "Zero-allocation affine handoff returns unconsumed buffer on error"
    )]
    pub fn send(&mut self, packet: PacketBuffer) -> Result<(), PacketBuffer> {
        if !self.is_ready() {
            return Err(packet);
        }

        // SAFETY:
        // 1. `&mut self` guarantess no concurrent `PacketProducer` call exists, and only
        //    `PacketProducer::sent` transitions `occupied` from `false` to `true`.
        // 2. Observing `occupied == false` with `Ordering::Acquire` synchronizes-with
        //    any prior `Release` store of `false` in `PacketConsumer::receive` (or
        //    initial construction), proving the consumer has finished reading `slot`
        //   and will not access `slot` again until `occupied` becomes `true`.
        // 3. `self.shared.slot.get()` is non-null, properly aligned for
        //    `MaybeUninit<PacketBuffer>`, and uniquely accessed by this thread.
        unsafe {
            (*self.shared_mem.slot.get()).write(packet);
        }

        self.shared_mem.occupied.0.store(true, Ordering::Release);
        Ok(())
    }

    /// Returns `true` if the channel slot is vacant and ready to accept a packet.
    #[must_use]
    pub fn is_ready(&self) -> bool {
        !self.shared_mem.occupied.0.load(Ordering::Acquire)
    }
}

impl PacketConsumer {
    /// Attempts to receive a packet from the channel slot by value.
    ///
    /// Consumes ownership from the channel, returning [`Some(PacketBuffer)`] if available,
    /// or [`None`] if the slot is empty.
    #[must_use]
    pub fn receive(&mut self) -> Option<PacketBuffer> {
        if !self.has_data() {
            return None;
        }

        // SAFETY:
        // 1. `&mut self` guarantees no concurrent `PacketConsumer` call exists, and only
        //    `PacketConsumer::receive` transitions `occupied` from `true` to `false`.
        // 2. Observing `occupied == true` with `Ordering::Acquire` synchronizes-with
        //    the `Release` store of `true` in `PacketProducer::send`, proving `slot`
        //    contains a fully initialized `PacketBuffer` and the producer will not
        //    access `slot` until `occupied` becomes `false`.
        // 3. `assume_init_read` performs a bitwise destructive move out of `slot`;
        //    clearing `occupied` to `false` below logically marks `slot` uninitialized,
        //    preventing double-reads or double-drops in `SharedSlot::drop`.
        let packet = unsafe { (*self.shared_mem.slot.get()).assume_init_read() };

        self.shared_mem.occupied.0.store(false, Ordering::Release);
        Some(packet)
    }

    /// Returns `true` if a packet is currently waiting in the channel slot.
    #[must_use]
    pub fn has_data(&self) -> bool {
        self.shared_mem.occupied.0.load(Ordering::Acquire)
    }
}
