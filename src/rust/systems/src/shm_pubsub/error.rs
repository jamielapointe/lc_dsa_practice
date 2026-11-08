//! Error definitions for the shared-memory pub/sub channel.
//!
//! Matches `docs/systems/shm_pubsub.md` Section 5 and `src/include/systems/shm_pubsub.hpp`.

use thiserror::Error;

/// Errors reported by `shm_pubsub` operations.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Error)]
pub enum ShmPubSubError {
    /// The requested operation has not been implemented yet.
    #[error("not implemented")]
    NotImplemented,

    /// Bad name or configuration, or a moved-from / closed handle.
    #[error("invalid argument")]
    InvalidArgument,

    /// Name is longer than `platform::MAX_SHM_NAME_LENGTH` (30 bytes).
    #[error("name too long")]
    NameTooLong,

    /// Channel with this name already exists in POSIX shared memory.
    #[error("already exists")]
    AlreadyExists,

    /// Channel with this name was not found in POSIX shared memory.
    #[error("not found")]
    NotFound,

    /// Segment layout, magic number, or version mismatch.
    #[error("layout mismatch")]
    LayoutMismatch,

    /// Another active publisher process currently holds the channel.
    #[error("publisher exists")]
    PublisherExists,

    /// Payload exceeds the configured maximum slot size.
    #[error("payload too large")]
    PayloadTooLarge,

    /// Destination buffer is smaller than the sample payload.
    #[error("buffer too small")]
    BufferTooSmall,

    /// No unread sample is available in non-blocking receive mode.
    #[error("would block")]
    WouldBlock,

    /// Timeout elapsed without receiving a sample.
    #[error("timed out")]
    TimedOut,

    /// Channel has been closed and drained.
    #[error("closed")]
    Closed,

    /// Shared memory pages could not be locked into physical RAM (`mlock`).
    #[error("memory lock failed")]
    MemoryLockFailed,

    /// Operating-system syscall failure (`errno`).
    #[error("operating-system error: {0}")]
    OsError(i32),
}
