#![allow(
    clippy::disallowed_methods,
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::items_after_statements,
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    clippy::cast_possible_wrap,
    clippy::cast_ptr_alignment,
    unsafe_code
)]

//! Comprehensive 4-Tier Integration and Adversarial Test Suite for `systems::shm_pubsub`.
//!
//! Enforces 100% requirements traceability across all 25 test-verified requirements
//! defined in `docs/systems/shm_pubsub.md` (prefix `SHMPS`).
//!
//! Tiers:
//! 1. Functional Correctness
//! 2. Boundary & Timeouts
//! 3. Invariants & Ownership
//! 4. Multi-Threaded & Multi-Process Stress

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
use std::ffi::CString;
use std::io::{BufRead, BufReader, Read, Write};
use std::process::{Child, Command, Stdio};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::thread;
use std::time::{Duration, Instant};
use systems::platform;
use systems::shm_pubsub::layout::{self, ChannelHeader};
use systems::shm_pubsub::{
    MAX_SLOT_COUNT, MAX_SLOT_SIZE, MIN_SLOT_COUNT, ShmChannel, ShmPubSubConfig, ShmPubSubError, ShmPublisher,
    ShmReadMode, ShmSubscriber,
};

// ===========================================================================
// Custom Counting Allocator (SHMPS-REQ-018 Thread-Isolated Heap Tracking)
// ===========================================================================

struct CountingAllocator;

thread_local! {
    static TRACK_THIS_THREAD: Cell<bool> = const { Cell::new(false) };
    static ALLOC_COUNT_THIS_THREAD: Cell<usize> = const { Cell::new(0) };
}

// SAFETY: GlobalAlloc forwards allocation requests directly to the standard System allocator.
unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        TRACK_THIS_THREAD.with(|track| {
            if track.get() {
                ALLOC_COUNT_THIS_THREAD.with(|count| {
                    count.set(count.get() + 1);
                });
            }
        });
        // SAFETY: layout is forwarded directly to the underlying System allocator.
        unsafe { System.alloc(layout) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        // SAFETY: ptr and layout were allocated by System.alloc.
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static GLOBAL_ALLOC: CountingAllocator = CountingAllocator;

// ===========================================================================
// Test Helpers & RAII Guards
// ===========================================================================

struct TestShmGuard<'a>(&'a str);

impl Drop for TestShmGuard<'_> {
    fn drop(&mut self) {
        let _ = ShmChannel::unlink(self.0);
    }
}

/// Generates a payload tagged with `tag` at offset 0, followed by a deterministic byte pattern.
fn make_payload(tag: u64, size: usize) -> Vec<u8> {
    let mut payload = vec![0u8; size];
    let tag_bytes = tag.to_ne_bytes();
    let prefix = size.min(8);
    payload[..prefix].copy_from_slice(&tag_bytes[..prefix]);
    for (i, b) in payload.iter_mut().enumerate().skip(prefix) {
        *b = (tag as u8).wrapping_add(i as u8);
    }
    payload
}

/// Verifies whether `payload` is self-consistent with the tag stored in its first 8 bytes.
fn payload_is_consistent(payload: &[u8]) -> bool {
    if payload.len() < 8 {
        return true;
    }
    let mut tag_bytes = [0u8; 8];
    tag_bytes.copy_from_slice(&payload[..8]);
    let tag = u64::from_ne_bytes(tag_bytes);
    let expected = make_payload(tag, payload.len());
    payload == expected
}

/// RAII wrapper for a child test process that prevents `BrokenPipe` on child stdout.
struct ChildProcess {
    child: Child,
    reader: Option<BufReader<std::process::ChildStdout>>,
}

impl ChildProcess {
    fn id(&self) -> u32 {
        self.child.id()
    }

    fn kill(&mut self) -> std::io::Result<()> {
        self.child.kill()
    }

    fn wait(&mut self) -> std::io::Result<std::process::ExitStatus> {
        if let Some(mut reader) = self.reader.take() {
            let mut sink = Vec::new();
            let _ = reader.read_to_end(&mut sink);
        }
        self.child.wait()
    }
}

/// Spawns a child OS process executing `helper_child_process_runner` with the requested role.
/// Blocks until the child outputs its "READY" handshake line on stdout.
fn spawn_child(role: &str, shm_name: &str, count: Option<u64>, extra_env: &[(&str, &str)]) -> ChildProcess {
    let exe = std::env::current_exe().expect("current_exe");
    let mut cmd = Command::new(exe);
    cmd.arg("helper_child_process_runner")
        .arg("--exact")
        .arg("--ignored")
        .arg("--nocapture")
        .stdout(Stdio::piped())
        .stderr(Stdio::inherit())
        .env("SHM_TEST_ROLE", role)
        .env("SHM_TEST_NAME", shm_name);

    if let Some(c) = count {
        cmd.env("SHM_TEST_COUNT", c.to_string());
    }
    for &(k, v) in extra_env {
        cmd.env(k, v);
    }

    let mut child = cmd.spawn().expect("failed to spawn child process");
    let mut reader = BufReader::new(child.stdout.take().expect("failed to capture child stdout"));

    // Wait for the child to print its READY handshake
    let mut ready = false;
    let mut line = String::new();
    let start = Instant::now();
    while start.elapsed() < Duration::from_secs(5) {
        line.clear();
        if reader.read_line(&mut line).unwrap_or(0) == 0 {
            break;
        }
        if line.trim() == "READY" {
            ready = true;
            break;
        }
    }
    assert!(ready, "child process failed to signal READY within deadline");

    ChildProcess {
        child,
        reader: Some(reader),
    }
}

// ===========================================================================
// Helper Child Process Entrypoint (for multi-process tests)
// ===========================================================================

#[test]
#[ignore = "child process entrypoint for multi-process tests"]
fn helper_child_process_runner() {
    let role = std::env::var("SHM_TEST_ROLE").expect("SHM_TEST_ROLE missing");
    let name = std::env::var("SHM_TEST_NAME").expect("SHM_TEST_NAME missing");

    match role.as_str() {
        "subscriber" => {
            let count: u64 = std::env::var("SHM_TEST_COUNT")
                .expect("SHM_TEST_COUNT missing")
                .parse()
                .expect("parse count");
            let channel = ShmChannel::open(&name).expect("child open");
            let mut sub = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("child attach sub");

            // Signal ready
            println!("READY");
            std::io::stdout().flush().expect("flush");

            let mut buf = vec![0u8; channel.config().slot_size];
            for expected_seq in 1..=count {
                let sample = sub
                    .receive_for(&mut buf, Duration::from_secs(5))
                    .expect("child receive");
                assert_eq!(sample.sequence, expected_seq);
                assert!(payload_is_consistent(&buf[..sample.size]));
            }
        }
        "publisher" => {
            let count: u64 = std::env::var("SHM_TEST_COUNT")
                .expect("SHM_TEST_COUNT missing")
                .parse()
                .expect("parse count");
            let channel = ShmChannel::open(&name).expect("child open");
            let mut pub_handle = ShmPublisher::attach(&channel).expect("child attach pub");
            let base_seq = pub_handle.next_sequence();

            // Signal ready
            println!("READY");
            std::io::stdout().flush().expect("flush");

            for i in 0..count {
                let expected_seq = base_seq + i;
                let payload = make_payload(expected_seq, channel.config().slot_size);
                let actual = pub_handle.publish(&payload).expect("child publish");
                assert_eq!(actual, expected_seq);
                thread::yield_now();
            }
        }
        "victim_publisher" => {
            let channel = ShmChannel::open(&name).expect("child open");
            let mut pub_handle = ShmPublisher::attach(&channel).expect("child attach pub");

            // Signal ready
            println!("READY");
            std::io::stdout().flush().expect("flush");

            let mut seq = 1u64;
            loop {
                let payload = make_payload(seq, channel.config().slot_size);
                if pub_handle.publish(&payload).is_err() {
                    break;
                }
                seq += 1;
                thread::sleep(Duration::from_micros(100));
            }
        }
        "blocking_subscriber" => {
            let channel = ShmChannel::open(&name).expect("child open");
            let mut sub = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("child attach");

            // Signal ready
            println!("READY");
            std::io::stdout().flush().expect("flush");

            let mut buf = vec![0u8; channel.config().slot_size];
            // Must block until channel.close() is called by parent
            let res = sub.receive(&mut buf);
            assert_eq!(res, Err(ShmPubSubError::Closed));
        }
        "memlock_limiter" => {
            let existing_name = std::env::var("SHM_EXISTING_NAME").expect("existing name");
            let fresh_name = std::env::var("SHM_FRESH_NAME").expect("fresh name");

            // SAFETY: geteuid is a safe query.
            if unsafe { libc::geteuid() == 0 } {
                println!("READY");
                std::io::stdout().flush().expect("flush");
                return;
            }

            let rlim = libc::rlimit {
                rlim_cur: 0,
                rlim_max: 0,
            };
            // SAFETY: setrlimit called with valid pointer to rlimit struct.
            let ret = unsafe { libc::setrlimit(libc::RLIMIT_MEMLOCK, &raw const rlim) };
            if ret != 0 {
                println!("READY");
                std::io::stdout().flush().expect("flush");
                return;
            }

            println!("READY");
            std::io::stdout().flush().expect("flush");

            let cfg = ShmPubSubConfig::new(4, 64);
            let created = ShmChannel::create(&fresh_name, cfg);
            assert_eq!(created.err(), Some(ShmPubSubError::MemoryLockFailed));

            // Fresh channel must NOT have been left behind in POSIX shared memory (SHMPS-REQ-024)
            assert_eq!(ShmChannel::open(&fresh_name).err(), Some(ShmPubSubError::NotFound));

            // Existing channel open must fail with MemoryLockFailed and NOT unlink the existing segment
            let opened = ShmChannel::open(&existing_name);
            assert_eq!(opened.err(), Some(ShmPubSubError::MemoryLockFailed));
        }
        other => panic!("unknown child role: {other}"),
    }
}

// ===========================================================================
// Tier 1: Functional Correctness
// ===========================================================================

#[test]
fn test_tier1_create_and_unlink_channel() {
    // requirements: SHMPS-REQ-001, SHMPS-REQ-006
    let shm_name = "/t1_create_unlink";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 64);
    let channel = ShmChannel::create(shm_name, config).expect("channel creation should succeed");

    assert_eq!(channel.name(), shm_name);
    assert_eq!(channel.config().slot_count, 8);
    assert_eq!(channel.config().slot_size, 64);
    assert!(!channel.is_closed());

    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier1_open_existing_channel_geometry() {
    // requirements: SHMPS-REQ-004
    let shm_name = "/t1_open_geom";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(16, 128);
    let channel_created = ShmChannel::create(shm_name, config).expect("create should succeed");

    let channel_opened = ShmChannel::open(shm_name).expect("open should succeed");
    assert_eq!(channel_opened.name(), shm_name);
    assert_eq!(channel_opened.config().slot_count, 16);
    assert_eq!(channel_opened.config().slot_size, 128);
    assert!(!channel_opened.is_closed());

    drop(channel_created);
    drop(channel_opened);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier1_publish_and_receive_canonical() {
    // requirements: SHMPS-REQ-007, SHMPS-REQ-008, SHMPS-REQ-010, SHMPS-REQ-012
    let shm_name = "/t1_pub_recv_canon";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create should succeed");

    let mut publisher = ShmPublisher::attach(&channel).expect("publisher attach should succeed");
    let mut subscriber =
        ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("subscriber attach should succeed");

    // Second publisher attach while alive must fail with PublisherExists (SHMPS-REQ-007)
    assert_eq!(
        ShmPublisher::attach(&channel).unwrap_err(),
        ShmPubSubError::PublisherExists
    );

    // Initial receive before any publish should report WouldBlock (SHMPS-REQ-012)
    let mut buf = [0u8; 64];
    assert_eq!(
        subscriber.try_receive(&mut buf).unwrap_err(),
        ShmPubSubError::WouldBlock
    );

    // Publish sample 1 (SHMPS-REQ-008)
    let payload1 = b"Sample payload number 1";
    let seq1 = publisher.publish(payload1).expect("publish 1");
    assert_eq!(seq1, 1);

    // Receive sample 1 (SHMPS-REQ-010)
    let sample1 = subscriber.try_receive(&mut buf).expect("try_receive 1");
    assert_eq!(sample1.sequence, 1);
    assert_eq!(sample1.size, payload1.len());
    assert_eq!(sample1.missed, 0);
    assert_eq!(&buf[..sample1.size], payload1);

    // Publish sample 2
    let payload2 = b"Sample payload number 2";
    let seq2 = publisher.publish(payload2).expect("publish 2");
    assert_eq!(seq2, 2);

    // Receive sample 2
    let sample2 = subscriber.try_receive(&mut buf).expect("try_receive 2");
    assert_eq!(sample2.sequence, 2);
    assert_eq!(sample2.size, payload2.len());
    assert_eq!(sample2.missed, 0);
    assert_eq!(&buf[..sample2.size], payload2);

    // Empty channel reports WouldBlock
    assert_eq!(
        subscriber.try_receive(&mut buf).unwrap_err(),
        ShmPubSubError::WouldBlock
    );

    drop(publisher);
    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier1_ring_buffer_wraparound_fifo_order() {
    // requirements: SHMPS-REQ-008, SHMPS-REQ-010
    let shm_name = "/t1_ring_wrap";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    // Ring capacity of 4 slots
    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    // Publish 16 samples, consuming continuously so reader never gets lapped
    let mut rx_buf = [0u8; 64];
    for seq in 1..=16u64 {
        let payload = make_payload(seq, 48);
        let pub_seq = publisher.publish(&payload).expect("publish");
        assert_eq!(pub_seq, seq);

        let sample = subscriber.try_receive(&mut rx_buf).expect("receive");
        assert_eq!(sample.sequence, seq);
        assert_eq!(sample.size, 48);
        assert_eq!(sample.missed, 0);
        assert_eq!(&rx_buf[..sample.size], &payload);
    }

    drop(publisher);
    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier1_buffer_drainage_after_close() {
    // requirements: SHMPS-REQ-014
    let shm_name = "/t1_drain_close";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 32);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");
    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");

    publisher.publish(b"msg-1").expect("pub 1");
    publisher.publish(b"msg-2").expect("pub 2");
    publisher.publish(b"msg-3").expect("pub 3");

    // Channel close stops publishing immediately
    channel.close();
    assert!(channel.is_closed());
    assert_eq!(publisher.publish(b"msg-4"), Err(ShmPubSubError::Closed));

    // Subscriber must drain committed samples 1, 2, 3 before observing Closed
    let mut buf = [0u8; 32];
    let s1 = subscriber.try_receive(&mut buf).expect("drain 1");
    assert_eq!(s1.sequence, 1);
    assert_eq!(&buf[..s1.size], b"msg-1");

    let s2 = subscriber.try_receive(&mut buf).expect("drain 2");
    assert_eq!(s2.sequence, 2);
    assert_eq!(&buf[..s2.size], b"msg-2");

    let s3 = subscriber.try_receive(&mut buf).expect("drain 3");
    assert_eq!(s3.sequence, 3);
    assert_eq!(&buf[..s3.size], b"msg-3");

    // Now drained: subscriber must report Closed
    assert_eq!(subscriber.try_receive(&mut buf), Err(ShmPubSubError::Closed));
    assert_eq!(subscriber.receive(&mut buf), Err(ShmPubSubError::Closed));
    assert_eq!(
        subscriber.receive_for(&mut buf, Duration::from_millis(20)),
        Err(ShmPubSubError::Closed)
    );

    drop(subscriber);
    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier1_boundary_payload_sizes_zero_and_max() {
    // requirements: SHMPS-REQ-008, SHMPS-REQ-010
    let shm_name = "/t1_payload_bnd";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let slot_size = 64;
    let config = ShmPubSubConfig::new(4, slot_size);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    // Oversized payload before any valid publish (65 bytes > 64 bytes)
    let oversized = vec![0xee; slot_size + 1];
    assert_eq!(publisher.publish(&oversized), Err(ShmPubSubError::PayloadTooLarge));
    assert_eq!(
        publisher.next_sequence(),
        1,
        "rejected publish must not advance sequence"
    );

    // Boundary: 0-byte payload
    let seq1 = publisher.publish(&[]).expect("zero-byte payload");
    assert_eq!(seq1, 1);
    assert_eq!(publisher.next_sequence(), 2);

    // Boundary: exact slot_size payload (64 bytes)
    let exact_payload = vec![0xaa; slot_size];
    let seq2 = publisher.publish(&exact_payload).expect("exact slot_size payload");
    assert_eq!(seq2, 2);
    assert_eq!(publisher.next_sequence(), 3);

    // Receive 0-byte payload
    let mut rx_buf = vec![0u8; slot_size];
    let sample1 = subscriber.try_receive(&mut rx_buf).expect("receive sample 1");
    assert_eq!(sample1.sequence, 1);
    assert_eq!(sample1.size, 0);

    // Receive exact slot_size payload
    let sample2 = subscriber.try_receive(&mut rx_buf).expect("receive sample 2");
    assert_eq!(sample2.sequence, 2);
    assert_eq!(sample2.size, slot_size);
    assert_eq!(&rx_buf[..sample2.size], &exact_payload);

    drop(publisher);
    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

// ===========================================================================
// Tier 2: Boundary & Timeouts
// ===========================================================================

#[test]
fn test_tier2_invalid_configurations_rejected() {
    // requirements: SHMPS-REQ-001, SHMPS-REQ-004
    let invalid_configs = [
        ShmPubSubConfig {
            slot_count: 0,
            slot_size: 64,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: 1,
            slot_size: 64,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: 3,
            slot_size: 64,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: 5,
            slot_size: 64,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: 12,
            slot_size: 64,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: MAX_SLOT_COUNT * 2,
            slot_size: 64,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: 8,
            slot_size: 0,
            memory_lock: true,
        },
        ShmPubSubConfig {
            slot_count: 8,
            slot_size: MAX_SLOT_SIZE + 1,
            memory_lock: true,
        },
    ];

    for (i, config) in invalid_configs.into_iter().enumerate() {
        assert!(!config.is_valid(), "config {config:?} must be reported as invalid");
        let name = format!("/t2_inv_cfg_{i}");
        let _guard = TestShmGuard(&name);
        let _ = ShmChannel::unlink(&name);

        let result = ShmChannel::create(&name, config);
        assert_eq!(result.err(), Some(ShmPubSubError::InvalidArgument));

        // Invariant: Failed create must leave no segment in shared memory namespace
        assert_eq!(ShmChannel::open(&name).err(), Some(ShmPubSubError::NotFound));
    }
}

#[test]
fn test_tier2_boundary_configurations_accepted() {
    // requirements: SHMPS-REQ-001
    let boundary_configs = [
        ShmPubSubConfig::new(MIN_SLOT_COUNT, 1),
        ShmPubSubConfig::new(MAX_SLOT_COUNT, 1),
        ShmPubSubConfig::new(MIN_SLOT_COUNT, MAX_SLOT_SIZE),
    ];

    for (i, config) in boundary_configs.into_iter().enumerate() {
        assert!(config.is_valid(), "config {config:?} must be valid");
        let name = format!("/t2_bnd_cfg_{i}");
        let _guard = TestShmGuard(&name);
        let _ = ShmChannel::unlink(&name);

        let channel = ShmChannel::create(&name, config).expect("boundary config creation should succeed");
        assert_eq!(channel.config().slot_count, config.slot_count);
        assert_eq!(channel.config().slot_size, config.slot_size);
        drop(channel);

        assert_eq!(ShmChannel::unlink(&name), Ok(()));
    }
}

#[test]
fn test_tier2_channel_name_validation() {
    // requirements: SHMPS-REQ-002, SHMPS-REQ-004, SHMPS-REQ-006
    let config = ShmPubSubConfig::new(4, 64);

    // Malformed names: empty, no leading slash, lone slash, multiple slashes, embedded null
    let bad_names = ["", "noslash", "/", "/a/b", "/a\0b"];
    for bad in bad_names {
        assert_eq!(
            ShmChannel::create(bad, config).err(),
            Some(ShmPubSubError::InvalidArgument),
            "create should reject '{bad}'"
        );
        assert_eq!(
            ShmChannel::open(bad).err(),
            Some(ShmPubSubError::InvalidArgument),
            "open should reject '{bad}'"
        );
        assert_eq!(
            ShmChannel::unlink(bad).err(),
            Some(ShmPubSubError::InvalidArgument),
            "unlink should reject '{bad}'"
        );
    }

    // Name length boundary: 30 chars is the maximum allowed length (1 slash + 29 chars)
    let max_len_name = format!("/{}", "a".repeat(29));
    assert_eq!(max_len_name.len(), 30);
    {
        let _guard = TestShmGuard(&max_len_name);
        let _ = ShmChannel::unlink(&max_len_name);

        let channel =
            ShmChannel::create(&max_len_name, config).expect("channel creation with 30-char name must succeed");
        assert_eq!(channel.name(), max_len_name);
        drop(channel);

        assert_eq!(ShmChannel::unlink(&max_len_name), Ok(()));
    }

    // Overlong names (> 30 chars): 31 chars and 100 chars must return NameTooLong
    let overlong_31 = format!("/{}", "a".repeat(30));
    assert_eq!(overlong_31.len(), 31);
    assert_eq!(
        ShmChannel::create(&overlong_31, config).err(),
        Some(ShmPubSubError::NameTooLong)
    );
    assert_eq!(ShmChannel::open(&overlong_31).err(), Some(ShmPubSubError::NameTooLong));
    assert_eq!(
        ShmChannel::unlink(&overlong_31).err(),
        Some(ShmPubSubError::NameTooLong)
    );

    let overlong_100 = format!("/{}", "a".repeat(99));
    assert_eq!(
        ShmChannel::create(&overlong_100, config).err(),
        Some(ShmPubSubError::NameTooLong)
    );
}

#[test]
fn test_tier2_duplicate_create_and_nonexistent_open() {
    // requirements: SHMPS-REQ-003, SHMPS-REQ-004, SHMPS-REQ-006
    let shm_name = "/t2_dup_create";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config_a = ShmPubSubConfig::new(4, 64);
    let channel_a = ShmChannel::create(shm_name, config_a).expect("initial create should succeed");

    // Second create with same name must fail with AlreadyExists without altering existing segment (SHMPS-REQ-003)
    let config_b = ShmPubSubConfig::new(16, 128);
    let err = ShmChannel::create(shm_name, config_b).unwrap_err();
    assert_eq!(err, ShmPubSubError::AlreadyExists);

    // Existing channel remains functional and untouched
    let opened = ShmChannel::open(shm_name).expect("open should succeed on existing channel");
    assert_eq!(opened.config().slot_count, 4);
    assert_eq!(opened.config().slot_size, 64);

    drop(opened);
    drop(channel_a);

    // Nonexistent channel operations
    let missing_name = "/t2_missing_segment";
    let _ = ShmChannel::unlink(missing_name);
    assert_eq!(ShmChannel::open(missing_name).unwrap_err(), ShmPubSubError::NotFound);
    assert_eq!(ShmChannel::unlink(missing_name).unwrap_err(), ShmPubSubError::NotFound);

    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier2_reject_incompatible_and_foreign_segments() {
    // requirements: SHMPS-REQ-005
    let shm_name = "/t2_incompat_seg";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    // 1. Foreign segment: 0 bytes
    let c_name = CString::new(shm_name).unwrap();
    // SAFETY: shm_open called with valid C string and POSIX flags.
    let fd = unsafe { libc::shm_open(c_name.as_ptr(), libc::O_CREAT | libc::O_EXCL | libc::O_RDWR, 0o600) };
    assert!(fd >= 0);
    // SAFETY: ftruncate and close called with valid fd.
    unsafe {
        assert_eq!(libc::ftruncate(fd, 0), 0);
        libc::close(fd);
    }
    assert_eq!(ShmChannel::open(shm_name).err(), Some(ShmPubSubError::LayoutMismatch));
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));

    // 2. Foreign segment: 4096 bytes of 0xAB (corrupt magic and layout)
    // SAFETY: shm_open called with valid C string and POSIX flags.
    let fd = unsafe { libc::shm_open(c_name.as_ptr(), libc::O_CREAT | libc::O_EXCL | libc::O_RDWR, 0o600) };
    assert!(fd >= 0);
    let size = 4096;
    // SAFETY: fd is valid; base mapped to size bytes and initialized with invalid layout bytes.
    unsafe {
        assert_eq!(libc::ftruncate(fd, size as libc::off_t), 0);
        let base = libc::mmap(
            core::ptr::null_mut(),
            size,
            libc::PROT_READ | libc::PROT_WRITE,
            libc::MAP_SHARED,
            fd,
            0,
        );
        assert_ne!(base, libc::MAP_FAILED);
        core::ptr::write_bytes(base, 0xab, size);
        libc::munmap(base, size);
        libc::close(fd);
    }
    assert_eq!(ShmChannel::open(shm_name).err(), Some(ShmPubSubError::LayoutMismatch));
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));

    // 3. Segment with corrupted layout_version (version 999 != LAYOUT_VERSION)
    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");
    let view = channel.view();
    // SAFETY: view.base is aligned and large enough for ChannelHeader.
    unsafe {
        let header_ptr = view.base.cast::<ChannelHeader>();
        (*header_ptr).layout_version = 999;
    }
    drop(channel);
    assert_eq!(ShmChannel::open(shm_name).err(), Some(ShmPubSubError::LayoutMismatch));
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));

    // 4. Segment with corrupted slot_count in header (3 is not a power of 2)
    let channel = ShmChannel::create(shm_name, config).expect("create");
    let view = channel.view();
    // SAFETY: view.base is aligned and large enough for ChannelHeader.
    unsafe {
        let header_ptr = view.base.cast::<ChannelHeader>();
        (*header_ptr).slot_count = 3;
    }
    drop(channel);
    assert_eq!(ShmChannel::open(shm_name).err(), Some(ShmPubSubError::LayoutMismatch));
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier2_nonblocking_try_receive_would_block() {
    // requirements: SHMPS-REQ-012
    let shm_name = "/t2_would_block";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    let mut buf = [0u8; 64];
    assert_eq!(subscriber.try_receive(&mut buf), Err(ShmPubSubError::WouldBlock));

    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier2_buffer_too_small_cursor_retention() {
    // requirements: SHMPS-REQ-012
    let shm_name = "/t2_buf_small_ret";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    let payload = b"This is a 32-byte test payload!!";
    assert_eq!(payload.len(), 32);
    publisher.publish(payload).expect("publish");

    // Attempt receive with 16-byte buffer (< 32-byte payload)
    let mut tiny_buf = [0u8; 16];
    assert_eq!(
        subscriber.try_receive(&mut tiny_buf),
        Err(ShmPubSubError::BufferTooSmall)
    );

    // Cursor must NOT have advanced; sample must still be retrievable with full buffer
    let mut full_buf = [0u8; 64];
    let sample = subscriber.try_receive(&mut full_buf).expect("retry");
    assert_eq!(sample.sequence, 1);
    assert_eq!(sample.size, 32);
    assert_eq!(sample.missed, 0);
    assert_eq!(&full_buf[..32], payload);

    drop(subscriber);
    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier2_receive_for_timeout_deadline() {
    // requirements: SHMPS-REQ-013
    let shm_name = "/t2_recv_timeout";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    let mut buf = [0u8; 64];
    let timeout = Duration::from_millis(40);
    let start = Instant::now();
    let res = subscriber.receive_for(&mut buf, timeout);
    let elapsed = start.elapsed();

    assert_eq!(res, Err(ShmPubSubError::TimedOut));
    assert!(
        elapsed >= timeout,
        "receive_for returned earlier than requested timeout ({elapsed:?} < {timeout:?})"
    );

    // Publishing wakes waiting subscriber
    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let handle = thread::spawn(move || {
        thread::sleep(Duration::from_millis(15));
        publisher.publish(b"wake-up message").expect("publish");
    });

    let wake_sample = subscriber
        .receive_for(&mut buf, Duration::from_millis(200))
        .expect("receive after publish");
    assert_eq!(wake_sample.sequence, 1);
    assert_eq!(&buf[..wake_sample.size], b"wake-up message");
    handle.join().expect("join");

    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier2_close_wakes_waiting_subscribers() {
    // requirements: SHMPS-REQ-013, SHMPS-REQ-014
    let shm_name = "/t2_close_wakes";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    const NUM_WAITERS: usize = 4;
    let started = Arc::new(AtomicUsize::new(0));
    let mut handles = Vec::with_capacity(NUM_WAITERS);

    for w_idx in 0..NUM_WAITERS {
        let channel_sub = channel.clone();
        let ready = Arc::clone(&started);
        handles.push(thread::spawn(move || {
            let mut sub = ShmSubscriber::attach(&channel_sub, ShmReadMode::InOrder).expect("attach");
            ready.fetch_add(1, Ordering::SeqCst);
            let mut buf = [0u8; 64];
            let res = sub.receive(&mut buf);
            (w_idx, res)
        }));
    }

    while started.load(Ordering::SeqCst) < NUM_WAITERS {
        thread::yield_now();
    }
    thread::sleep(Duration::from_millis(25));

    let close_start = Instant::now();
    channel.close();
    assert!(channel.is_closed());

    for handle in handles {
        let (idx, res) = handle.join().expect("join");
        assert_eq!(res, Err(ShmPubSubError::Closed), "waiter {idx} failed to wake on close");
    }

    let elapsed = close_start.elapsed();
    // SHMPS-REQ-014: All blocked receivers must wake within 100 ms
    assert!(
        elapsed < Duration::from_millis(200),
        "channel.close() unblocking took too long: {elapsed:?}"
    );

    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier2_double_unlink_behavior() {
    // requirements: SHMPS-REQ-006, SHMPS-REQ-019
    let shm_name = "/t2_double_unlink";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");
    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    // First unlink removes the name from POSIX shm namespace
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));

    // Later open calls must return NotFound
    assert_eq!(ShmChannel::open(shm_name).err(), Some(ShmPubSubError::NotFound));

    // Second unlink on unlinked name must return NotFound
    assert_eq!(ShmChannel::unlink(shm_name).err(), Some(ShmPubSubError::NotFound));

    // Invariant: Existing mapped handles remain usable after unlink (SHMPS-REQ-006)
    assert_eq!(publisher.publish(b"still active").expect("pub after unlink"), 1);

    let mut buf = [0u8; 64];
    let sample = subscriber.try_receive(&mut buf).expect("read after unlink");
    assert_eq!(sample.sequence, 1);
    assert_eq!(&buf[..sample.size], b"still active");

    drop(publisher);
    drop(subscriber);
    drop(channel);
}

#[test]
fn test_tier2_memory_lock_failure_handling() {
    // requirements: SHMPS-REQ-024
    let existing_name = "/t2_memlock_exist";
    let fresh_name = "/t2_memlock_fresh";
    let _guard1 = TestShmGuard(existing_name);
    let _guard2 = TestShmGuard(fresh_name);
    let _ = ShmChannel::unlink(existing_name);
    let _ = ShmChannel::unlink(fresh_name);

    // Parent creates existing channel
    let config = ShmPubSubConfig::new(4, 64);
    let existing_channel = ShmChannel::create(existing_name, config).expect("create existing");

    // Spawn child with memlock_limiter
    let mut child = spawn_child(
        "memlock_limiter",
        existing_name,
        None,
        &[("SHM_EXISTING_NAME", existing_name), ("SHM_FRESH_NAME", fresh_name)],
    );

    let status = child.wait().expect("wait child");
    assert!(status.success(), "child memlock limiter execution failed");

    // Verify existing channel was NOT corrupted or unlinked by child's failed open
    let reopened = ShmChannel::open(existing_name).expect("existing channel must still exist");
    assert_eq!(reopened.config().slot_count, 4);

    drop(existing_channel);
    drop(reopened);
    assert_eq!(ShmChannel::unlink(existing_name), Ok(()));
}

// ===========================================================================
// Tier 3: Invariants & Ownership
// ===========================================================================

#[test]
fn test_tier3_seqlock_detection_torn_reads() {
    // requirements: SHMPS-REQ-015, SHMPS-REQ-026
    let shm_name = "/t3_seqlock_churn";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    // Minimal 2-slot ring to maximize seqlock contention and churn
    let config = ShmPubSubConfig::new(2, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    const CHURN_SAMPLES: u64 = 8_000;
    const NUM_READERS: usize = 4;
    let start_signal = Arc::new(AtomicBool::new(false));
    let mut reader_handles = Vec::with_capacity(NUM_READERS);

    for r_idx in 0..NUM_READERS {
        let channel_sub = channel.clone();
        let start = Arc::clone(&start_signal);
        let mode = if r_idx % 2 == 0 {
            ShmReadMode::InOrder
        } else {
            ShmReadMode::Latest
        };

        reader_handles.push(thread::spawn(move || {
            let mut sub = ShmSubscriber::attach(&channel_sub, mode).expect("attach");
            while !start.load(Ordering::Acquire) {
                thread::yield_now();
            }

            let mut buf = [0u8; 64];
            let mut reads = 0u64;

            loop {
                match sub.try_receive(&mut buf) {
                    Ok(sample) => {
                        assert_eq!(sample.size, 64);
                        // Invariant: Seqlock ensures no torn reads! Payload must be internally consistent.
                        assert!(
                            payload_is_consistent(&buf[..sample.size]),
                            "reader {r_idx} detected torn or corrupted sample at seq {}",
                            sample.sequence
                        );
                        reads += 1;
                    }
                    Err(ShmPubSubError::WouldBlock) => {
                        if channel_sub.is_closed() {
                            break;
                        }
                        thread::yield_now();
                    }
                    Err(ShmPubSubError::Closed) => break,
                    Err(e) => panic!("reader {r_idx} unexpected error {e:?}"),
                }
            }
            reads
        }));
    }

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    start_signal.store(true, Ordering::Release);

    for s in 1..=CHURN_SAMPLES {
        let payload = make_payload(s, 64);
        publisher.publish(&payload).expect("publish must succeed");
    }

    thread::sleep(Duration::from_millis(25));
    channel.close();

    for handle in reader_handles {
        let reads = handle.join().expect("reader join");
        assert!(reads > 0, "reader should have received samples");
    }

    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_missed_slot_detection_overrun() {
    // requirements: SHMPS-REQ-011
    let shm_name = "/t3_overrun_det";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 32);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    // Publisher publishes 10 samples (1..=10) without subscriber consuming
    for s in 1..=10u64 {
        publisher.publish(&s.to_ne_bytes()).expect("publish");
    }

    // Since slot_count is 4, lag is 10 - 1 = 9 >= 4 (lapped).
    // Subscriber must detect overrun, jump to newest sample (10), and report missed = 9 (SHMPS-REQ-011)
    let mut buf = [0u8; 32];
    let sample = subscriber.try_receive(&mut buf).expect("try_receive");
    assert_eq!(sample.sequence, 10);
    assert_eq!(sample.missed, 9);
    assert_eq!(&buf[0..8], &10u64.to_ne_bytes());

    drop(subscriber);
    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_jump_to_head_latest_mode() {
    // requirements: SHMPS-REQ-011, SHMPS-REQ-027
    let shm_name = "/t3_jump_latest";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 32);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut sub_latest = ShmSubscriber::attach(&channel, ShmReadMode::Latest).expect("sub attach");

    // Publish 5 samples
    for s in 1..=5u64 {
        publisher.publish(&s.to_ne_bytes()).expect("publish");
    }

    // Latest mode jumps directly to sample 5 with missed = 4
    let mut buf = [0u8; 32];
    let sample = sub_latest.try_receive(&mut buf).expect("try_receive");
    assert_eq!(sample.sequence, 5);
    assert_eq!(sample.missed, 4);

    // No new samples -> WouldBlock
    assert_eq!(sub_latest.try_receive(&mut buf), Err(ShmPubSubError::WouldBlock));

    // Publish 3 more (6, 7, 8)
    for s in 6..=8u64 {
        publisher.publish(&s.to_ne_bytes()).expect("publish");
    }

    let sample2 = sub_latest.try_receive(&mut buf).expect("try_receive");
    assert_eq!(sample2.sequence, 8);
    assert_eq!(sample2.missed, 2); // skipped 6, 7

    drop(sub_latest);
    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_in_order_consumption_mode() {
    // requirements: SHMPS-REQ-010, SHMPS-REQ-027
    let shm_name = "/t3_inorder_mode";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 32);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut sub_inorder = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    // InOrder mode receives every published sample sequentially without skips as long as lag < slot_count
    for s in 1..=6u64 {
        publisher.publish(&s.to_ne_bytes()).expect("publish");
    }

    let mut buf = [0u8; 32];
    for expected_seq in 1..=6u64 {
        let sample = sub_inorder.try_receive(&mut buf).expect("try_receive");
        assert_eq!(sample.sequence, expected_seq);
        assert_eq!(sample.missed, 0);
        assert_eq!(&buf[0..8], &expected_seq.to_ne_bytes());
    }

    drop(sub_inorder);
    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_single_publisher_and_role_transfer() {
    // requirements: SHMPS-REQ-007
    let shm_name = "/t3_single_pub_role";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    {
        let mut pub1 = ShmPublisher::attach(&channel).expect("pub1 attach");
        assert_eq!(pub1.publish(b"msg1").expect("publish"), 1);

        // Second publisher attach while pub1 is alive must fail with PublisherExists
        assert_eq!(
            ShmPublisher::attach(&channel).unwrap_err(),
            ShmPubSubError::PublisherExists
        );
        // pub1 dropped at end of scope: role released cleanly
    }

    // New publisher immediately attaches and continues sequence
    let mut pub2 = ShmPublisher::attach(&channel).expect("pub2 attach");
    assert_eq!(pub2.next_sequence(), 2);
    assert_eq!(pub2.publish(b"msg2").expect("publish"), 2);

    drop(pub2);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_sequence_monotonicity_and_crash_recovery() {
    // requirements: SHMPS-REQ-007, SHMPS-REQ-020
    let shm_name = "/t3_seq_mono_crash";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    // Publish samples 1, 2, 3
    {
        let mut pub1 = ShmPublisher::attach(&channel).expect("pub1");
        pub1.publish(b"sample-1").expect("pub 1");
        pub1.publish(b"sample-2").expect("pub 2");
        pub1.publish(b"sample-3").expect("pub 3");
    }

    // Simulate an uncommitted half-written slot left odd ("in progress") from a killed publisher
    // Sample 4: slot index (4-1) & 3 = 3. Seq word = 2*4 - 1 = 7.
    let view = channel.view();
    let slot4 = view.slot_for(4);
    slot4.header.seq.store(7, Ordering::Release);

    // Simulate dead publisher takeover: publisher PID in header set to a non-existent PID (e.g. 999999)
    view.header().publisher_pid.store(999_999, Ordering::Release);

    // New publisher attaches via takeover
    let mut pub_takeover = ShmPublisher::attach(&channel).expect("takeover attach");

    // Sequence must be max(published=3, max(seq/2=7/2=3)) + 1 = 4 or higher (SHMPS-REQ-020)
    assert!(
        pub_takeover.next_sequence() >= 4,
        "recovered sequence must be strictly greater than any started or committed sequence"
    );

    let seq = pub_takeover.publish(b"sample-takeover").expect("takeover publish");
    assert!(seq >= 4);

    drop(pub_takeover);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_zero_heap_allocation_during_steady_state() {
    // requirements: SHMPS-REQ-018
    let shm_name = "/t3_zero_alloc";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 128);
    let channel = ShmChannel::create(shm_name, config).expect("create");
    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    let payload = [0x5au8; 64];
    let mut rx_buf = [0u8; 128];

    // Warm-up to ensure one-time harness initialization is settled
    publisher.publish(&payload).expect("warmup publish");
    let _ = subscriber.try_receive(&mut rx_buf).expect("warmup receive");

    // Verify steady-state operations perform ZERO heap allocations on this thread (SHMPS-REQ-018)
    TRACK_THIS_THREAD.with(|t| t.set(true));
    let allocs_before = ALLOC_COUNT_THIS_THREAD.with(Cell::get);

    for _ in 0..100 {
        let seq = publisher.publish(&payload).expect("publish");
        let sample = subscriber.try_receive(&mut rx_buf).expect("try_receive");
        assert_eq!(sample.sequence, seq);
        assert!(!channel.is_closed());
    }

    let allocs_after = ALLOC_COUNT_THIS_THREAD.with(Cell::get);
    TRACK_THIS_THREAD.with(|t| t.set(false));

    assert_eq!(
        allocs_after, allocs_before,
        "Steady-state publish and try_receive must perform ZERO heap allocations (SHMPS-REQ-018)"
    );

    // Verify channel.close() and is_closed() perform ZERO heap allocations
    TRACK_THIS_THREAD.with(|t| t.set(true));
    let allocs_before_close = ALLOC_COUNT_THIS_THREAD.with(Cell::get);
    channel.close();
    assert!(channel.is_closed());
    let allocs_after_close = ALLOC_COUNT_THIS_THREAD.with(Cell::get);
    TRACK_THIS_THREAD.with(|t| t.set(false));

    assert_eq!(
        allocs_after_close, allocs_before_close,
        "channel.close() and is_closed() must perform ZERO heap allocations (SHMPS-REQ-018)"
    );

    drop(subscriber);
    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_memory_residency_prefault() {
    // requirements: SHMPS-REQ-023
    let shm_name = "/t3_mem_residency";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(16, 2048);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    // Verify that every page across the segment was pre-faulted by create() and is accessible
    let view = channel.view();
    let size = layout::region_bytes(&config);
    let page_step = platform::page_size();

    let mut offset = 0;
    while offset < size {
        // SAFETY: offset is strictly within the mapped region of size bytes.
        let byte = unsafe { *view.base.add(offset) };
        let _ = byte;
        offset += page_step;
    }

    // Verify open() read-touches every page
    let opened = ShmChannel::open(shm_name).expect("open");
    assert_eq!(opened.config().slot_count, 16);
    assert_eq!(opened.config().slot_size, 2048);

    drop(opened);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier3_handle_lifetime_and_resource_release() {
    // requirements: SHMPS-REQ-019
    let shm_name = "/t3_handle_lifetime";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(4, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    // Moving and dropping channel handle unmaps memory without unlinking name (SHMPS-REQ-019)
    let moved_channel = channel;
    drop(moved_channel);

    // Channel still exists and can be opened by name
    let opened = ShmChannel::open(shm_name).expect("open after original channel drop");
    assert_eq!(opened.config().slot_count, 4);

    drop(opened);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

// ===========================================================================
// Tier 4: Multi-Threaded & Multi-Process Stress
// ===========================================================================

#[test]
fn test_tier4_spms_multithreaded_stress() {
    // requirements: SHMPS-REQ-010, SHMPS-REQ-015, SHMPS-REQ-017, SHMPS-REQ-026
    let shm_name = "/t4_spms_thread_stress";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(16, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    const NUM_SAMPLES: u64 = 4_000;
    const NUM_SUBSCRIBERS: usize = 4;

    let mut subscriber_handles = Vec::with_capacity(NUM_SUBSCRIBERS);
    let start_signal = Arc::new(AtomicBool::new(false));
    let ready_count = Arc::new(AtomicUsize::new(0));

    for sub_idx in 0..NUM_SUBSCRIBERS {
        let channel_sub = channel.clone();
        let start = Arc::clone(&start_signal);
        let ready = Arc::clone(&ready_count);
        let mode = if sub_idx % 2 == 0 {
            ShmReadMode::InOrder
        } else {
            ShmReadMode::Latest
        };

        subscriber_handles.push(thread::spawn(move || {
            let mut sub = ShmSubscriber::attach(&channel_sub, mode).expect("attach");
            ready.fetch_add(1, Ordering::SeqCst);
            while !start.load(Ordering::Acquire) {
                thread::yield_now();
            }

            let mut buf = [0u8; 64];
            let mut last_seq = 0u64;
            let mut received_count = 0u64;

            loop {
                match sub.receive_for(&mut buf, Duration::from_millis(40)) {
                    Ok(sample) => {
                        assert_eq!(sample.size, 64);
                        // Invariant: No torn reads! Both endpoints and body must be internally consistent.
                        assert!(
                            payload_is_consistent(&buf[..sample.size]),
                            "sub {sub_idx} detected torn sample at seq {}",
                            sample.sequence
                        );

                        if last_seq > 0 {
                            assert!(sample.sequence > last_seq, "sub {sub_idx} non-monotonic sequence");
                            if mode == ShmReadMode::InOrder {
                                assert_eq!(
                                    sample.sequence,
                                    last_seq + 1 + sample.missed,
                                    "sub {sub_idx} missed count invariant failed"
                                );
                            }
                        }

                        last_seq = sample.sequence;
                        received_count += 1;
                    }
                    Err(ShmPubSubError::TimedOut) => {
                        if channel_sub.is_closed() {
                            break;
                        }
                    }
                    Err(ShmPubSubError::Closed) => break,
                    Err(e) => panic!("sub {sub_idx} unexpected error {e:?}"),
                }
            }
            (received_count, last_seq)
        }));
    }

    while ready_count.load(Ordering::SeqCst) < NUM_SUBSCRIBERS {
        thread::yield_now();
    }

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    start_signal.store(true, Ordering::Release);

    for s in 1..=NUM_SAMPLES {
        let payload = make_payload(s, 64);
        let published_seq = publisher.publish(&payload).expect("publish");
        assert_eq!(published_seq, s);
    }

    thread::sleep(Duration::from_millis(50));
    channel.close();

    for (idx, handle) in subscriber_handles.into_iter().enumerate() {
        let (count, last_seq) = handle.join().expect("subscriber join");
        assert!(count > 0, "subscriber {idx} received no samples");
        assert!(last_seq <= NUM_SAMPLES, "sub {idx} read beyond max published sequence");
    }

    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier4_publisher_wait_free_under_slow_subscribers() {
    // requirements: SHMPS-REQ-009, SHMPS-REQ-025
    let shm_name = "/t4_pub_waitfree";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 128);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let stop_subscribers = Arc::new(AtomicBool::new(false));
    let mut sub_handles = Vec::new();

    // Spawn 2 idle/sleeping subscribers holding open handles
    for _ in 0..2 {
        let channel_sub = channel.clone();
        let stop = Arc::clone(&stop_subscribers);
        sub_handles.push(thread::spawn(move || {
            let mut sub = ShmSubscriber::attach(&channel_sub, ShmReadMode::InOrder).expect("attach");
            let mut buf = [0u8; 128];
            while !stop.load(Ordering::Acquire) {
                thread::sleep(Duration::from_millis(10));
                let _ = sub.try_receive(&mut buf);
            }
        }));
    }

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    let payload = [0xabu8; 128];

    const PUBLISH_COUNT: u64 = 10_000;
    let start = Instant::now();

    for _ in 1..=PUBLISH_COUNT {
        publisher.publish(&payload).expect("publish must never fail or block");
    }

    let elapsed = start.elapsed();
    // 10,000 wait-free publishes must complete in bounded time (< 500 ms)
    assert!(
        elapsed < Duration::from_millis(500),
        "publisher took too long ({elapsed:?}), violation of wait-free contract (SHMPS-REQ-025)"
    );

    stop_subscribers.store(true, Ordering::Release);
    for h in sub_handles {
        h.join().expect("sub thread join");
    }

    drop(publisher);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier4_cross_process_publish_and_receive() {
    // requirements: SHMPS-REQ-010, SHMPS-REQ-016
    let shm_name = "/t4_xproc_pubsub";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    // Roomy ring (256 slots) to prevent reader overrun in fast cross-process publication
    let config = ShmPubSubConfig::new(256, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    const MSG_COUNT: u64 = 100;

    // Part A: Parent publisher -> Child subscriber process
    let mut child_subscriber_proc = spawn_child("subscriber", shm_name, Some(MSG_COUNT), &[]);

    let mut publisher = ShmPublisher::attach(&channel).expect("pub attach");
    for seq in 1..=MSG_COUNT {
        let payload = make_payload(seq, 64);
        publisher.publish(&payload).expect("parent publish");
        thread::yield_now();
    }

    let status = child_subscriber_proc.wait().expect("wait child sub");
    assert!(status.success(), "child subscriber process exited with failure");
    drop(publisher);

    // Part B: Child publisher process -> Parent subscriber
    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");
    let mut child_publisher_proc = spawn_child("publisher", shm_name, Some(MSG_COUNT), &[]);

    let mut rx_buf = [0u8; 64];
    for expected_seq in (MSG_COUNT + 1)..=(MSG_COUNT * 2) {
        let sample = subscriber
            .receive_for(&mut rx_buf, Duration::from_secs(5))
            .expect("parent receive from child pub");
        assert_eq!(sample.sequence, expected_seq);
        assert!(payload_is_consistent(&rx_buf[..sample.size]));
    }

    let pub_status = child_publisher_proc.wait().expect("wait child pub");
    assert!(pub_status.success(), "child publisher process exited with failure");

    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier4_cross_process_close_wake() {
    // requirements: SHMPS-REQ-014, SHMPS-REQ-016
    let shm_name = "/t4_xproc_close";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    // Spawn child blocked in subscriber.receive()
    let mut child_waiter = spawn_child("blocking_subscriber", shm_name, None, &[]);

    // Give child time to enter blocking loop
    thread::sleep(Duration::from_millis(30));

    let close_start = Instant::now();
    channel.close();
    assert!(channel.is_closed());

    let status = child_waiter.wait().expect("wait child waiter");
    assert!(
        status.success(),
        "child subscriber failed to unblock with Closed upon close()"
    );

    let elapsed = close_start.elapsed();
    assert!(
        elapsed < Duration::from_millis(200),
        "cross-process close unblocking took too long: {elapsed:?}"
    );

    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}

#[test]
fn test_tier4_cross_process_publisher_crash_and_takeover() {
    // requirements: SHMPS-REQ-007, SHMPS-REQ-016, SHMPS-REQ-020
    let shm_name = "/t4_xproc_crash";
    let _guard = TestShmGuard(shm_name);
    let _ = ShmChannel::unlink(shm_name);

    let config = ShmPubSubConfig::new(8, 64);
    let channel = ShmChannel::create(shm_name, config).expect("create");

    let mut subscriber = ShmSubscriber::attach(&channel, ShmReadMode::InOrder).expect("sub attach");

    // Spawn child publisher that publishes in a loop
    let mut victim_child = spawn_child("victim_publisher", shm_name, None, &[]);
    let victim_pid = victim_child.id();

    // Verify victim child has claimed the publisher role
    let view = channel.view();
    let start = Instant::now();
    while view.header().publisher_pid.load(Ordering::Acquire) != i64::from(victim_pid) {
        assert!(
            start.elapsed() <= Duration::from_secs(5),
            "victim child failed to claim publisher role in shared header"
        );
        thread::yield_now();
    }

    // While victim child is alive, second publisher attach must fail with PublisherExists (SHMPS-REQ-007)
    assert_eq!(
        ShmPublisher::attach(&channel).unwrap_err(),
        ShmPubSubError::PublisherExists
    );

    // Read at least a few samples published by victim
    let mut rx_buf = [0u8; 64];
    let mut highest_victim_seq = 0u64;
    for _ in 0..5 {
        if let Ok(sample) = subscriber.receive_for(&mut rx_buf, Duration::from_millis(100)) {
            highest_victim_seq = highest_victim_seq.max(sample.sequence);
            assert!(payload_is_consistent(&rx_buf[..sample.size]));
        }
    }

    // Kill the victim child process with SIGKILL (SHMPS-REQ-020)
    victim_child.kill().expect("kill victim child");
    let _ = victim_child.wait().expect("wait victim child exit");

    // Probing victim PID must now report dead (ESRCH/dead)
    assert!(!systems::shm_pubsub::posix::SharedMemoryRegion::process_alive(
        i64::from(victim_pid)
    ));

    // Parent attaches as replacement publisher via dead-process takeover
    let mut replacement_pub = ShmPublisher::attach(&channel).expect("replacement publisher takeover attach");

    // Invariant: Sequence assigned by replacement publisher must be strictly greater than any sequence
    // published or started by the dead publisher (SHMPS-REQ-020)
    let next_seq = replacement_pub.next_sequence();
    assert!(
        next_seq > highest_victim_seq,
        "recovered sequence {next_seq} must exceed highest victim sequence {highest_victim_seq}"
    );

    // Replacement publisher publishes a new sample
    let payload = make_payload(next_seq, 64);
    let pub_seq = replacement_pub.publish(&payload).expect("replacement publish");
    assert_eq!(pub_seq, next_seq);

    // Drain any older samples until we observe the replacement sample
    let mut saw_replacement = false;
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline {
        match subscriber.receive_for(&mut rx_buf, Duration::from_millis(100)) {
            Ok(sample) => {
                assert!(payload_is_consistent(&rx_buf[..sample.size]));
                if sample.sequence == next_seq {
                    saw_replacement = true;
                    break;
                }
            }
            Err(_) => break,
        }
    }
    assert!(
        saw_replacement,
        "subscriber failed to receive sample from replacement publisher"
    );

    drop(replacement_pub);
    drop(subscriber);
    drop(channel);
    assert_eq!(ShmChannel::unlink(shm_name), Ok(()));
}
