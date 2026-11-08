//! Single-Producer / Multi-Subscriber (SPMS) Shared-Memory Pub/Sub IPC.
//!
//! Provides lock-free, zero-copy, real-time shared-memory pub/sub communication
//! across threads and processes adhering strictly to `docs/systems/shm_pubsub.md`.

pub mod backoff;
pub mod config;
pub mod error;
pub mod layout;
pub mod posix;

use std::sync::Arc;
use std::sync::atomic::Ordering;
use std::time::{Duration, Instant};

pub use backoff::Backoff;
pub use config::{MAX_SLOT_COUNT, MAX_SLOT_SIZE, MIN_SLOT_COUNT, ShmPubSubConfig, ShmReadMode, ShmSample};
pub use error::ShmPubSubError;
pub use layout::ChannelView;
use posix::SharedMemoryRegion;

/// Type alias for `ShmPublisher` for Producer/Consumer nomenclature parity.
pub type ShmProducer = ShmPublisher;

/// Type alias for `ShmSubscriber` for Producer/Consumer nomenclature parity.
pub type ShmConsumer = ShmSubscriber;

/// Type alias for `ShmPubSubConfig`.
pub type ShmConfig = ShmPubSubConfig;

/// A process's handle to a named shared-memory channel.
///
/// Owns the underlying `SharedMemoryRegion`. Destroying `ShmChannel` unmaps
/// memory without unlinking the channel name (SHMPS-REQ-019).
#[derive(Clone, Debug)]
pub struct ShmChannel {
    name: String,
    region: Arc<SharedMemoryRegion>,
    view: ChannelView,
}

impl ShmChannel {
    /// Creates, truncates, pre-faults, wires (`mlock`), and initializes a new channel.
    ///
    /// # Errors
    /// Returns `ShmPubSubError` if the name or geometry is invalid, if the channel
    /// already exists, or if memory locking fails.
    pub fn create(name: &str, config: ShmPubSubConfig) -> Result<Self, ShmPubSubError> {
        posix::validate_name(name)?;
        if !config.is_valid() {
            return Err(ShmPubSubError::InvalidArgument);
        }

        let bytes = layout::region_bytes(&config);
        let region = SharedMemoryRegion::create(name, bytes, config.memory_lock)?;
        layout::initialize_layout(region.base(), &config);

        let view = ChannelView {
            base: region.base(),
            slot_mask: config.slot_count - 1,
            slot_size: config.slot_size,
            slot_stride: layout::slot_stride_for(config.slot_size),
        };

        Ok(Self {
            name: name.to_string(),
            region: Arc::new(region),
            view,
        })
    }

    /// Opens and maps an existing channel, discovering its geometry from the header.
    ///
    /// # Errors
    /// Returns `ShmPubSubError` if the name is invalid, the channel is missing,
    /// the segment is corrupt/incompatible, or memory locking fails.
    pub fn open(name: &str) -> Result<Self, ShmPubSubError> {
        Self::open_with_options(name, true)
    }

    /// Opens an existing channel with explicit control over `memory_lock`.
    ///
    /// # Errors
    /// Returns `ShmPubSubError` if the name is invalid, the channel is missing,
    /// the segment is corrupt/incompatible, or memory locking fails.
    #[allow(clippy::cast_ptr_alignment)]
    pub fn open_with_options(name: &str, memory_lock: bool) -> Result<Self, ShmPubSubError> {
        posix::validate_name(name)?;
        let region = SharedMemoryRegion::open(name, memory_lock)?;

        if !layout::layout_is_valid(region.base(), region.size()) {
            return Err(ShmPubSubError::LayoutMismatch);
        }

        // SAFETY: region is validated by layout_is_valid.
        let header = unsafe { &*(region.base().cast::<layout::ChannelHeader>()) };
        let slot_count = header.slot_count as usize;
        let slot_size = header.slot_size as usize;
        let slot_stride = header.slot_stride as usize;

        let view = ChannelView {
            base: region.base(),
            slot_mask: slot_count - 1,
            slot_size,
            slot_stride,
        };

        Ok(Self {
            name: name.to_string(),
            region: Arc::new(region),
            view,
        })
    }

    /// Unlinks the named channel from the POSIX shared-memory filesystem (SHMPS-REQ-006).
    ///
    /// # Errors
    /// Returns `NotFound` if no segment with this name exists, or `OsError`.
    pub fn unlink(name: &str) -> Result<(), ShmPubSubError> {
        SharedMemoryRegion::unlink(name)
    }

    /// Returns the name this channel was opened with.
    #[must_use]
    pub fn name(&self) -> &str {
        &self.name
    }

    /// Returns the geometry configuration of this channel.
    #[must_use]
    pub fn config(&self) -> ShmPubSubConfig {
        let header = self.view.header();
        ShmPubSubConfig {
            slot_count: header.slot_count as usize,
            slot_size: header.slot_size as usize,
            memory_lock: true,
        }
    }

    /// Closes the channel for all processes: stops publishing, wakes blocked readers,
    /// and allows readers to drain committed samples (SHMPS-REQ-014).
    pub fn close(&self) {
        self.view.header().closed.store(1, Ordering::Release);
    }

    /// Returns whether any process has called `close()`.
    #[must_use]
    pub fn is_closed(&self) -> bool {
        self.view.header().closed.load(Ordering::Acquire) != 0
    }

    /// Internal accessor for the channel's view.
    #[must_use]
    pub const fn view(&self) -> ChannelView {
        self.view
    }
}

/// The exclusive single-publisher handle for a channel. Wait-free.
///
/// Guaranteed to never block, loop on shared state, or invoke syscalls during steady-state
/// operations. Automatically releases the publisher role on `Drop` (SHMPS-REQ-007).
#[derive(Debug)]
pub struct ShmPublisher {
    _region: Arc<SharedMemoryRegion>,
    view: ChannelView,
    next_sequence: u64,
    owner_pid: i64,
}

impl ShmPublisher {
    /// Attaches as the single publisher to `channel`.
    ///
    /// Claims ownership via atomic CAS on `publisher_pid`. If held by a dead process,
    /// safely takes over and recovers sequence numbering past any uncommitted slot (SHMPS-REQ-020).
    ///
    /// # Errors
    /// Returns `PublisherExists` if another live publisher is attached, or `Closed` if closed.
    pub fn attach(channel: &ShmChannel) -> Result<Self, ShmPubSubError> {
        let view = channel.view();
        let header = view.header();

        if header.closed.load(Ordering::Acquire) != 0 {
            return Err(ShmPubSubError::Closed);
        }

        let me = SharedMemoryRegion::current_process_id();
        let mut holder = 0;

        if let Err(actual) = header
            .publisher_pid
            .compare_exchange(holder, me, Ordering::AcqRel, Ordering::Acquire)
        {
            holder = actual;
            if holder == me || SharedMemoryRegion::process_alive(holder) {
                return Err(ShmPubSubError::PublisherExists);
            }

            // Holder is dead: attempt takeover CAS.
            if header
                .publisher_pid
                .compare_exchange(holder, me, Ordering::AcqRel, Ordering::Acquire)
                .is_err()
            {
                return Err(ShmPubSubError::PublisherExists);
            }
        }

        let next_sequence = layout::recover_next_sequence(&view);
        Ok(Self {
            _region: Arc::clone(&channel.region),
            view,
            next_sequence,
            owner_pid: me,
        })
    }

    /// Copies `payload` into the next slot and assigns a monotonic sequence number.
    ///
    /// Strictly wait-free (SHMPS-REQ-009, SHMPS-REQ-025): executes in $O(\text{payload})$ steps
    /// with zero system calls, zero mutexes, and zero CAS retry loops.
    ///
    /// # Errors
    /// Returns `PayloadTooLarge` if `payload.len() > slot_size`, or `Closed` if the channel is closed.
    #[allow(clippy::cast_possible_truncation)]
    pub fn publish(&mut self, payload: &[u8]) -> Result<u64, ShmPubSubError> {
        if payload.len() > self.view.slot_size {
            return Err(ShmPubSubError::PayloadTooLarge);
        }

        let header = self.view.header();
        if header.closed.load(Ordering::Relaxed) != 0 {
            return Err(ShmPubSubError::Closed);
        }

        let sequence = self.next_sequence;
        let slot = self.view.slot_for(sequence);

        // (W1) Mark slot in progress with odd sequence.
        slot.header.seq.store((2 * sequence) - 1, Ordering::Relaxed);

        // (W2) Release fence guarantees odd mark is visible before any payload stores.
        core::sync::atomic::fence(Ordering::Release);

        // (W3) Store payload words and size as relaxed atomics.
        layout::store_payload(slot.words, payload);
        slot.header.size.store(payload.len() as u32, Ordering::Relaxed);

        // (W4) Mark slot committed with even sequence.
        slot.header.seq.store(2 * sequence, Ordering::Release);

        // (W5) Publish head sequence to subscribers.
        header.published.store(sequence, Ordering::Release);

        self.next_sequence = sequence + 1;
        Ok(sequence)
    }

    /// Returns the sequence number that will be assigned to the next sample.
    #[must_use]
    pub const fn next_sequence(&self) -> u64 {
        self.next_sequence
    }

    /// Closes the channel.
    pub fn close(&self) {
        self.view.header().closed.store(1, Ordering::Release);
    }
}

impl Drop for ShmPublisher {
    fn drop(&mut self) {
        // Fork-safety: Only release if this process claimed the role.
        if self.owner_pid == SharedMemoryRegion::current_process_id() {
            let _ = self.view.header().publisher_pid.compare_exchange(
                self.owner_pid,
                0,
                Ordering::Release,
                Ordering::Relaxed,
            );
        }
    }
}

/// Outcome of attempting to read a specific slot in the seqlock protocol.
enum ReadOutcome {
    Ok(usize),
    BufferTooSmall,
    Overwritten,
    NotReady,
}

/// Executes the R1-R4 lock-free seqlock read protocol on one slot.
fn read_slot(view: &ChannelView, sequence: u64, buffer: &mut [u8]) -> ReadOutcome {
    let slot = view.slot_for(sequence);
    let wanted = 2 * sequence;

    // (R1) Initial sequence check (Acquire).
    let v1 = slot.header.seq.load(Ordering::Acquire);
    if v1 != wanted {
        return if v1 > wanted {
            ReadOutcome::Overwritten
        } else {
            ReadOutcome::NotReady
        };
    }

    // (R2) Load size and copy payload words (Relaxed).
    let size = slot.header.size.load(Ordering::Relaxed) as usize;
    let size_plausible = size <= view.slot_size;
    let fits = size_plausible && size <= buffer.len();

    if fits {
        layout::load_payload(slot.words, &mut buffer[..size]);
    }

    // (R3) Acquire fence prevents payload loads from reordering past the second check.
    core::sync::atomic::fence(Ordering::Acquire);

    // (R4) Second sequence check (Relaxed).
    let v2 = slot.header.seq.load(Ordering::Relaxed);
    if v2 != v1 || !size_plausible {
        return ReadOutcome::Overwritten;
    }

    if !fits {
        return ReadOutcome::BufferTooSmall;
    }

    ReadOutcome::Ok(size)
}

/// An independent subscriber handle with a private cursor. Lock-free.
///
/// Holds no shared consumer state: crashing subscribers do not affect other consumers or publisher.
#[derive(Debug)]
pub struct ShmSubscriber {
    _region: Arc<SharedMemoryRegion>,
    view: ChannelView,
    next_sequence: u64,
    mode: ShmReadMode,
    slot_count: usize,
}

impl ShmSubscriber {
    /// Attaches a new subscriber to `channel`.
    ///
    /// Starts at the next sample to be published after attaching (no history replay, A-6).
    ///
    /// # Errors
    /// Returns `ShmPubSubError` if channel cannot be accessed.
    pub fn attach(channel: &ShmChannel, mode: ShmReadMode) -> Result<Self, ShmPubSubError> {
        let view = channel.view();
        let newest = view.header().published.load(Ordering::Acquire);
        let slot_count = view.slot_mask + 1;

        Ok(Self {
            _region: Arc::clone(&channel.region),
            view,
            next_sequence: newest + 1,
            mode,
            slot_count,
        })
    }

    /// Attempts to read the next sample without blocking (SHMPS-REQ-012).
    ///
    /// Detects overruns and torn reads via per-slot seqlocks, skipping to the newest sample.
    ///
    /// # Errors
    /// Returns `WouldBlock` if nothing new is published, `BufferTooSmall` if `buffer` is too small
    /// (sample is not consumed, cursor not advanced), or `Closed` if closed and drained.
    pub fn try_receive(&mut self, buffer: &mut [u8]) -> Result<ShmSample, ShmPubSubError> {
        let header = self.view.header();
        let mut cursor = self.next_sequence;
        let mut missed = 0u64;
        let mut force_newest = false;

        loop {
            // Must load closed BEFORE published (SHMPS-REQ-014 drain semantics).
            let closed = header.closed.load(Ordering::Acquire) != 0;
            let head = header.published.load(Ordering::Acquire);

            if head < cursor {
                return Err(if closed {
                    ShmPubSubError::Closed
                } else {
                    ShmPubSubError::WouldBlock
                });
            }

            let lag = head - cursor;
            let lapped = lag >= self.slot_count as u64;

            if lag > 0 && (lapped || force_newest || self.mode == ShmReadMode::Latest) {
                missed += lag;
                cursor = head;
            }

            match read_slot(&self.view, cursor, buffer) {
                ReadOutcome::Ok(size) => {
                    self.next_sequence = cursor + 1;
                    return Ok(ShmSample {
                        sequence: cursor,
                        size,
                        missed,
                    });
                }
                ReadOutcome::BufferTooSmall => {
                    // Buffer too small does NOT advance cursor or consume sample.
                    return Err(ShmPubSubError::BufferTooSmall);
                }
                ReadOutcome::NotReady => {
                    return Err(if closed {
                        ShmPubSubError::Closed
                    } else {
                        ShmPubSubError::WouldBlock
                    });
                }
                ReadOutcome::Overwritten => {
                    force_newest = true;
                }
            }
        }
    }

    /// Waits up to `timeout` for a sample using bounded backoff (SHMPS-REQ-013).
    ///
    /// # Errors
    /// Returns `TimedOut` if `timeout` expires, `BufferTooSmall` if `buffer` is smaller than payload,
    /// or `Closed` if the channel is closed.
    pub fn receive_for(&mut self, buffer: &mut [u8], timeout: Duration) -> Result<ShmSample, ShmPubSubError> {
        self.receive_until(buffer, Some(Instant::now() + timeout))
    }

    /// Alias for `receive_for`.
    ///
    /// # Errors
    /// Returns `TimedOut` if `timeout` expires, `BufferTooSmall` if `buffer` is smaller than payload,
    /// or `Closed` if the channel is closed.
    pub fn receive_timeout(&mut self, buffer: &mut [u8], timeout: Duration) -> Result<ShmSample, ShmPubSubError> {
        self.receive_for(buffer, timeout)
    }

    /// Blocks until a sample arrives or the channel is closed (SHMPS-REQ-013).
    ///
    /// # Errors
    /// Returns `BufferTooSmall` if `buffer` is smaller than payload, or `Closed` if channel is closed.
    pub fn receive(&mut self, buffer: &mut [u8]) -> Result<ShmSample, ShmPubSubError> {
        self.receive_until(buffer, None)
    }

    /// Internal polling loop with bounded exponential backoff.
    fn receive_until(&mut self, buffer: &mut [u8], deadline: Option<Instant>) -> Result<ShmSample, ShmPubSubError> {
        let mut backoff = Backoff::new();
        loop {
            match self.try_receive(buffer) {
                Ok(sample) => return Ok(sample),
                Err(ShmPubSubError::WouldBlock) => {
                    if deadline.is_some_and(|dl| Instant::now() >= dl) {
                        return Err(ShmPubSubError::TimedOut);
                    }
                    backoff.wait(deadline);
                }
                Err(err) => return Err(err),
            }
        }
    }

    /// Returns the sequence number this subscriber expects next.
    #[must_use]
    pub const fn next_sequence(&self) -> u64 {
        self.next_sequence
    }

    /// Returns the active read mode.
    #[must_use]
    pub const fn mode(&self) -> ShmReadMode {
        self.mode
    }
}
