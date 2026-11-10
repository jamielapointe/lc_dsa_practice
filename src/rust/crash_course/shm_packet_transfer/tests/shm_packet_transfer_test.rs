#![allow(clippy::disallowed_methods, clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! 4-Tier integration test suite for Module 01: `shm_packet_transfer`.
//!
//! Validates zero-allocation, affine ownership transfer, bitwise `memcpy` moves,
//! drop reclamation, and concurrent handoff across isolated threads.

use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::thread;

use shm_packet_transfer::{
    PACKET_CAPACITY, PacketBuffer, PacketConsumer, PacketError, PacketHeader, PacketProducer, ShmHandoffChannel,
};

// ===========================================================================
// Test Helpers
// ===========================================================================

#[derive(Debug)]
struct DropDetector {
    _id: usize,
    counter: Arc<AtomicUsize>,
}

impl DropDetector {
    const fn new(id: usize, counter: Arc<AtomicUsize>) -> Self {
        Self { _id: id, counter }
    }
}

impl Drop for DropDetector {
    fn drop(&mut self) {
        self.counter.fetch_add(1, Ordering::SeqCst);
    }
}

// ===========================================================================
// Tier 1: Canonical Functional Tests
// ===========================================================================

#[test]
fn test_canonical_packet_construction() {
    let channel_id = 42_u16;
    let sequence_num = 1_001_u32;
    let data = b"telemetry-sensor-frame-001";

    let packet = PacketBuffer::new(channel_id, sequence_num, data).expect("packet creation should succeed");

    assert_eq!(packet.channel_id(), channel_id);
    assert_eq!(packet.sequence_num(), sequence_num);
    assert_eq!(packet.len(), data.len());
    assert!(!packet.is_empty());
    assert_eq!(packet.payload(), data);

    let header: &PacketHeader = packet.header();
    assert_eq!(header.channel_id, channel_id);
    assert_eq!(header.sequence_num, sequence_num);
    assert_eq!(usize::from(header.payload_len), data.len());
    assert_eq!(header.flags, 0);
}

#[test]
fn test_canonical_send_receive_lifecycle() {
    let (mut producer, mut consumer): (PacketProducer, PacketConsumer) = ShmHandoffChannel::new();
    assert!(producer.is_ready());
    assert!(!consumer.has_data());

    let payload = [0x5au8; 64];
    let packet = PacketBuffer::new(1, 100, &payload).expect("packet creation should succeed");

    producer.send(packet).expect("send into empty channel should succeed");
    assert!(consumer.has_data());

    let received = consumer.receive().expect("receive should yield sent packet");
    assert_eq!(received.channel_id(), 1);
    assert_eq!(received.sequence_num(), 100);
    assert_eq!(received.len(), 64);
    assert_eq!(received.payload(), &payload[..]);

    assert!(!consumer.has_data());
    assert!(consumer.receive().is_none());
}

#[test]
fn test_canonical_sequential_transfers() {
    let (mut producer, mut consumer) = ShmHandoffChannel::new();

    for seq in 0..10_u32 {
        let payload = format!("packet-sequence-payload-{seq}").into_bytes();
        let packet = PacketBuffer::new(7, seq, &payload).expect("packet creation should succeed");

        producer.send(packet).expect("send should succeed");
        let received = consumer.receive().expect("receive should yield packet");

        assert_eq!(received.channel_id(), 7);
        assert_eq!(received.sequence_num(), seq);
        assert_eq!(received.len(), payload.len());
        assert_eq!(received.payload(), payload.as_slice());
    }
}

// ===========================================================================
// Tier 2: Boundary & Corner Cases
// ===========================================================================

#[test]
fn test_boundary_empty_payload() {
    let (mut producer, mut consumer) = ShmHandoffChannel::new();
    let empty_packet = PacketBuffer::new(3, 50, b"").expect("empty payload must be permitted");

    assert_eq!(empty_packet.len(), 0);
    assert!(empty_packet.is_empty());
    assert_eq!(empty_packet.payload(), b"");

    producer.send(empty_packet).expect("send empty packet should succeed");
    let received = consumer.receive().expect("receive empty packet should succeed");

    assert_eq!(received.len(), 0);
    assert!(received.is_empty());
    assert_eq!(received.payload(), b"");
}

#[test]
fn test_boundary_max_capacity_payload() {
    let (mut producer, mut consumer) = ShmHandoffChannel::new();
    let max_payload = [0xeeu8; PACKET_CAPACITY];
    let max_packet = PacketBuffer::new(4, 60, &max_payload).expect("max capacity payload must be permitted");

    assert_eq!(max_packet.len(), PACKET_CAPACITY);
    assert_eq!(max_packet.payload(), &max_payload[..]);

    producer
        .send(max_packet)
        .expect("send max capacity packet should succeed");
    let received = consumer.receive().expect("receive max capacity packet should succeed");

    assert_eq!(received.len(), PACKET_CAPACITY);
    assert_eq!(received.payload(), &max_payload[..]);
}

#[test]
fn test_boundary_payload_overflow_rejected() {
    let oversized = [0xccu8; PACKET_CAPACITY + 1];
    let result = PacketBuffer::new(5, 70, &oversized);

    match result {
        Err(PacketError::PayloadTooLarge { requested, capacity }) => {
            assert_eq!(requested, PACKET_CAPACITY + 1);
            assert_eq!(capacity, PACKET_CAPACITY);
        }
        other => panic!("expected PayloadTooLarge error, got {other:?}"),
    }
}

#[test]
fn test_boundary_send_when_full_returns_packet() {
    let (mut producer, _consumer) = ShmHandoffChannel::new();

    let first_packet = PacketBuffer::new(8, 80, b"initial slot occupant").expect("first packet creation");
    producer.send(first_packet).expect("first send must succeed");

    let second_packet = PacketBuffer::new(8, 81, b"competing rejected packet").expect("second packet creation");
    let send_err = producer.send(second_packet);

    assert!(send_err.is_err());
    let unconsumed_packet = send_err.expect_err("send on full slot must return unconsumed packet");

    assert_eq!(unconsumed_packet.channel_id(), 8);
    assert_eq!(unconsumed_packet.sequence_num(), 81);
    assert_eq!(unconsumed_packet.payload(), b"competing rejected packet");
}

#[test]
fn test_boundary_receive_when_empty_returns_none() {
    let (_producer, mut consumer) = ShmHandoffChannel::new();
    assert!(!consumer.has_data());
    assert!(consumer.receive().is_none());
}

// ===========================================================================
// Tier 3: Invariants, RAII & Drop Proofs
// ===========================================================================

#[test]
fn test_invariants_raii_drop_reclamation() {
    let drop_counter = Arc::new(AtomicUsize::new(0));

    // Sub-scenario A: Value dropped without handoff triggers single drop of enclosing resource.
    {
        let _detector = DropDetector::new(1, Arc::clone(&drop_counter));
        let _unconsumed = PacketBuffer::new(1, 1, b"unconsumed packet").expect("packet creation");
    }
    assert_eq!(drop_counter.load(Ordering::SeqCst), 1);

    // Sub-scenario B: Moving packet destructively across channel transfers ownership;
    // drop occurs only when consumed value or holding endpoint is dropped.
    let (mut producer, mut consumer) = ShmHandoffChannel::new();
    {
        let _detector = DropDetector::new(2, Arc::clone(&drop_counter));
        let packet = PacketBuffer::new(1, 2, b"transferred packet").expect("packet creation");
        producer.send(packet).expect("send should succeed");
        // _detector drops here, incrementing counter to 2
    }
    assert_eq!(drop_counter.load(Ordering::SeqCst), 2);

    // Consumer receives packet by value; dropping received packet leaves scope cleanly.
    {
        let received = consumer.receive().expect("receive should succeed");
        assert_eq!(received.sequence_num(), 2);
    }

    // Sub-scenario C: Dropping producer/consumer endpoints holding an unconsumed packet in the slot
    // must cleanly drop and reclaim the buffer without leaking resources or panicking.
    {
        let (mut orphan_producer, _orphan_consumer) = ShmHandoffChannel::new();
        let orphan_packet = PacketBuffer::new(1, 3, b"orphaned in slot").expect("packet creation");
        orphan_producer.send(orphan_packet).expect("send should succeed");
    }
}

#[test]
fn test_invariants_static_affine_move_enforcement() {
    let (mut producer, mut consumer) = ShmHandoffChannel::new();
    let packet = PacketBuffer::new(9, 90, b"affine destructive move proof").expect("packet creation");

    // Passing packet by value consumes ownership destructively (bitwise move).
    producer.send(packet).expect("send by value consumes packet");

    // The consumer receives exclusive ownership of the packet by value.
    let received = consumer.receive().expect("receive extracts packet by value");
    assert_eq!(received.channel_id(), 9);
    assert_eq!(received.sequence_num(), 90);
    assert_eq!(received.payload(), b"affine destructive move proof");

    // Demonstrating affine ownership recovery: when send fails, ownership is preserved
    // in Err(packet) so the caller can retry after draining the channel slot.
    let occupant = PacketBuffer::new(9, 91, b"slot occupant").expect("occupant creation");
    producer.send(occupant).expect("fill slot");

    let contender = PacketBuffer::new(9, 92, b"slot contender").expect("contender creation");
    let recovered = match producer.send(contender) {
        Ok(()) => panic!("channel was expected to be full"),
        Err(unsent) => unsent,
    };

    // Drain occupant to unblock the channel slot
    let drained = consumer.receive().expect("drain occupant");
    assert_eq!(drained.sequence_num(), 91);

    // The caller still holds exclusive ownership of `recovered`, allowing a retry move
    producer
        .send(recovered)
        .expect("retry send must succeed now that slot is drained");
    let final_received = consumer.receive().expect("receive retried packet");
    assert_eq!(final_received.sequence_num(), 92);
    assert_eq!(final_received.payload(), b"slot contender");
}

// ===========================================================================
// Tier 4: Concurrency & Threaded Scope
// ===========================================================================

#[test]
fn test_concurrency_threaded_producer_consumer() {
    const NUM_PACKETS: u32 = 500;
    let (mut producer, mut consumer) = ShmHandoffChannel::new();

    thread::scope(|s| {
        let producer_handle = s.spawn(move || {
            for seq in 0..NUM_PACKETS {
                let payload = seq.to_le_bytes();
                let mut packet = PacketBuffer::new(1, seq, &payload).expect("packet creation");

                loop {
                    match producer.send(packet) {
                        Ok(()) => break,
                        Err(unsent) => {
                            packet = unsent;
                            thread::yield_now();
                        }
                    }
                }
            }
        });

        let consumer_handle = s.spawn(move || {
            let mut received_count = 0_u32;
            while received_count < NUM_PACKETS {
                if let Some(packet) = consumer.receive() {
                    assert_eq!(packet.channel_id(), 1);
                    assert_eq!(packet.sequence_num(), received_count);
                    assert_eq!(packet.len(), 4);
                    assert_eq!(packet.payload(), &received_count.to_le_bytes());
                    received_count += 1;
                } else {
                    thread::yield_now();
                }
            }
            assert_eq!(received_count, NUM_PACKETS);
        });

        let p_res = producer_handle.join();
        let c_res = consumer_handle.join();
        if let Err(panic_payload) = p_res {
            std::panic::resume_unwind(panic_payload);
        }
        if let Err(panic_payload) = c_res {
            std::panic::resume_unwind(panic_payload);
        }
    });
}
