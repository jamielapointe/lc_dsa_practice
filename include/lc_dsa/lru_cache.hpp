#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>

namespace lc_dsa {

/// @file lru_cache.hpp
/// @brief 146. LRU Cache (Medium)
/// @details https://leetcode.com/problems/lru-cache/
///
/// Design a data structure that follows the constraints of a Least Recently
/// Used (LRU) cache.
///
/// Implement the `LRUCache` class:
/// * `LRUCache(int capacity)` Initialize the LRU cache with positive size
/// `capacity`.
/// * `int get(int key)` Return the value of the `key` if the key exists,
/// otherwise return `-1`.
/// * `void put(int key, int value)` Update the value of the `key` if the `key`
/// exists. Otherwise, add the `key-value` pair to the cache. If the number of
/// keys exceeds the `capacity` from this operation, evict the least recently
/// used key.
///
/// The functions `get` and `put` must each run in $O(1)$ average time
/// complexity.
///
/// Constraints:
/// * `1 <= capacity <= 3000`
/// * `0 <= key <= 10^4`
/// * `0 <= value <= 10^5`
/// * At most `2 * 10^5` calls will be made to `get` and `put`.
///
/// Example 1:
/// Input
/// ["LRUCache", "put", "put", "get", "put", "get", "put", "get", "get", "get"]
/// [[2], [1, 1], [2, 2], [1], [3, 3], [2], [4, 4], [1], [3], [4]]
/// Output
/// [null, null, null, 1, null, -1, null, -1, 3, 4]

/// @brief Modern C++23 Idiomatic Implementation
class LruCache {
 public:
  /// @brief Initialize the LRU cache with positive size capacity.
  explicit LruCache(int capacity);

  /// @brief Return the value of the key if the key exists, otherwise return -1.
  [[nodiscard]] auto get(int key) -> int;

  /// @brief Update the value of the key if the key exists. Otherwise, add the
  /// key-value pair to the cache.
  auto put(int key, int value) -> void;

 private:
  struct Entry {
    int key;
    int value;
  };
  size_t capacity_;
  using List = std::list<Entry>;
  List lru_list_;
  std::unordered_map<int, List::iterator> cache_;

  void move_to_front(List::iterator it) {
    lru_list_.splice(lru_list_.begin(), lru_list_, it);
  }
};

}  // namespace lc_dsa
