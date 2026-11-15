#include "leet_code/design_circular_queue.hpp"

#include <cstddef>

namespace leet_code {

// -----------------------------------------------------------------------------
// Idiomatic Modern C++23 Implementation (TDD Stub)
// -----------------------------------------------------------------------------

CircularQueue::CircularQueue(size_t k) : queue_(k, 0), capacity_(k) {
}

auto CircularQueue::en_queue(int value) -> bool {
  if (is_full()) {
    return false;
  }
  queue_[tail_] = value;
  tail_ = (tail_ + 1uz) % capacity_;
  ++size_;
  return true;
}

auto CircularQueue::de_queue() -> bool {
  if (is_empty()) {
    return false;
  }
  head_ = (head_ + 1uz) % capacity_;
  --size_;
  return true;
}

auto CircularQueue::front() const -> int {
  if (is_empty()) {
    return -1;
  }
  return queue_[head_];
}

auto CircularQueue::rear() const -> int {
  if (is_empty()) {
    return -1;
  }
  size_t rear_index = tail_ == 0uz ? capacity_ - 1uz : tail_ - 1uz;
  return queue_[rear_index];
}

auto CircularQueue::is_empty() const -> bool {
  return size_ == 0uz;
}

auto CircularQueue::is_full() const -> bool {
  return size_ == capacity_;
}

// -----------------------------------------------------------------------------
// LeetCode Compatibility Wrapper
// -----------------------------------------------------------------------------

MyCircularQueue::MyCircularQueue(int k) : queue_(static_cast<size_t>(k)) {
}

auto MyCircularQueue::enQueue(int value) -> bool {
  return queue_.en_queue(value);
}

auto MyCircularQueue::deQueue() -> bool {
  return queue_.de_queue();
}

auto MyCircularQueue::Front() -> int {
  return queue_.front();
}

auto MyCircularQueue::Rear() -> int {
  return queue_.rear();
}

auto MyCircularQueue::isEmpty() -> bool {
  return queue_.is_empty();
}

auto MyCircularQueue::isFull() -> bool {
  return queue_.is_full();
}

}  // namespace leet_code
