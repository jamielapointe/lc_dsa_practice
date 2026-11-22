//! LeetCode #703: Kth Largest Element in a Stream (Easy).
//!
//! URL: <https://leetcode.com/problems/kth-largest-element-in-a-stream/>
//!
//! # Problem Description
//!
//! Implement a class which, for a given integer `k`, maintains a stream of test
//! scores and continuously returns the `k`th highest score in the sorted list
//! of all scores after each new score is submitted. More specifically, we are
//! looking for the `k`th highest score in the sorted list of all scores (not
//! the `k`th distinct score).
//!
//! - `KthLargest(int k, int[] nums)` initializes the object with the integer
//!   `k` and the stream of test scores `nums`.
//! - `int add(int val)` adds a new test score `val` to the stream and returns
//!   the element representing the `k`th largest element in the pool of test
//!   scores so far.
//!
//! # Examples
//!
//! 1. `["KthLargest", "add", "add", "add", "add", "add"]` with
//!    `[[3, [4, 5, 8, 2]], [3], [5], [10], [9], [4]]` -> `[null, 4, 5, 5, 8, 8]`
//! 2. `["KthLargest", "add", "add", "add", "add"]` with
//!    `[[4, [7, 7, 7, 7, 8, 3]], [2], [10], [9], [9]]` -> `[null, 7, 7, 7, 8]`
//!
//! # Constraints
//!
//! - `0 <= nums.length <= 10^4`
//! - `1 <= k <= nums.length + 1`
//! - `-10^4 <= nums[i] <= 10^4`
//! - `-10^4 <= val <= 10^4`
//! - At most `10^4` calls will be made to `add`.
//!
//! # Complexity Target
//!
//! - Construction: `O(n log k)` (or `O(n)` heapify + bounded pruning) over `n = nums.len()`
//! - Per `add` operation: `O(log k)` worst-case time, zero steady-state heap reallocation
//! - Auxiliary space: `O(k)` bounded min-heap retaining at most `k` elements
//!
//! # Preconditions
//!
//! Precondition validation in [`KthLargestStream::new`] executes in strict
//! deterministic order:
//!
//! 1. `k == 0` returns [`KthLargestError::InvalidK`] (checked first, even when
//!    `nums` is empty).
//! 2. `nums.len().saturating_add(1) < k` returns
//!    [`KthLargestError::InsufficientInitialElements`].
//! 3. Starved initialization where `nums.len() + 1 == k` (for example,
//!    `k = 1, nums = &[]` or `k = 3, nums = &[10, 20]`) is valid, guaranteeing
//!    at least `k` elements exist once the first [`KthLargestStream::add`] call
//!    is made.
use std::cmp::Reverse;
use std::collections::BinaryHeap;

/// Errors that may occur when constructing a [`KthLargestStream`].
#[must_use]
#[non_exhaustive]
#[derive(Clone, Copy, Debug, Eq, PartialEq, thiserror::Error)]
pub enum KthLargestError {
    /// Requested rank `k` is zero (`k` must be `>= 1`).
    #[error("k must be at least 1")]
    InvalidK,
    /// Initial slice contains fewer than `k - 1` elements, so the first `add`
    /// call would still have fewer than `k` elements in the pool.
    #[error("initial slice must contain at least k - 1 elements")]
    InsufficientInitialElements,
    /// Stub placeholder returned before the algorithm is implemented.
    #[error("kth largest stream is not yet implemented")]
    NotImplemented,
}

/// Bounded streaming `k`th-largest tracker backed by at most `k` retained scores.
#[derive(Clone, Debug)]
pub struct KthLargestStream {
    k: usize,
    min_heap: BinaryHeap<Reverse<i32>>,
}

impl KthLargestStream {
    /// Constructs a new [`KthLargestStream`] tracking the `k`th largest element
    /// seeded with initial scores from `nums`.
    ///
    /// Borrowed input `nums` is never mutated. Starved initialization with
    /// `nums.len() + 1 == k` (such as `k = 1, nums = &[]`) succeeds with
    /// `self.len() == k - 1` prior to the first [`Self::add`] call.
    ///
    /// # Errors
    ///
    /// Preconditions are checked in strict deterministic order:
    ///
    /// 1. Returns [`KthLargestError::InvalidK`] if `k == 0` (even when `nums`
    ///    is empty).
    /// 2. Returns [`KthLargestError::InsufficientInitialElements`] if
    ///    `nums.len().saturating_add(1) < k`.
    /// 3. Returns [`KthLargestError::NotImplemented`] while this constructor
    ///    remains a TDD-red stub.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # fn main() -> Result<(), leet_code::kth_largest_element_in_a_stream::KthLargestError> {
    /// use leet_code::kth_largest_element_in_a_stream::KthLargestStream;
    ///
    /// let mut stream = KthLargestStream::new(3, &[4, 5, 8, 2])?;
    /// assert_eq!(stream.k(), 3);
    /// assert_eq!(stream.len(), 3);
    /// assert_eq!(stream.add(3), 4);
    /// assert_eq!(stream.add(5), 5);
    /// assert_eq!(stream.add(10), 5);
    /// assert_eq!(stream.add(9), 8);
    /// assert_eq!(stream.add(4), 8);
    /// # Ok(())
    /// # }
    /// ```
    pub fn new(k: usize, nums: &[i32]) -> Result<Self, KthLargestError> {
        if k == 0 {
            return Err(KthLargestError::InvalidK);
        }
        if nums.len().saturating_add(1) < k {
            return Err(KthLargestError::InsufficientInitialElements);
        }

        let mut min_heap = BinaryHeap::with_capacity(k + 1);
        for &num in nums {
            min_heap.push(Reverse(num));

            if min_heap.len() > k {
                min_heap.pop();
            }
        }

        Ok(Self { k, min_heap })
    }

    /// Returns the target rank `k` (`>= 1`) configured for this stream.
    #[must_use]
    pub const fn k(&self) -> usize {
        self.k
    }

    /// Returns the number of elements currently retained in the bounded state
    /// (`<= self.k()`).
    #[must_use]
    pub fn len(&self) -> usize {
        self.min_heap.len()
    }

    /// Returns `true` if the stream currently retains zero elements (possible
    /// only before the first [`Self::add`] when initialized with `k = 1` and
    /// `nums = &[]`).
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.min_heap.is_empty()
    }

    /// Inserts `val` into the stream and returns the `k`th largest element seen
    /// so far.
    ///
    /// # Examples
    ///
    /// ```no_run
    /// # fn main() -> Result<(), leet_code::kth_largest_element_in_a_stream::KthLargestError> {
    /// use leet_code::kth_largest_element_in_a_stream::KthLargestStream;
    ///
    /// let mut stream = KthLargestStream::new(1, &[])?;
    /// assert!(stream.is_empty());
    /// assert_eq!(stream.add(42), 42);
    /// assert_eq!(stream.len(), 1);
    /// # Ok(())
    /// # }
    /// ```
    pub fn add(&mut self, val: i32) -> i32 {
        self.min_heap.push(Reverse(val));
        if self.min_heap.len() > self.k {
            self.min_heap.pop();
        }
        self.min_heap.peek().map_or(val, |&Reverse(kth)| kth)
    }
}

/// LeetCode compatibility wrapper conforming to the official `KthLargest` signature.
#[derive(Clone, Debug)]
pub struct KthLargest {
    inner: Option<KthLargestStream>,
}

impl KthLargest {
    /// Initializes `KthLargest` with integer rank `k` and initial stream `nums`.
    ///
    /// # Panics
    ///
    /// This constructor never panics; invalid preconditions leave the wrapper
    /// in a safe fallback state returning `0` on [`Self::add`].
    ///
    /// # Examples
    ///
    /// ```no_run
    /// use leet_code::kth_largest_element_in_a_stream::KthLargest;
    ///
    /// let mut kth = KthLargest::new(3, vec![4, 5, 8, 2]);
    /// assert_eq!(kth.add(3), 4);
    /// assert_eq!(kth.add(5), 5);
    /// ```
    #[must_use]
    #[expect(
        clippy::needless_pass_by_value,
        reason = "Matches official LeetCode Vec<i32> signature"
    )]
    pub fn new(k: i32, nums: Vec<i32>) -> Self {
        let inner = usize::try_from(k)
            .ok()
            .and_then(|k_usize| KthLargestStream::new(k_usize, &nums).ok());
        Self { inner }
    }

    /// Appends `val` to the stream and returns the `k`th largest element so far.
    ///
    /// # Panics
    ///
    /// This method never panics.
    pub fn add(&mut self, val: i32) -> i32 {
        self.inner.as_mut().map_or(0, |stream| stream.add(val))
    }
}
