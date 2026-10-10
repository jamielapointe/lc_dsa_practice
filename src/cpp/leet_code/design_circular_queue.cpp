#include "leet_code/design_circular_queue.hpp"

#include <cstddef>

namespace leet_code {

// -----------------------------------------------------------------------------
// Idiomatic Modern C++23 Implementation (TDD Stub)
// -----------------------------------------------------------------------------

CircularQueue::CircularQueue(size_t k) : queue_(k, 0), capacity_(k) {}

auto CircularQueue::en_queue(int value) -> bool {
  if (is_full()) {
    return false;
  }
  queue_[tail_] = value;
  tail_ = (tail_ + 1) % capacity_;
  ++size_;
  return true;
}

auto CircularQueue::de_queue() -> bool {
  if (is_empty()) {
    return false;
  }
  head_ = (head_ + 1) % capacity_;
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
  size_t rear_index = tail_ == 0 ? capacity_ - 1 : tail_ - 1;
  return queue_[rear_index];
}

auto CircularQueue::is_empty() const -> bool { return size_ == 0; }

auto CircularQueue::is_full() const -> bool { return size_ == capacity_; }

// -----------------------------------------------------------------------------
// LeetCode Compatibility Wrapper
// -----------------------------------------------------------------------------

MyCircularQueue::MyCircularQueue(int k) : queue_(static_cast<size_t>(k)) {}

bool MyCircularQueue::enQueue(int value) { return queue_.en_queue(value); }

bool MyCircularQueue::deQueue() { return queue_.de_queue(); }

int MyCircularQueue::Front() { return queue_.front(); }

int MyCircularQueue::Rear() { return queue_.rear(); }

bool MyCircularQueue::isEmpty() { return queue_.is_empty(); }

bool MyCircularQueue::isFull() { return queue_.is_full(); }

}  // namespace leet_code
