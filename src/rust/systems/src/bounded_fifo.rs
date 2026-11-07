//! Fixed-capacity, thread-safe (MPMC) FIFO queue with zero steady-state heap allocation.
//!
//! Storage lives entirely inline within the struct using const generics, eliminating
//! steady-state heap allocations and making it ideal for deterministic robotics and systems software.

use std::sync::Condvar;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;

/// Error returned when a push operation fails.
#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum PushError<T> {
    /// The queue has reached its maximum configured capacity. Contains the rejected item.
    #[error("queue is full")]
    Full(T),
    /// The queue has been closed and cannot accept new items. Contains the rejected item.
    #[error("queue is closed")]
    Closed(T),
}

impl<T> PushError<T> {
    /// Extracts the rejected item from the error.
    #[must_use]
    pub fn into_inner(self) -> T {
        match self {
            Self::Full(item) | Self::Closed(item) => item,
        }
    }
}

/// Error returned when a timed push operation fails.
#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum PushTimeoutError<T> {
    /// Timed out waiting for available queue space. Contains the rejected item.
    #[error("timed out waiting for space")]
    Timeout(T),
    /// The queue has been closed and cannot accept new items. Contains the rejected item.
    #[error("queue is closed")]
    Closed(T),
}

impl<T> PushTimeoutError<T> {
    /// Extracts the rejected item from the error.
    #[must_use]
    pub fn into_inner(self) -> T {
        match self {
            Self::Timeout(item) | Self::Closed(item) => item,
        }
    }
}

/// Error returned when a pop operation fails.
#[derive(Clone, Copy, Debug, Eq, PartialEq, thiserror::Error)]
pub enum PopError {
    /// The queue contains no items.
    #[error("queue is empty")]
    Empty,
    /// The queue has been closed and all remaining items drained.
    #[error("queue is closed")]
    Closed,
}

/// Error returned when a timed pop operation fails.
#[derive(Clone, Copy, Debug, Eq, PartialEq, thiserror::Error)]
pub enum PopTimeoutError {
    /// Timed out waiting for an element.
    #[error("timed out waiting for element")]
    Timeout,
    /// The queue has been closed and all remaining items drained.
    #[error("queue is closed")]
    Closed,
}

/// Internal ring buffer state guarded by the mutex.
struct State<T, const CAPACITY: usize> {
    slots: [Option<T>; CAPACITY],
    head: usize,
    count: usize,
    closed: bool,
}

/// Fixed-capacity, thread-safe (MPMC) circular FIFO queue with zero steady-state heap allocations.
///
/// Storage is allocated inline via const generics. Synchronization uses a standard mutex and
/// dual condition variables (`not_empty` and `not_full`), guaranteeing complete memory safety
/// with zero `unsafe` blocks.
pub struct BoundedFifo<T, const CAPACITY: usize> {
    state: Mutex<State<T, CAPACITY>>,
    not_empty: Condvar,
    not_full: Condvar,
}

impl<T, const CAPACITY: usize> Default for BoundedFifo<T, CAPACITY> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T, const CAPACITY: usize> BoundedFifo<T, CAPACITY> {
    /// Constructs a new empty `BoundedFifo`.
    ///
    /// # Panics
    ///
    /// Panics at compile time if `CAPACITY` is zero.
    #[must_use]
    pub const fn new() -> Self {
        const {
            assert!(CAPACITY > 0, "BoundedFifo capacity must be strictly positive");
        }

        Self {
            state: Mutex::new(State {
                slots: [const { None }; CAPACITY],
                head: 0,
                count: 0,
                closed: false,
            }),
            not_empty: Condvar::new(),
            not_full: Condvar::new(),
        }
    }

    /// Returns the maximum capacity of the queue.
    #[must_use]
    pub const fn capacity(&self) -> usize {
        CAPACITY
    }

    /// Returns the current number of queued elements (snapshot).
    #[must_use]
    pub fn len(&self) -> usize {
        self.state.lock().map_or(0, |state| state.count)
    }

    /// Returns `true` if the queue is currently empty (snapshot).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Returns `true` if the queue is currently full (snapshot).
    #[must_use]
    pub fn is_full(&self) -> bool {
        self.len() >= CAPACITY
    }

    /// Returns `true` once [`BoundedFifo::close`] has been called (snapshot).
    #[must_use]
    pub fn is_closed(&self) -> bool {
        self.state.lock().map_or(true, |state| state.closed)
    }

    /// Closes the queue, preventing subsequent pushes and waking all blocked threads.
    ///
    /// Consumers may continue draining any remaining elements until empty.
    pub fn close(&self) {
        {
            if let Ok(mut state) = self.state.lock() {
                state.closed = true;
            }
        }
        self.not_empty.notify_all();
        self.not_full.notify_all();
    }

    /// Attempts to enqueue an element without blocking.
    ///
    /// # Errors
    ///
    /// Returns [`PushError::Closed`] if the queue is closed.
    /// Returns [`PushError::Full`] if the queue is already at capacity.
    /// In both error cases, ownership of `item` is returned in the error payload.
    pub fn try_push(&self, item: T) -> Result<(), PushError<T>> {
        let Ok(mut state) = self.state.lock() else {
            return Err(PushError::Closed(item));
        };

        if state.closed {
            return Err(PushError::Closed(item));
        }

        if state.count >= CAPACITY {
            return Err(PushError::Full(item));
        }

        let tail = (state.head + state.count) % CAPACITY;
        state.slots[tail] = Some(item);
        state.count += 1;
        drop(state);

        self.not_empty.notify_one();
        Ok(())
    }

    /// Enqueues an element, blocking the calling thread while the queue is full.
    ///
    /// # Errors
    ///
    /// Returns [`PushError::Closed`] with the unconsumed item if the queue is closed
    /// before or while waiting.
    pub fn push(&self, item: T) -> Result<(), PushError<T>> {
        let Ok(mut state) = self.state.lock() else {
            return Err(PushError::Closed(item));
        };

        while !state.closed && state.count >= CAPACITY {
            state = match self.not_full.wait(state) {
                Ok(guard) => guard,
                Err(_) => return Err(PushError::Closed(item)),
            };
        }

        if state.closed {
            return Err(PushError::Closed(item));
        }

        let tail = (state.head + state.count) % CAPACITY;
        state.slots[tail] = Some(item);
        state.count += 1;
        drop(state);

        self.not_empty.notify_one();
        Ok(())
    }

    /// Enqueues an element, blocking until space is available or the timeout expires.
    ///
    /// # Errors
    ///
    /// Returns [`PushTimeoutError::Closed`] with `item` if the queue is closed.
    /// Returns [`PushTimeoutError::Timeout`] with `item` if `timeout` expires before space becomes available.
    pub fn push_timeout(&self, item: T, timeout: Duration) -> Result<(), PushTimeoutError<T>> {
        let start = Instant::now();
        let Ok(mut state) = self.state.lock() else {
            return Err(PushTimeoutError::Closed(item));
        };

        while !state.closed && state.count >= CAPACITY {
            let elapsed = start.elapsed();
            let Some(remaining) = timeout.checked_sub(elapsed) else {
                return Err(PushTimeoutError::Timeout(item));
            };
            let Ok((guard, wait_res)) = self.not_full.wait_timeout(state, remaining) else {
                return Err(PushTimeoutError::Closed(item));
            };
            state = guard;
            if wait_res.timed_out() && !state.closed && state.count >= CAPACITY {
                return Err(PushTimeoutError::Timeout(item));
            }
        }

        if state.closed {
            return Err(PushTimeoutError::Closed(item));
        }

        let tail = (state.head + state.count) % CAPACITY;
        state.slots[tail] = Some(item);
        state.count += 1;
        drop(state);

        self.not_empty.notify_one();
        Ok(())
    }

    /// Attempts to dequeue the oldest element without blocking.
    ///
    /// # Errors
    ///
    /// Returns [`PopError::Empty`] if the queue currently contains no elements.
    /// Returns [`PopError::Closed`] if the queue is closed and completely drained.
    pub fn try_pop(&self) -> Result<T, PopError> {
        let Ok(mut state) = self.state.lock() else {
            return Err(PopError::Closed);
        };

        if state.count > 0 {
            let head = state.head;
            let item = state.slots[head].take();
            state.head = (head + 1) % CAPACITY;
            state.count -= 1;
            drop(state);

            self.not_full.notify_one();
            item.ok_or(PopError::Empty)
        } else if state.closed {
            Err(PopError::Closed)
        } else {
            Err(PopError::Empty)
        }
    }

    /// Dequeues the oldest element, blocking the calling thread while the queue is empty.
    ///
    /// # Errors
    ///
    /// Returns [`PopError::Closed`] once the queue is closed and completely drained.
    pub fn pop(&self) -> Result<T, PopError> {
        let Ok(mut state) = self.state.lock() else {
            return Err(PopError::Closed);
        };

        while !state.closed && state.count == 0 {
            state = match self.not_empty.wait(state) {
                Ok(guard) => guard,
                Err(_) => return Err(PopError::Closed),
            };
        }

        if state.count > 0 {
            let head = state.head;
            let item = state.slots[head].take();
            state.head = (head + 1) % CAPACITY;
            state.count -= 1;
            drop(state);

            self.not_full.notify_one();
            item.ok_or(PopError::Closed)
        } else {
            Err(PopError::Closed)
        }
    }

    /// Dequeues the oldest element, blocking until an element is available or the timeout expires.
    ///
    /// # Errors
    ///
    /// Returns [`PopTimeoutError::Closed`] if the queue is closed and drained.
    /// Returns [`PopTimeoutError::Timeout`] if `timeout` expires before an element is enqueued.
    pub fn pop_timeout(&self, timeout: Duration) -> Result<T, PopTimeoutError> {
        let start = Instant::now();
        let Ok(mut state) = self.state.lock() else {
            return Err(PopTimeoutError::Closed);
        };

        while !state.closed && state.count == 0 {
            let elapsed = start.elapsed();
            let Some(remaining) = timeout.checked_sub(elapsed) else {
                return Err(PopTimeoutError::Timeout);
            };
            let Ok((guard, wait_res)) = self.not_empty.wait_timeout(state, remaining) else {
                return Err(PopTimeoutError::Closed);
            };
            state = guard;
            if wait_res.timed_out() && !state.closed && state.count == 0 {
                return Err(PopTimeoutError::Timeout);
            }
        }

        if state.count > 0 {
            let head = state.head;
            let item = state.slots[head].take();
            state.head = (head + 1) % CAPACITY;
            state.count -= 1;
            drop(state);

            self.not_full.notify_one();
            item.ok_or(PopTimeoutError::Closed)
        } else {
            Err(PopTimeoutError::Closed)
        }
    }
}
