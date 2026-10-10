#include "leet_code/moving_average_from_data_stream.hpp"

#include <cstddef>

namespace leet_code {

MovingAverageImpl::MovingAverageImpl(std::size_t size)
    : size_(size),
      buffer_(size, 0),
      inv_size_(1.0 / static_cast<double>(size)) {}

auto MovingAverageImpl::next(int val) -> double {
  current_sum_ += val - buffer_[head_];
  buffer_[head_] = val;
  if (++head_ == size_) [[unlikely]] {
    head_ = 0;
  }
  if (count_ < size_) [[unlikely]] {
    return static_cast<double>(current_sum_) / static_cast<double>(++count_);
  }
  return static_cast<double>(current_sum_) * inv_size_;
}

MovingAverage::MovingAverage(int size)
    : impl_(static_cast<std::size_t>(size)) {}

auto MovingAverage::next(int val) -> double { return impl_.next(val); }

}  // namespace leet_code
