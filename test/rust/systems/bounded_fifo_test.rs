#![allow(clippy::disallowed_methods, clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! 4-Tier integration test suite for `systems::BoundedFifo`.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};
use systems::bounded_fifo::{BoundedFifo, PopError, PopTimeoutError, PushError, PushTimeoutError};

// ===========================================================================
// Test Helpers
// ===========================================================================

#[derive(Debug)]
struct DropDetector {
    id: usize,
    counter: Arc<AtomicUsize>,
}

impl DropDetector {
    const fn new(id: usize, counter: Arc<AtomicUsize>) -> Self {
        Self { id, counter }
    }
}

impl Drop for DropDetector {
    fn drop(&mut self) {
        self.counter.fetch_add(1, Ordering::SeqCst);
    }
}

// ===========================================================================
// Tier 1: Canonical FIFO Behavior
// ===========================================================================

#[test]
fn test_fifo_initial_state() {
    // requirements: FIFO-REQ-001
    let fifo = BoundedFifo::<i32, 4>::new();
    assert_eq!(fifo.capacity(), 4);
    assert_eq!(fifo.len(), 0);
    assert!(fifo.is_empty());
    assert!(!fifo.is_full());
    assert!(!fifo.is_closed());
}

#[test]
fn test_fifo_canonical_ordering() {
    // requirements: FIFO-REQ-001, FIFO-REQ-003
    let fifo = BoundedFifo::<i32, 4>::new();
    assert_eq!(fifo.try_push(10), Ok(()));
    assert_eq!(fifo.try_push(20), Ok(()));
    assert_eq!(fifo.try_push(30), Ok(()));

    assert_eq!(fifo.len(), 3);
    assert_eq!(fifo.try_pop(), Ok(10));
    assert_eq!(fifo.try_pop(), Ok(20));
    assert_eq!(fifo.try_pop(), Ok(30));
    assert_eq!(fifo.try_pop(), Err(PopError::Empty));
    assert!(fifo.is_empty());
}

#[test]
fn test_fifo_blocking_push_pop() {
    // requirements: FIFO-REQ-003
    let fifo = Arc::new(BoundedFifo::<i32, 2>::new());
    let fifo_clone = Arc::clone(&fifo);

    let handle = thread::spawn(move || {
        thread::sleep(Duration::from_millis(10));
        assert_eq!(fifo_clone.push(99), Ok(()));
    });

    assert_eq!(fifo.pop(), Ok(99));
    handle.join().unwrap();
}

// ===========================================================================
// Tier 2: Boundary, Timeouts, and Rejection Contracts
// ===========================================================================

#[test]
fn test_fifo_capacity_one_boundary() {
    // requirements: FIFO-REQ-001, FIFO-REQ-004
    let fifo = BoundedFifo::<i32, 1>::new();
    assert_eq!(fifo.try_push(42), Ok(()));
    assert!(fifo.is_full());

    // Queue is full: push must reject and return original value
    let err = fifo.try_push(99).unwrap_err();
    assert_eq!(err, PushError::Full(99));
    assert_eq!(err.into_inner(), 99);

    assert_eq!(fifo.try_pop(), Ok(42));
    assert!(fifo.is_empty());
    assert_eq!(fifo.try_pop(), Err(PopError::Empty));
}

#[test]
fn test_fifo_rejection_contract() {
    // requirements: FIFO-REQ-004
    let fifo = BoundedFifo::<String, 1>::new();
    let original = "payload".to_string();
    assert_eq!(fifo.try_push(original), Ok(()));

    let rejected = "rejected".to_string();
    let err = fifo.try_push(rejected).unwrap_err();
    match err {
        PushError::Full(val) => assert_eq!(val, "rejected"),
        PushError::Closed(_) => panic!("expected Full error"),
    }
}

#[test]
fn test_fifo_timeouts() {
    // requirements: FIFO-REQ-003
    let fifo = BoundedFifo::<i32, 1>::new();

    // Pop timeout on empty queue
    let pop_err = fifo.pop_timeout(Duration::from_millis(15)).unwrap_err();
    assert_eq!(pop_err, PopTimeoutError::Timeout);

    // Fill queue
    assert_eq!(fifo.try_push(1), Ok(()));

    // Push timeout on full queue
    let push_err = fifo.push_timeout(2, Duration::from_millis(15)).unwrap_err();
    assert_eq!(push_err, PushTimeoutError::Timeout(2));
    assert_eq!(push_err.into_inner(), 2);
}

// ===========================================================================
// Tier 3: RAII, Drop, and Waking Close
// ===========================================================================

#[test]
fn test_fifo_waking_close() {
    // requirements: FIFO-REQ-005
    // 1. Blocked producer woken on full queue
    let full_fifo = Arc::new(BoundedFifo::<i32, 1>::new());
    assert_eq!(full_fifo.try_push(1), Ok(()));

    let fifo_prod = Arc::clone(&full_fifo);
    let prod_handle = thread::spawn(move || {
        // Blocked because queue is full
        fifo_prod.push(2)
    });

    thread::sleep(Duration::from_millis(20));
    full_fifo.close();
    assert!(full_fifo.is_closed());
    assert_eq!(prod_handle.join().unwrap(), Err(PushError::Closed(2)));

    // 2. Blocked consumer woken on empty queue
    let empty_fifo = Arc::new(BoundedFifo::<i32, 2>::new());
    let fifo_cons = Arc::clone(&empty_fifo);
    let cons_handle = thread::spawn(move || {
        // Blocked because queue is empty
        fifo_cons.pop()
    });

    thread::sleep(Duration::from_millis(20));
    empty_fifo.close();
    assert!(empty_fifo.is_closed());
    assert_eq!(cons_handle.join().unwrap(), Err(PopError::Closed));
}

#[test]
fn test_fifo_default_trait() {
    // requirements: FIFO-REQ-001
    let fifo = BoundedFifo::<i32, 2>::default();
    assert_eq!(fifo.capacity(), 2);
    assert_eq!(fifo.len(), 0);
    assert!(fifo.is_empty());
}

#[test]
fn test_fifo_drainage_after_close() {
    // requirements: FIFO-REQ-005
    let fifo = BoundedFifo::<i32, 4>::new();
    assert_eq!(fifo.try_push(1), Ok(()));
    assert_eq!(fifo.try_push(2), Ok(()));

    fifo.close();
    assert!(fifo.is_closed());

    // Subsequent pushes rejected immediately via try_push, push, and push_timeout
    let err_try = fifo.try_push(3).unwrap_err();
    assert_eq!(err_try, PushError::Closed(3));
    assert_eq!(err_try.into_inner(), 3);

    let err_push = fifo.push(4).unwrap_err();
    assert_eq!(err_push, PushError::Closed(4));
    assert_eq!(err_push.into_inner(), 4);

    let err_timeout = fifo.push_timeout(5, Duration::from_millis(5)).unwrap_err();
    assert_eq!(err_timeout, PushTimeoutError::Closed(5));
    assert_eq!(err_timeout.into_inner(), 5);

    // Remaining items can be fully drained
    assert_eq!(fifo.try_pop(), Ok(1));
    assert_eq!(fifo.try_pop(), Ok(2));

    // When empty and closed, all pop variants return Closed
    assert_eq!(fifo.try_pop(), Err(PopError::Closed));
    assert_eq!(fifo.pop(), Err(PopError::Closed));
    assert_eq!(fifo.pop_timeout(Duration::from_millis(5)), Err(PopTimeoutError::Closed));
}

#[test]
fn test_fifo_raii_drop_unconsumed_elements() {
    // requirements: FIFO-REQ-006
    let drop_counter = Arc::new(AtomicUsize::new(0));

    {
        let fifo = BoundedFifo::<DropDetector, 8>::new();
        for i in 0..5 {
            assert!(fifo.try_push(DropDetector::new(i, Arc::clone(&drop_counter))).is_ok());
        }

        // Pop and drop 2 items
        let Ok(item0) = fifo.try_pop() else {
            panic!("expected item0");
        };
        let Ok(item1) = fifo.try_pop() else {
            panic!("expected item1");
        };
        drop(item0);
        drop(item1);
        assert_eq!(drop_counter.load(Ordering::SeqCst), 2);

        // 3 items remain in fifo
    } // fifo is dropped here

    // All 5 items must have been dropped cleanly
    assert_eq!(drop_counter.load(Ordering::SeqCst), 5);
}

// ===========================================================================
// Tier 4: Multi-Threaded MPMC Stress
// ===========================================================================

#[test]
fn test_fifo_mpmc_stress() {
    // requirements: FIFO-REQ-002
    const QUEUE_CAPACITY: usize = 16;
    const NUM_PRODUCERS: usize = 4;
    const NUM_CONSUMERS: usize = 4;
    const ITEMS_PER_PRODUCER: usize = 2500;

    let fifo = Arc::new(BoundedFifo::<usize, QUEUE_CAPACITY>::new());
    let mut producer_handles = Vec::with_capacity(NUM_PRODUCERS);

    for p in 0..NUM_PRODUCERS {
        let fifo_p = Arc::clone(&fifo);
        let handle = thread::spawn(move || {
            let base = p * ITEMS_PER_PRODUCER;
            for i in 0..ITEMS_PER_PRODUCER {
                let item = base + i;
                fifo_p.push(item).unwrap();
            }
        });
        producer_handles.push(handle);
    }

    let total_consumed = Arc::new(AtomicUsize::new(0));
    let checksum = Arc::new(AtomicUsize::new(0));
    let mut consumer_handles = Vec::with_capacity(NUM_CONSUMERS);

    for _ in 0..NUM_CONSUMERS {
        let fifo_c = Arc::clone(&fifo);
        let count_ref = Arc::clone(&total_consumed);
        let sum_ref = Arc::clone(&checksum);

        let handle = thread::spawn(move || {
            while let Ok(val) = fifo_c.pop() {
                count_ref.fetch_add(1, Ordering::Relaxed);
                sum_ref.fetch_add(val, Ordering::Relaxed);
            }
        });
        consumer_handles.push(handle);
    }

    for h in producer_handles {
        h.join().unwrap();
    }

    // Wait until all items have been popped
    while total_consumed.load(Ordering::Relaxed) < NUM_PRODUCERS * ITEMS_PER_PRODUCER {
        thread::yield_now();
    }

    fifo.close();

    for h in consumer_handles {
        h.join().unwrap();
    }

    let total_items = NUM_PRODUCERS * ITEMS_PER_PRODUCER;
    assert_eq!(total_consumed.load(Ordering::SeqCst), total_items);

    // Sum of 0..(total_items - 1)
    let expected_sum = total_items * (total_items - 1) / 2;
    assert_eq!(checksum.load(Ordering::SeqCst), expected_sum);
}

// ===========================================================================
// Tier 5: Adversarial Stress, Contention, and Edge-Case Mining
// ===========================================================================

#[test]
fn test_adversarial_mpmc_contention_capacity_one() {
    // requirements: FIFO-REQ-001, FIFO-REQ-002
    // Capacity 1 is the worst-case boundary: every push/pop alternates buffer full/empty state.
    const QUEUE_CAPACITY: usize = 1;
    const NUM_PRODUCERS: usize = 8;
    const NUM_CONSUMERS: usize = 8;
    const ITEMS_PER_PRODUCER: usize = 2500;
    const TOTAL_ITEMS: usize = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    let fifo = Arc::new(BoundedFifo::<usize, QUEUE_CAPACITY>::new());
    let mut producer_handles = Vec::with_capacity(NUM_PRODUCERS);

    for p in 0..NUM_PRODUCERS {
        let fifo_p = Arc::clone(&fifo);
        let handle = thread::spawn(move || {
            let base = p * ITEMS_PER_PRODUCER;
            for i in 0..ITEMS_PER_PRODUCER {
                let item = base + i;
                fifo_p.push(item).unwrap();
            }
        });
        producer_handles.push(handle);
    }

    let total_consumed = Arc::new(AtomicUsize::new(0));
    let checksum = Arc::new(AtomicUsize::new(0));
    let mut consumer_handles = Vec::with_capacity(NUM_CONSUMERS);

    for _ in 0..NUM_CONSUMERS {
        let fifo_c = Arc::clone(&fifo);
        let count_ref = Arc::clone(&total_consumed);
        let sum_ref = Arc::clone(&checksum);

        let handle = thread::spawn(move || {
            while let Ok(val) = fifo_c.pop() {
                count_ref.fetch_add(1, Ordering::Relaxed);
                sum_ref.fetch_add(val, Ordering::Relaxed);
            }
        });
        consumer_handles.push(handle);
    }

    for h in producer_handles {
        h.join().unwrap();
    }

    while total_consumed.load(Ordering::Relaxed) < TOTAL_ITEMS {
        thread::yield_now();
    }

    fifo.close();

    for h in consumer_handles {
        h.join().unwrap();
    }

    assert_eq!(total_consumed.load(Ordering::SeqCst), TOTAL_ITEMS);
    let expected_sum = TOTAL_ITEMS * (TOTAL_ITEMS - 1) / 2;
    assert_eq!(checksum.load(Ordering::SeqCst), expected_sum);
}

#[test]
fn test_adversarial_heavy_mpmc_contention_and_exact_delivery() {
    // requirements: FIFO-REQ-002
    // Prime-number capacity 7 with 16 producers and 16 consumers transmitting 32,000 items.
    // Verifies that every single item from 0 to 31,999 is received exactly once.
    const QUEUE_CAPACITY: usize = 7;
    const NUM_PRODUCERS: usize = 16;
    const NUM_CONSUMERS: usize = 16;
    const ITEMS_PER_PRODUCER: usize = 2000;
    const TOTAL_ITEMS: usize = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    let fifo = Arc::new(BoundedFifo::<usize, QUEUE_CAPACITY>::new());
    let mut producer_handles = Vec::with_capacity(NUM_PRODUCERS);

    for p in 0..NUM_PRODUCERS {
        let fifo_p = Arc::clone(&fifo);
        let handle = thread::spawn(move || {
            let base = p * ITEMS_PER_PRODUCER;
            for i in 0..ITEMS_PER_PRODUCER {
                let item = base + i;
                // Mix of try_push with backoff and push
                if i % 3 == 0 {
                    let mut val = item;
                    loop {
                        match fifo_p.try_push(val) {
                            Ok(()) => break,
                            Err(PushError::Full(rejected)) => {
                                val = rejected;
                                thread::yield_now();
                            }
                            Err(PushError::Closed(_)) => panic!("queue closed prematurely"),
                        }
                    }
                } else {
                    fifo_p.push(item).unwrap();
                }
            }
        });
        producer_handles.push(handle);
    }

    let received = Arc::new(Mutex::new(vec![false; TOTAL_ITEMS]));
    let total_consumed = Arc::new(AtomicUsize::new(0));
    let mut consumer_handles = Vec::with_capacity(NUM_CONSUMERS);

    for _ in 0..NUM_CONSUMERS {
        let fifo_c = Arc::clone(&fifo);
        let rec_ref = Arc::clone(&received);
        let count_ref = Arc::clone(&total_consumed);

        let handle = thread::spawn(move || {
            while let Ok(val) = fifo_c.pop() {
                count_ref.fetch_add(1, Ordering::Relaxed);
                let mut guard = rec_ref.lock().unwrap();
                assert!(!guard[val], "duplicate delivery of item {val}");
                guard[val] = true;
            }
        });
        consumer_handles.push(handle);
    }

    for h in producer_handles {
        h.join().unwrap();
    }

    while total_consumed.load(Ordering::Relaxed) < TOTAL_ITEMS {
        thread::yield_now();
    }

    fifo.close();

    for h in consumer_handles {
        h.join().unwrap();
    }

    assert_eq!(total_consumed.load(Ordering::SeqCst), TOTAL_ITEMS);
    let all_received = {
        let guard = received.lock().unwrap();
        guard.iter().all(|&seen| seen)
    };
    assert!(all_received, "all items must be received exactly once");
}

#[test]
fn test_adversarial_rejection_contract_zero_leaks() {
    // requirements: FIFO-REQ-004, FIFO-REQ-006
    // Stress test rejection contracts under repeated capacity saturation and shutdown,
    // ensuring zero leaks and exact single-drop destruction of heap-allocated elements.
    let drop_counter = Arc::new(AtomicUsize::new(0));

    {
        let fifo = BoundedFifo::<DropDetector, 2>::new();

        // Pre-fill queue with 2 elements (id 0, 1)
        assert!(fifo.try_push(DropDetector::new(0, Arc::clone(&drop_counter))).is_ok());
        assert!(fifo.try_push(DropDetector::new(1, Arc::clone(&drop_counter))).is_ok());
        assert!(fifo.is_full());

        // 1. Try push 1,000 times into full queue.
        for id in 2..1002 {
            let detector = DropDetector::new(id, Arc::clone(&drop_counter));
            let err = fifo.try_push(detector).unwrap_err();
            if id % 2 == 0 {
                // Extract via into_inner and explicitly drop
                let item = err.into_inner();
                assert_eq!(item.id, id);
                drop(item);
            } else {
                // Drop error enum directly
                drop(err);
            }
        }
        // At this point, 1,000 rejected items must have dropped. Pre-filled 2 still in queue.
        assert_eq!(drop_counter.load(Ordering::SeqCst), 1000);

        // 2. Timed push 500 times into full queue with small timeout.
        for id in 1002..1502 {
            let detector = DropDetector::new(id, Arc::clone(&drop_counter));
            let err = fifo.push_timeout(detector, Duration::from_micros(10)).unwrap_err();
            if id % 2 == 0 {
                let item = err.into_inner();
                assert_eq!(item.id, id);
                drop(item);
            } else {
                drop(err);
            }
        }
        assert_eq!(drop_counter.load(Ordering::SeqCst), 1500);

        // 3. Close the queue and attempt pushes on closed queue.
        fifo.close();
        assert!(fifo.is_closed());

        for id in 1502..2002 {
            let detector = DropDetector::new(id, Arc::clone(&drop_counter));
            let err = fifo.try_push(detector).unwrap_err();
            assert_eq!(err.into_inner().id, id);
        }
        assert_eq!(drop_counter.load(Ordering::SeqCst), 2000);

        for id in 2002..2502 {
            let detector = DropDetector::new(id, Arc::clone(&drop_counter));
            let err = fifo.push_timeout(detector, Duration::from_micros(10)).unwrap_err();
            assert_eq!(err.into_inner().id, id);
        }
        assert_eq!(drop_counter.load(Ordering::SeqCst), 2500);

        // Pre-filled 2 elements still inside fifo.
        assert_eq!(fifo.len(), 2);
    } // FIFO dropped here! The 2 pre-filled items must drop cleanly.

    assert_eq!(drop_counter.load(Ordering::SeqCst), 2502);
}

#[test]
fn test_adversarial_concurrent_close_unblocking() {
    // requirements: FIFO-REQ-005
    // Concurrently unblocks blocked producers on full queue and blocked consumers on empty queue,
    // with multiple threads simultaneously calling close().
    const NUM_PRODUCERS: usize = 12;
    const NUM_CONSUMERS: usize = 12;

    let fifo = Arc::new(BoundedFifo::<usize, 2>::new());

    // Pre-fill queue with 2 elements so producers block
    assert!(fifo.try_push(100).is_ok());
    assert!(fifo.try_push(101).is_ok());

    let mut producer_handles = Vec::with_capacity(NUM_PRODUCERS);
    let producers_started = Arc::new(AtomicUsize::new(0));

    for p in 0..NUM_PRODUCERS {
        let fifo_p = Arc::clone(&fifo);
        let started = Arc::clone(&producers_started);
        let handle = thread::spawn(move || {
            started.fetch_add(1, Ordering::SeqCst);
            fifo_p.push(p)
        });
        producer_handles.push(handle);
    }

    // Wait until all producers are running and blocked
    while producers_started.load(Ordering::SeqCst) < NUM_PRODUCERS {
        thread::yield_now();
    }
    thread::sleep(Duration::from_millis(30));

    // Create a second FIFO for blocked consumers
    let empty_fifo = Arc::new(BoundedFifo::<usize, 4>::new());
    let mut consumer_handles = Vec::with_capacity(NUM_CONSUMERS);
    let consumers_started = Arc::new(AtomicUsize::new(0));

    for _ in 0..NUM_CONSUMERS {
        let fifo_c = Arc::clone(&empty_fifo);
        let started = Arc::clone(&consumers_started);
        let handle = thread::spawn(move || {
            started.fetch_add(1, Ordering::SeqCst);
            fifo_c.pop()
        });
        consumer_handles.push(handle);
    }

    while consumers_started.load(Ordering::SeqCst) < NUM_CONSUMERS {
        thread::yield_now();
    }
    thread::sleep(Duration::from_millis(30));

    // Concurrently invoke close() from 4 threads
    let start_close = Instant::now();
    let mut close_handles = Vec::new();

    for _ in 0..4 {
        let f1 = Arc::clone(&fifo);
        let f2 = Arc::clone(&empty_fifo);
        close_handles.push(thread::spawn(move || {
            f1.close();
            f2.close();
        }));
    }

    for h in close_handles {
        h.join().unwrap();
    }

    // Verify all producers unblocked immediately with PushError::Closed
    for (p, handle) in producer_handles.into_iter().enumerate() {
        let res = handle.join().unwrap();
        assert_eq!(res, Err(PushError::Closed(p)));
    }

    // Verify all consumers unblocked immediately with PopError::Closed
    for handle in consumer_handles {
        let res = handle.join().unwrap();
        assert_eq!(res, Err(PopError::Closed));
    }

    assert!(
        start_close.elapsed() < Duration::from_millis(500),
        "close unblocking took too long"
    );
}

#[test]
fn test_adversarial_post_close_drain_behavior() {
    // requirements: FIFO-REQ-005
    let fifo = BoundedFifo::<i32, 5>::new();
    for i in 0..5 {
        assert_eq!(fifo.try_push(i), Ok(()));
    }
    assert!(fifo.is_full());

    fifo.close();
    assert!(fifo.is_closed());

    // Subsequent pushes must fail immediately
    assert_eq!(fifo.try_push(99), Err(PushError::Closed(99)));
    assert_eq!(fifo.push(99), Err(PushError::Closed(99)));
    assert_eq!(
        fifo.push_timeout(99, Duration::from_millis(100)),
        Err(PushTimeoutError::Closed(99))
    );

    // Queue still contains 5 elements
    assert_eq!(fifo.len(), 5);

    // Drain all 5 elements
    for i in 0..5 {
        assert_eq!(fifo.try_pop(), Ok(i));
    }

    // Now empty and closed
    assert_eq!(fifo.len(), 0);
    assert!(fifo.is_empty());
    assert_eq!(fifo.try_pop(), Err(PopError::Closed));
    assert_eq!(fifo.pop(), Err(PopError::Closed));
    assert_eq!(
        fifo.pop_timeout(Duration::from_millis(100)),
        Err(PopTimeoutError::Closed)
    );

    // Multiple close calls remain idempotent
    fifo.close();
    fifo.close();
    assert!(fifo.is_closed());
}

#[test]
fn test_adversarial_timeout_edge_cases() {
    // requirements: FIFO-REQ-003
    let fifo = Arc::new(BoundedFifo::<i32, 1>::new());

    // Duration::ZERO timeout on empty queue pop
    let start = Instant::now();
    assert_eq!(fifo.pop_timeout(Duration::ZERO), Err(PopTimeoutError::Timeout));
    assert!(start.elapsed() < Duration::from_millis(50));

    // Duration::ZERO timeout on non-full queue push
    assert_eq!(fifo.push_timeout(10, Duration::ZERO), Ok(()));

    // Duration::ZERO timeout on full queue push
    let start = Instant::now();
    let err = fifo.push_timeout(20, Duration::ZERO).unwrap_err();
    assert_eq!(err, PushTimeoutError::Timeout(20));
    assert_eq!(err.into_inner(), 20);
    assert!(start.elapsed() < Duration::from_millis(50));

    // Duration::ZERO timeout on non-empty queue pop
    assert_eq!(fifo.pop_timeout(Duration::ZERO), Ok(10));

    // Early wakeup: pop_timeout blocks with long timeout (5s), producer enqueues after 20ms
    let fifo_clone = Arc::clone(&fifo);
    let handle = thread::spawn(move || {
        let start = Instant::now();
        let val = fifo_clone.pop_timeout(Duration::from_secs(5));
        (val, start.elapsed())
    });

    thread::sleep(Duration::from_millis(20));
    assert_eq!(fifo.try_push(42), Ok(()));

    let (val, elapsed) = handle.join().unwrap();
    assert_eq!(val, Ok(42));
    assert!(
        elapsed < Duration::from_millis(500),
        "pop_timeout waited too long: {elapsed:?}"
    );
}
