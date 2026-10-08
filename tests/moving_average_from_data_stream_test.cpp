#include "lc_dsa/moving_average_from_data_stream.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

// Tier 1: Canonical Feature Coverage
TEST(MovingAverageTest, LeetCodeExample) {
  lc_dsa::MovingAverage ma(3);
  EXPECT_DOUBLE_EQ(ma.next(1), 1.0);
  EXPECT_DOUBLE_EQ(ma.next(10), 5.5);
  EXPECT_NEAR(ma.next(3), 4.66667, 1e-4);
  EXPECT_DOUBLE_EQ(ma.next(5), 6.0);
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(MovingAverageTest, WindowSizeOne) {
  lc_dsa::MovingAverage ma(1);
  EXPECT_DOUBLE_EQ(ma.next(5), 5.0);
  EXPECT_DOUBLE_EQ(ma.next(10), 10.0);
  EXPECT_DOUBLE_EQ(ma.next(-3), -3.0);
}

// Tier 4: Stress Workloads & OJ Simulation
TEST(MovingAverageTest, StressTest) {
  lc_dsa::MovingAverage ma(1000);
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < 10000; ++i) {
    ma.next(i);
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_LT(duration.count(), 200);
}

}  // namespace
