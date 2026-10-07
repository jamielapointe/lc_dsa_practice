#include "lc_dsa/moving_average_from_data_stream.hpp"

namespace lc_dsa {

MovingAverageImpl::MovingAverageImpl(int size) : size_(size) {
  static_cast<void>(size_);
  static_cast<void>(current_sum_);
  static_cast<void>(window_);
}

auto MovingAverageImpl::next(int val) -> double {
  static_cast<void>(val);
  static_cast<void>(size_);  // silence static method warning
  return 0.0;
}

MovingAverage::MovingAverage(int size) : impl_(size) {}

auto MovingAverage::next(int val) -> double { return impl_.next(val); }

}  // namespace lc_dsa
