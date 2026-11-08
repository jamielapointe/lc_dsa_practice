//! Configuration types and sample metadata for shared-memory pub/sub.

/// Smallest allowed number of ring buffer slots (power of two).
pub const MIN_SLOT_COUNT: usize = 2;

/// Largest allowed number of ring buffer slots (power of two).
pub const MAX_SLOT_COUNT: usize = 4096;

/// Largest allowed payload size per slot in bytes (64 `KiB`).
pub const MAX_SLOT_SIZE: usize = 64 * 1024;

/// Channel geometry and locking parameters.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct ShmPubSubConfig {
    /// Number of ring buffer slots; must be a power of two in `[MIN_SLOT_COUNT, MAX_SLOT_COUNT]`.
    pub slot_count: usize,
    /// Maximum payload bytes per slot; in `[1, MAX_SLOT_SIZE]`.
    pub slot_size: usize,
    /// Whether to wire pages into physical RAM with `mlock`. Defaults to true.
    pub memory_lock: bool,
}

impl ShmPubSubConfig {
    /// Creates a new configuration with `memory_lock` enabled.
    #[must_use]
    pub const fn new(slot_count: usize, slot_size: usize) -> Self {
        Self {
            slot_count,
            slot_size,
            memory_lock: true,
        }
    }

    /// Creates a new configuration with explicit `memory_lock` setting.
    #[must_use]
    pub const fn with_memory_lock(slot_count: usize, slot_size: usize, memory_lock: bool) -> Self {
        Self {
            slot_count,
            slot_size,
            memory_lock,
        }
    }

    /// Validates whether the configuration satisfies SHMPS-REQ-001.
    #[must_use]
    pub const fn is_valid(&self) -> bool {
        self.slot_count.is_power_of_two()
            && self.slot_count >= MIN_SLOT_COUNT
            && self.slot_count <= MAX_SLOT_COUNT
            && self.slot_size >= 1
            && self.slot_size <= MAX_SLOT_SIZE
    }
}

impl Default for ShmPubSubConfig {
    fn default() -> Self {
        Self {
            slot_count: MIN_SLOT_COUNT,
            slot_size: 256,
            memory_lock: true,
        }
    }
}

/// Metadata of a sample received by a subscriber.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ShmSample {
    /// Monotonic sequence number assigned by the publisher, starting at 1.
    pub sequence: u64,
    /// Number of payload bytes written into the destination buffer.
    pub size: usize,
    /// Number of samples skipped since the previous receive.
    pub missed: u64,
}

/// How a subscriber handles unread samples when falling behind (SHMPS-REQ-027).
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum ShmReadMode {
    /// Sequential continuity: receives every sample in order while keeping up;
    /// skips to newest only when lapped or torn.
    #[default]
    InOrder,

    /// Freshness priority: always skips to the newest published sample on every receive.
    Latest,
}
