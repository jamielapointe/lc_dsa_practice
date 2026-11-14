//! Crubit interop Module 3: heap-owning Rust RAII buffer (`String`, `Vec<u8>`, `Box<T>`) with deterministic `Drop`.

use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;

static LIVE_PACKET_BUFFERS: AtomicUsize = AtomicUsize::new(0);

/// Summary statistics tracked inside a heap-allocated [`TelemetryPacketBuffer`].
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct PacketStats {
    /// Total bytes appended to the packet buffer.
    pub appended_bytes: usize,
    /// Running XOR checksum across all appended bytes.
    pub xor_checksum: u32,
}

/// Move-only RAII telemetry packet buffer whose lifetime is tracked atomically across FFI drops.
#[derive(Debug)]
pub struct TelemetryPacketBuffer {
    label: String,
    payload: Vec<u8>,
    stats: Box<PacketStats>,
}

impl TelemetryPacketBuffer {
    /// Creates a new [`TelemetryPacketBuffer`] with pre-allocated `capacity` and increments the live counter.
    #[must_use]
    pub fn create(label: &str, capacity: usize) -> Self {
        LIVE_PACKET_BUFFERS.fetch_add(1, Ordering::SeqCst);
        Self {
            label: label.to_owned(),
            payload: Vec::with_capacity(capacity),
            stats: Box::new(PacketStats::default()),
        }
    }

    /// Appends `byte` to the buffer, updates the XOR checksum, and returns the new byte count.
    pub fn append_byte(&mut self, byte: u8) -> usize {
        self.payload.push(byte);
        self.stats.appended_bytes = self.payload.len();
        self.stats.xor_checksum ^= u32::from(byte);
        self.payload.len()
    }

    /// Returns the running XOR checksum of all appended bytes.
    #[must_use]
    pub fn checksum(&self) -> u32 {
        self.stats.xor_checksum
    }

    /// Returns the byte length of the buffer label.
    #[must_use]
    pub const fn label_len(&self) -> usize {
        self.label.len()
    }
}

impl Drop for TelemetryPacketBuffer {
    fn drop(&mut self) {
        LIVE_PACKET_BUFFERS.fetch_sub(1, Ordering::SeqCst);
    }
}

/// Returns the current number of live [`TelemetryPacketBuffer`] instances.
#[must_use]
pub fn live_packet_buffers() -> usize {
    LIVE_PACKET_BUFFERS.load(Ordering::SeqCst)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tracks_live_instances_across_drop() {
        let before = live_packet_buffers();
        {
            let mut buf = TelemetryPacketBuffer::create("imu-0", 16);
            assert!(live_packet_buffers() > before);
            assert_eq!(buf.label_len(), 5);
            assert_eq!(buf.append_byte(0xaa), 1);
            assert_eq!(buf.append_byte(0x55), 2);
            assert_eq!(buf.checksum(), 0xff);
        }
        assert_eq!(live_packet_buffers(), before);
    }
}
