#pragma once

/// @file design_hit_counter.hpp
/// @brief 362. Design Hit Counter (Medium)
///
/// Design a hit counter which counts the number of hits received in the past 5
/// minutes.
///
/// Each function accepts a timestamp parameter (in seconds granularity) and you
/// may assume that calls are being made to the system in chronological order
/// (ie, the timestamp is monotonically increasing). You may assume that the
/// earliest timestamp starts at 1.
///
/// It is possible that several hits arrive roughly at the same time.
///
/// Implement the HitCounter class:
/// - HitCounter() Initializes the object of the hit counter system.
/// - void hit(int timestamp) Records a hit that happened at timestamp (in
/// seconds).
/// - int getHits(int timestamp) Returns the number of hits in the past 5
/// minutes from timestamp.
///
/// Example:
/// HitCounter counter;
/// counter.hit(1);
/// counter.hit(2);
/// counter.hit(3);
/// counter.getHits(4);   // returns 3
/// counter.hit(300);
/// counter.getHits(300); // returns 4
/// counter.getHits(301); // returns 3
///
/// Constraints:
/// - 1 <= timestamp <= 2 * 10^9
/// - All the calls are being made to the system in chronological order
/// - At most 300 calls will be made to hit and getHits
///
/// Targets:
/// Time Complexity: $O(1)$
/// Space Complexity: $O(1)$ auxiliary space

#include <array>
#include <utility>

namespace leet_code {

class HitCounter {
 public:
  HitCounter() = default;
  ~HitCounter() = default;

  // Prevent copying and moving
  HitCounter(const HitCounter&) = delete;
  HitCounter& operator=(const HitCounter&) = delete;
  HitCounter(HitCounter&&) = delete;
  HitCounter& operator=(HitCounter&&) = delete;

  auto hit(int timestamp) -> void;

  [[nodiscard]] auto getHits(int timestamp) -> int;

 private:
  static constexpr size_t kBucketSize = 300;
  std::array<std::pair<int, int>, kBucketSize> buckets_{
      {
          {0, 0},
      },
  };  // <timestamp, count>
  size_t head_{0};
  size_t tail_{0};
  bool is_start_{true};
};

}  // namespace leet_code
