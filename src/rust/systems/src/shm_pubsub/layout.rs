//! Shared-memory binary layout, header structures, and seqlock synchronization.
//!
//! Mirrors `src/include/systems/shm_pubsub.hpp` and `docs/systems/shm_pubsub.md`.
//!
//! # Binary Layout
//!
//! ```text
//! Offset 0: ChannelHeader (3 cache lines)
//!   Line 0: magic (AtomicU64), layout_version (u32), slot_count (u32), slot_size (u32), slot_stride (u32), pad
//!   Line 1: published (AtomicU64), publisher_pid (AtomicI64), pad
//!   Line 2: closed (AtomicU32), pad
//! Offset 3*CACHE_LINE_SIZE + i * slot_stride: Slot i
//!   SlotHeader (16 bytes: seq (AtomicU64), size (AtomicU32), reserved (u32))
//!   Payload buffer: slot_size rounded to 8 bytes (as relaxed AtomicU64 words)
//!   Padding up to slot_stride (cache-line multiple)
//! ```

#![allow(unsafe_code)]

use crate::platform::CACHE_LINE_SIZE;
use crate::shm_pubsub::config::ShmPubSubConfig;
use core::sync::atomic::{AtomicI64, AtomicU32, AtomicU64, Ordering};

/// "SHMPSUB1" in ASCII. Identifies a valid channel segment.
pub const MAGIC: u64 = 0x5348_4d50_5355_4231;

/// Layout version for segment format validation (SHMPS-REQ-005).
pub const LAYOUT_VERSION: u32 = 1;

/// Word size in bytes for payload copying (sizeof(u64)).
pub const WORD_BYTES: usize = 8;

/// Size of the slot metadata header in bytes (seq: 8, size: 4, reserved: 4).
pub const SLOT_HEADER_BYTES: usize = 16;

/// Total bytes occupied by the 3-cache-line channel header.
pub const HEADER_BYTES: usize = 3 * CACHE_LINE_SIZE;

/// Returns `value` rounded up to the nearest multiple of `multiple`.
#[must_use]
pub const fn round_up(value: usize, multiple: usize) -> usize {
    value.div_ceil(multiple) * multiple
}

/// Computes the stride in bytes from one slot to the next, padded to `CACHE_LINE_SIZE`.
#[must_use]
pub const fn slot_stride_for(slot_size: usize) -> usize {
    round_up(SLOT_HEADER_BYTES + round_up(slot_size, WORD_BYTES), CACHE_LINE_SIZE)
}

/// Computes the total shared-memory segment size in bytes for a given configuration.
#[must_use]
pub const fn region_bytes(config: &ShmPubSubConfig) -> usize {
    HEADER_BYTES + (config.slot_count * slot_stride_for(config.slot_size))
}

/// Channel-wide state residing at offset 0 of the shared mapping.
/// Spans exactly 3 distinct cache lines to avoid false-sharing invalidations.
#[repr(C)]
pub struct ChannelHeader {
    // --- Cache Line 0: Identity and geometry (read-only after create) ---
    /// Segment magic number (written last with Release order during initialization).
    pub magic: AtomicU64,
    /// Binary layout version.
    pub layout_version: u32,
    /// Number of ring buffer slots (power of two).
    pub slot_count: u32,
    /// Maximum payload size per slot in bytes.
    pub slot_size: u32,
    /// Distance in bytes between consecutive slot starts.
    pub slot_stride: u32,
    _pad0: [u8; CACHE_LINE_SIZE - 24],

    // --- Cache Line 1: Active publisher state (polled by subscribers) ---
    /// Sequence number of the newest committed sample (0 = nothing published yet).
    pub published: AtomicU64,
    /// PID of the process holding the single-publisher role (0 = vacant).
    pub publisher_pid: AtomicI64,
    _pad1: [u8; CACHE_LINE_SIZE - 16],

    // --- Cache Line 2: Channel lifecycle control ---
    /// 1 if channel is closed; 0 otherwise.
    pub closed: AtomicU32,
    _pad2: [u8; CACHE_LINE_SIZE - 4],
}

// Compile-time sanity checks on binary layout.
const _: () = {
    assert!(core::mem::size_of::<ChannelHeader>() == 3 * CACHE_LINE_SIZE);
    assert!(core::mem::offset_of!(ChannelHeader, magic) == 0);
    assert!(core::mem::offset_of!(ChannelHeader, published) == CACHE_LINE_SIZE);
    assert!(core::mem::offset_of!(ChannelHeader, publisher_pid) == CACHE_LINE_SIZE + 8);
    assert!(core::mem::offset_of!(ChannelHeader, closed) == 2 * CACHE_LINE_SIZE);
};

/// Metadata header preceding each slot's payload.
#[repr(C)]
pub struct SlotHeader {
    /// Seqlock sequence counter:
    /// - `0`: slot unwritten.
    /// - `2s - 1` (odd): write of sample `s` in progress.
    /// - `2s` (even): slot holds committed, stable sample `s`.
    pub seq: AtomicU64,
    /// Length of the payload currently stored in this slot.
    pub size: AtomicU32,
    /// Reserved padding to ensure payload starts at an 8-byte aligned offset.
    pub reserved: u32,
}

const _: () = {
    assert!(core::mem::size_of::<SlotHeader>() == 16);
    assert!(core::mem::offset_of!(SlotHeader, seq) == 0);
    assert!(core::mem::offset_of!(SlotHeader, size) == 8);
    assert!(core::mem::offset_of!(SlotHeader, reserved) == 12);
};

/// Lightweight pointer view of a slot inside the mapped region.
pub struct SlotRef<'a> {
    /// Reference to the slot's seqlock header.
    pub header: &'a SlotHeader,
    /// Payload buffer as relaxed atomic words.
    pub words: &'a [AtomicU64],
}

/// Cached mapping geometry used by handles for address arithmetic without shared memory reads.
#[derive(Clone, Copy, Debug)]
pub struct ChannelView {
    /// Pointer to the start of the mapping (`ChannelHeader`).
    pub base: *mut u8,
    /// `slot_count - 1` bitmask for fast power-of-two slot indexing.
    pub slot_mask: usize,
    /// Maximum payload bytes per slot.
    pub slot_size: usize,
    /// Distance in bytes between consecutive slot starts.
    pub slot_stride: usize,
}

// SAFETY: The shared mapping contains only thread-safe atomics and immutable geometry.
unsafe impl Send for ChannelView {}
// SAFETY: The shared mapping contains only thread-safe atomics and immutable geometry.
unsafe impl Sync for ChannelView {}

impl ChannelView {
    /// Returns a reference to the `ChannelHeader` at offset 0.
    #[must_use]
    #[allow(clippy::cast_ptr_alignment)]
    pub fn header(&self) -> &ChannelHeader {
        // SAFETY: self.base points to a valid mapped segment of at least HEADER_BYTES,
        // properly aligned to at least CACHE_LINE_SIZE (mmap guarantees page alignment).
        unsafe { &*(self.base.cast::<ChannelHeader>()) }
    }

    /// Computes the slot reference for a given sample sequence number (`sequence >= 1`).
    #[must_use]
    #[allow(clippy::cast_ptr_alignment, clippy::cast_possible_truncation)]
    pub fn slot_for(&self, sequence: u64) -> SlotRef<'_> {
        let index = ((sequence.wrapping_sub(1)) as usize) & self.slot_mask;
        let slot_offset = HEADER_BYTES + (index * self.slot_stride);

        // SAFETY: slot_offset is strictly bounded within the mapped region because
        // index <= slot_mask < slot_count, and slot starts are cache-line aligned (at least 8-byte aligned).
        // payload_ptr is 8-byte aligned at offset 16 from slot start, with word_count words within the slot.
        let (header, words) = unsafe {
            let slot_ptr = self.base.add(slot_offset);
            let header = &*(slot_ptr.cast::<SlotHeader>());
            let payload_ptr = slot_ptr.add(SLOT_HEADER_BYTES).cast::<AtomicU64>();
            let word_count = round_up(self.slot_size, WORD_BYTES) / WORD_BYTES;
            let words = core::slice::from_raw_parts(payload_ptr, word_count);
            (header, words)
        };

        SlotRef { header, words }
    }
}

/// Writes `payload` into `words` as relaxed 64-bit atomic stores, zero-padding any remainder.
pub fn store_payload(words: &[AtomicU64], payload: &[u8]) {
    let full_words = payload.len() / WORD_BYTES;
    for (i, word_elem) in words.iter().enumerate().take(full_words) {
        let mut chunk = [0u8; 8];
        chunk.copy_from_slice(&payload[i * WORD_BYTES..(i + 1) * WORD_BYTES]);
        let val = u64::from_ne_bytes(chunk);
        word_elem.store(val, Ordering::Relaxed);
    }

    let tail = payload.len() % WORD_BYTES;
    if tail != 0 {
        let mut chunk = [0u8; 8];
        chunk[..tail].copy_from_slice(&payload[full_words * WORD_BYTES..]);
        let val = u64::from_ne_bytes(chunk);
        words[full_words].store(val, Ordering::Relaxed);
    }
}

/// Reads `out.len()` bytes from `words` as relaxed 64-bit atomic loads into `out`.
pub fn load_payload(words: &[AtomicU64], out: &mut [u8]) {
    let full_words = out.len() / WORD_BYTES;
    for (i, word_elem) in words.iter().enumerate().take(full_words) {
        let val = word_elem.load(Ordering::Relaxed);
        out[i * WORD_BYTES..(i + 1) * WORD_BYTES].copy_from_slice(&val.to_ne_bytes());
    }

    let tail = out.len() % WORD_BYTES;
    if tail != 0 {
        let val = words[full_words].load(Ordering::Relaxed);
        let bytes = val.to_ne_bytes();
        out[full_words * WORD_BYTES..].copy_from_slice(&bytes[..tail]);
    }
}

/// Recovers the next sequence number to assign after a publisher crash (SHMPS-REQ-020).
/// Scans `published` and all slot seqlocks, returning `max(published, max(seq / 2)) + 1`.
#[must_use]
#[allow(clippy::cast_possible_truncation)]
pub fn recover_next_sequence(view: &ChannelView) -> u64 {
    let mut newest = view.header().published.load(Ordering::Acquire);
    let slot_count = view.slot_mask + 1;
    for sequence in 1..=slot_count {
        let lock = view.slot_for(sequence as u64).header.seq.load(Ordering::Acquire);
        newest = newest.max(lock / 2);
    }
    newest + 1
}

/// Initializes a newly created, zeroed shared-memory mapping with headers and slot geometry.
#[allow(clippy::cast_ptr_alignment, clippy::cast_possible_truncation)]
pub fn initialize_layout(base: *mut u8, config: &ShmPubSubConfig) {
    let slot_stride = slot_stride_for(config.slot_size);
    // SAFETY: base is a newly mapped, page-aligned segment of size region_bytes(config).
    let header = unsafe { &mut *(base.cast::<ChannelHeader>()) };

    header.magic.store(0, Ordering::Relaxed);
    header.layout_version = LAYOUT_VERSION;
    header.slot_count = config.slot_count as u32;
    header.slot_size = config.slot_size as u32;
    header.slot_stride = slot_stride as u32;
    header.published.store(0, Ordering::Relaxed);
    header.publisher_pid.store(0, Ordering::Relaxed);
    header.closed.store(0, Ordering::Relaxed);

    let view = ChannelView {
        base,
        slot_mask: config.slot_count - 1,
        slot_size: config.slot_size,
        slot_stride,
    };

    for sequence in 1..=config.slot_count {
        let slot = view.slot_for(sequence as u64);
        slot.header.seq.store(0, Ordering::Relaxed);
        slot.header.size.store(0, Ordering::Relaxed);
    }

    // Release store magic last so concurrent `open()` sees fully initialized fields.
    header.magic.store(MAGIC, Ordering::Release);
}

/// Checks if an existing mapped region conforms to a valid `ShmChannel` layout (SHMPS-REQ-005).
#[must_use]
#[allow(clippy::cast_ptr_alignment)]
pub fn layout_is_valid(base: *mut u8, size: usize) -> bool {
    if base.is_null() || size < HEADER_BYTES {
        return false;
    }

    // SAFETY: base is non-null and size >= HEADER_BYTES.
    let header = unsafe { &*(base.cast::<ChannelHeader>()) };
    if header.magic.load(Ordering::Acquire) != MAGIC || header.layout_version != LAYOUT_VERSION {
        return false;
    }

    let config = ShmPubSubConfig {
        slot_count: header.slot_count as usize,
        slot_size: header.slot_size as usize,
        memory_lock: true,
    };

    if !config.is_valid() {
        return false;
    }

    let expected_stride = slot_stride_for(config.slot_size);
    if header.slot_stride as usize != expected_stride {
        return false;
    }

    size >= region_bytes(&config)
}
