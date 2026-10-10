#include "leet_code/design_hit_counter.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

// Tier 1: Canonical Feature Coverage
TEST(DesignHitCounterTest, OfficialExample) {
  leet_code::HitCounter counter;
  counter.hit(1);
  counter.hit(2);
  counter.hit(3);
  EXPECT_EQ(counter.getHits(4), 3);
  counter.hit(300);
  EXPECT_EQ(counter.getHits(300), 4);
  EXPECT_EQ(counter.getHits(301), 3);
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(DesignHitCounterTest, EdgeCases) {
  leet_code::HitCounter counter;
  // Get hits before any hit
  EXPECT_EQ(counter.getHits(1), 0);

  // Hit at 1, get at 1
  counter.hit(1);
  EXPECT_EQ(counter.getHits(1), 1);

  // Multiple hits at same timestamp
  counter.hit(2);
  counter.hit(2);
  counter.hit(2);
  EXPECT_EQ(counter.getHits(2), 4);

  // Exactly 300 seconds diff
  EXPECT_EQ(counter.getHits(300), 4);
  // 301 seconds diff -> the hit at 1 is dropped
  EXPECT_EQ(counter.getHits(301), 3);
}

// Tier 3: Sanitizer & Memory Invariants
TEST(DesignHitCounterTest, MemoryInvariants) {
  auto* counter = new leet_code::HitCounter();
  counter->hit(10);
  counter->hit(20);
  EXPECT_EQ(counter->getHits(30), 2);
  delete counter;  // Ensure no leaks
}

// Tier 4: Stress Workloads & OJ Simulation
TEST(DesignHitCounterTest, StressTest) {
  leet_code::HitCounter counter;
  auto start = std::chrono::steady_clock::now();

  // Programmatically generate a lot of hits
  for (int i = 1; i <= 10000; ++i) {
    counter.hit(i);
  }

  int result = counter.getHits(10000);
  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(result, 300);
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_LT(duration, std::chrono::milliseconds(200));
}

}  // namespace
