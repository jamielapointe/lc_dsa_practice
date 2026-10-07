#include "lc_dsa/lru_cache.hpp"

#include <cstddef>
#include <stdexcept>

namespace lc_dsa {

LruCache::LruCache(int capacity) : capacity_(static_cast<size_t>(capacity)) {
  // a check to ensure capacity > 0 with appropriate error handling
  if (capacity <= 0) {
    throw std::invalid_argument("Capacity must be positive");
  }
  cache_.reserve(capacity_ + 1U);
}

auto LruCache::get(int key) -> int {
  auto it = cache_.find(key);

  if (it == cache_.end()) {
    return -1;
  }

  move_to_front(it->second);

  return it->second->value;
}

auto LruCache::put(int key, int value) -> void {
  if (auto it = cache_.find(key); it != cache_.end()) {
    it->second->value = value;
    move_to_front(it->second);
    return;
  }

  if (lru_list_.size() == capacity_) {
    cache_.erase(lru_list_.back().key);
    lru_list_.pop_back();
  }

  lru_list_.emplace_front(key, value);
  cache_.emplace(key, lru_list_.begin());
}

}  // namespace lc_dsa
