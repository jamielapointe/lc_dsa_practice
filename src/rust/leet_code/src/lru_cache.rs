//! LeetCode #146: LRU Cache.
//!
//! URL: <https://leetcode.com/problems/lru-cache/>

use std::collections::HashMap;

/// An internal doubly-linked arena node holding key, value, and neighbors.
#[derive(Clone, Debug)]
struct Node {
    key: i32,
    value: i32,
    prev: Option<usize>,
    next: Option<usize>,
}

/// Fixed-capacity least-recently-used cache with O(1) average time operations.
///
/// Implemented using an arena/index-pool backing (`Vec<Node>` with `Option<usize>` indices)
/// to eliminate `Rc` or `RefCell` overhead and completely avoid `unsafe` blocks.
#[derive(Clone, Debug)]
pub struct LruCache {
    capacity: usize,
    map: HashMap<i32, usize>,
    nodes: Vec<Node>,
    free_list: Vec<usize>,
    head: Option<usize>, // Most recently used
    tail: Option<usize>, // Least recently used
}

impl LruCache {
    /// Constructs a new `LruCache` with the specified maximum capacity.
    ///
    /// # Examples
    ///
    /// ```
    /// use leet_code::lru_cache::LruCache;
    ///
    /// let cache = LruCache::new(2);
    /// assert_eq!(cache.capacity(), 2);
    /// assert_eq!(cache.len(), 0);
    /// ```
    #[must_use]
    pub fn new(capacity: usize) -> Self {
        Self {
            capacity,
            map: HashMap::with_capacity(capacity),
            nodes: Vec::with_capacity(capacity),
            free_list: Vec::new(),
            head: None,
            tail: None,
        }
    }

    /// Returns the maximum capacity of the cache.
    #[must_use]
    pub const fn capacity(&self) -> usize {
        self.capacity
    }

    /// Returns the number of key-value pairs currently stored in the cache.
    #[must_use]
    pub fn len(&self) -> usize {
        self.map.len()
    }

    /// Returns `true` if the cache contains no entries.
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.map.is_empty()
    }

    /// Returns `true` if the cache contains the specified key without updating recency.
    #[must_use]
    pub fn contains_key(&self, key: i32) -> bool {
        self.map.contains_key(&key)
    }

    /// Retrieves the cached value and marks `key` as most recently used.
    ///
    /// Returns `None` if `key` is not present.
    ///
    /// # Examples
    ///
    /// ```
    /// use leet_code::lru_cache::LruCache;
    ///
    /// let mut cache = LruCache::new(2);
    /// cache.put(1, 100);
    /// assert_eq!(cache.get(1), Some(100));
    /// assert_eq!(cache.get(2), None);
    /// ```
    pub fn get(&mut self, key: i32) -> Option<i32> {
        if let Some(&idx) = self.map.get(&key) {
            self.move_to_head(idx);
            Some(self.nodes[idx].value)
        } else {
            None
        }
    }

    /// Inserts or updates the given key-value pair, marking it as most recently used.
    ///
    /// If the cache exceeds capacity, the least recently used entry is evicted.
    ///
    /// # Examples
    ///
    /// ```
    /// use leet_code::lru_cache::LruCache;
    ///
    /// let mut cache = LruCache::new(2);
    /// cache.put(1, 10);
    /// cache.put(2, 20);
    /// cache.put(3, 30); // Evicts key 1
    /// assert_eq!(cache.get(1), None);
    /// assert_eq!(cache.get(2), Some(20));
    /// assert_eq!(cache.get(3), Some(30));
    /// ```
    pub fn put(&mut self, key: i32, value: i32) {
        if self.capacity == 0 {
            return;
        }

        if let Some(&idx) = self.map.get(&key) {
            self.nodes[idx].value = value;
            self.move_to_head(idx);
            return;
        }

        if self.map.len() >= self.capacity {
            let Some(lru_idx) = self.tail else {
                return;
            };
            self.detach(lru_idx);
            let evicted_key = self.nodes[lru_idx].key;
            self.map.remove(&evicted_key);
            self.free_list.push(lru_idx);
        }

        let new_idx = if let Some(free_idx) = self.free_list.pop() {
            self.nodes[free_idx] = Node {
                key,
                value,
                prev: None,
                next: None,
            };
            free_idx
        } else {
            let fresh_idx = self.nodes.len();
            self.nodes.push(Node {
                key,
                value,
                prev: None,
                next: None,
            });
            fresh_idx
        };

        self.attach_head(new_idx);
        self.map.insert(key, new_idx);
    }

    fn detach(&mut self, idx: usize) {
        let prev = self.nodes[idx].prev;
        let next = self.nodes[idx].next;

        if let Some(p) = prev {
            self.nodes[p].next = next;
        } else {
            self.head = next;
        }

        if let Some(n) = next {
            self.nodes[n].prev = prev;
        } else {
            self.tail = prev;
        }

        self.nodes[idx].prev = None;
        self.nodes[idx].next = None;
    }

    fn attach_head(&mut self, idx: usize) {
        self.nodes[idx].prev = None;
        self.nodes[idx].next = self.head;

        if let Some(old_head) = self.head {
            self.nodes[old_head].prev = Some(idx);
        } else {
            self.tail = Some(idx);
        }

        self.head = Some(idx);
    }

    fn move_to_head(&mut self, idx: usize) {
        if self.head == Some(idx) {
            return;
        }
        self.detach(idx);
        self.attach_head(idx);
    }
}

/// LeetCode compatibility wrapper conforming to the official problem interface.
#[derive(Clone, Debug)]
pub struct LRUCache {
    inner: LruCache,
}

impl LRUCache {
    /// Constructs a new `LRUCache` with capacity `capacity`.
    #[must_use]
    pub fn new(capacity: i32) -> Self {
        let cap = usize::try_from(capacity.max(0)).unwrap_or(0);
        Self {
            inner: LruCache::new(cap),
        }
    }

    /// Returns the value of `key` if it exists, otherwise returns `-1`.
    pub fn get(&mut self, key: i32) -> i32 {
        self.inner.get(key).unwrap_or(-1)
    }

    /// Inserts or updates `key` with `value`.
    pub fn put(&mut self, key: i32, value: i32) {
        self.inner.put(key, value);
    }
}
