//! Bounded polling with exponential backoff for lock-free subscribers.
//!
//! Assumption A-7 & SHMPS-REQ-013/014:
//! Spin 64, yield 1, then sleep 50 µs doubling to 1 ms, clamped to deadline.

use std::thread;
use std::time::{Duration, Instant};

/// Bounded backoff state machine for subscriber polling.
#[derive(Debug)]
pub struct Backoff {
    spins: u32,
    yielded: bool,
    sleep: Duration,
}

impl Backoff {
    const SPIN_LIMIT: u32 = 64;
    const FIRST_SLEEP: Duration = Duration::from_micros(50);
    const MAX_SLEEP: Duration = Duration::from_millis(1);

    /// Creates a fresh backoff instance starting at spin 0.
    #[must_use]
    pub const fn new() -> Self {
        Self {
            spins: 0,
            yielded: false,
            sleep: Self::FIRST_SLEEP,
        }
    }

    /// Resets the backoff state back to spin 0.
    pub const fn reset(&mut self) {
        self.spins = 0;
        self.yielded = false;
        self.sleep = Self::FIRST_SLEEP;
    }

    /// Performs one step of escalating wait (spin -> yield -> exponential sleep).
    pub fn wait(&mut self, deadline: Option<Instant>) {
        if self.spins < Self::SPIN_LIMIT {
            self.spins += 1;
            core::hint::spin_loop();
            return;
        }

        if !self.yielded {
            self.yielded = true;
            thread::yield_now();
            return;
        }

        if let Some(dl) = deadline {
            let now = Instant::now();
            if now >= dl {
                return;
            }
            let remaining = dl - now;
            let sleep_duration = self.sleep.min(remaining);
            thread::sleep(sleep_duration);
        } else {
            thread::sleep(self.sleep);
        }

        self.sleep = (self.sleep * 2).min(Self::MAX_SLEEP);
    }
}

impl Default for Backoff {
    fn default() -> Self {
        Self::new()
    }
}
