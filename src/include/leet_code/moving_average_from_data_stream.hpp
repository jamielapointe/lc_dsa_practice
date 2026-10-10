#pragma once

#include <cstddef>
#include <vector>

namespace leet_code {

/// @file moving_average_from_data_stream.hpp
/// @brief 346. Moving Average from Data Stream (Easy) -
/// https://leetcode.com/problems/moving-average-from-data-stream/
///
/// Given a stream of integers and a window size, calculate the moving average
/// of all integers in the sliding window.
///
/// Implement the MovingAverage class:
/// * MovingAverage(int size) Initializes the object with the size of the window
/// size.
/// * double next(int val) Returns the moving average of the last size values of
/// the stream.
///
/// Examples:
/// MovingAverage movingAverage = new MovingAverage(3);
/// movingAverage.next(1); // return 1.0 = 1 / 1
/// movingAverage.next(10); // return 5.5 = (1 + 10) / 2
/// movingAverage.next(3); // return 4.66667 = (1 + 10 + 3) / 3
/// movingAverage.next(5); // return 6.0 = (10 + 3 + 5) / 3
///
/// Constraints:
/// * 1 <= size <= 1000
/// * -10^5 <= val <= 10^5
/// * At most 10^4 calls will be made to next.
///
/// Complexity:
/// * Time: O(1) per next() call
/// * Space: O(size) for the queue

class MovingAverageImpl {
 public:
  explicit MovingAverageImpl(size_t size);

  [[nodiscard]] auto next(int val) -> double;

 private:
  size_t size_{0};
  int32_t current_sum_{0};
  std::vector<int32_t> buffer_;
  size_t head_{0};
  size_t count_{0};
  double inv_size_;
};

// LeetCode Compatibility Wrapper
class MovingAverage {
 public:
  explicit MovingAverage(int size);
  auto next(int val) -> double;

 private:
  MovingAverageImpl impl_;
};

}  // namespace leet_code
