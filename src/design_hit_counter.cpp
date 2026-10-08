#include "lc_dsa/design_hit_counter.hpp"

#include <cstddef>

namespace lc_dsa {

auto HitCounter::hit(int timestamp) -> void {
  if (is_start_) [[unlikely]] {
    is_start_ = false;
    buckets_[tail_] = {timestamp, 1};
    return;
  }

  // Circular buffer logic
  if (buckets_[tail_].first == timestamp) {
    ++buckets_[tail_].second;
  } else {
    buckets_[tail_ = (tail_ + 1) % kBucketSize] = {timestamp, 1};
    if (tail_ == head_) [[unlikely]] {
      head_ = (head_ + 1) % kBucketSize;
    }
  }
}

auto HitCounter::getHits(int timestamp) -> int {
  if (is_start_) [[unlikely]] {
    return 0;
  }
  if (head_ == tail_) [[unlikely]] {
    if ((timestamp - buckets_[tail_].first) < static_cast<int>(kBucketSize)) {
      return buckets_[tail_].second;
    }
    return 0;
  }

  // Now tail_ is guaranteed to not equal head_
  // Recall timestamp shall be monitonic (or equal).
  int hits = 0;
  size_t current_head = head_;
  if ((timestamp - buckets_[current_head].first) <
      static_cast<int>(kBucketSize)) {
    hits += buckets_[current_head].second;
  }
  current_head = (current_head + 1) % kBucketSize;
  while (current_head != head_) {
    if ((timestamp - buckets_[current_head].first) <
        static_cast<int>(kBucketSize)) {
      hits += buckets_[current_head].second;
    }
    current_head = (current_head + 1) % kBucketSize;
  }
  return hits;
}

}  // namespace lc_dsa
