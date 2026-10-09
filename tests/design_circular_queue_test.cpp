#include "lc_dsa/design_circular_queue.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

// -----------------------------------------------------------------------------
// Tier 1: Canonical Feature Coverage (LeetCode Examples)
// -----------------------------------------------------------------------------

TEST(DesignCircularQueueTest, Example1) {
  lc_dsa::MyCircularQueue my_queue(3);
  EXPECT_EQ(my_queue.enQueue(1), true);
  EXPECT_EQ(my_queue.enQueue(2), true);
  EXPECT_EQ(my_queue.enQueue(3), true);
  EXPECT_EQ(my_queue.enQueue(4), false);
  EXPECT_EQ(my_queue.Rear(), 3);
  EXPECT_EQ(my_queue.isFull(), true);
  EXPECT_EQ(my_queue.deQueue(), true);
  EXPECT_EQ(my_queue.enQueue(4), true);
  EXPECT_EQ(my_queue.Rear(), 4);
}

TEST(DesignCircularQueueModernTest, Example1) {
  lc_dsa::CircularQueue queue(3);
  EXPECT_EQ(queue.en_queue(1), true);
  EXPECT_EQ(queue.en_queue(2), true);
  EXPECT_EQ(queue.en_queue(3), true);
  EXPECT_EQ(queue.en_queue(4), false);
  EXPECT_EQ(queue.rear(), 3);
  EXPECT_EQ(queue.is_full(), true);
  EXPECT_EQ(queue.de_queue(), true);
  EXPECT_EQ(queue.en_queue(4), true);
  EXPECT_EQ(queue.rear(), 4);
}

// -----------------------------------------------------------------------------
// Tier 2: Boundary, Corner & Edge Cases
// -----------------------------------------------------------------------------

TEST(DesignCircularQueueTest, CapacityOne) {
  lc_dsa::CircularQueue queue(1);
  EXPECT_EQ(queue.is_empty(), true);
  EXPECT_EQ(queue.is_full(), false);
  EXPECT_EQ(queue.en_queue(10), true);
  EXPECT_EQ(queue.is_empty(), false);
  EXPECT_EQ(queue.is_full(), true);
  EXPECT_EQ(queue.en_queue(20), false);
  EXPECT_EQ(queue.front(), 10);
  EXPECT_EQ(queue.rear(), 10);
  EXPECT_EQ(queue.de_queue(), true);
  EXPECT_EQ(queue.is_empty(), true);
  EXPECT_EQ(queue.front(), -1);
  EXPECT_EQ(queue.rear(), -1);
}

TEST(DesignCircularQueueTest, EmptyOperations) {
  lc_dsa::CircularQueue queue(5);
  EXPECT_EQ(queue.de_queue(), false);
  EXPECT_EQ(queue.front(), -1);
  EXPECT_EQ(queue.rear(), -1);
}

// -----------------------------------------------------------------------------
// Tier 3: Sanitizer & Memory Invariants
// -----------------------------------------------------------------------------

TEST(DesignCircularQueueTest, WrapAroundMemory) {
  lc_dsa::CircularQueue queue(3);
  EXPECT_EQ(queue.en_queue(1), true);
  EXPECT_EQ(queue.en_queue(2), true);
  EXPECT_EQ(queue.en_queue(3), true);
  EXPECT_EQ(queue.de_queue(), true);
  EXPECT_EQ(queue.de_queue(), true);
  EXPECT_EQ(queue.en_queue(4), true);
  EXPECT_EQ(queue.en_queue(5), true);  // Wrapping around
  EXPECT_EQ(queue.front(), 3);
  EXPECT_EQ(queue.rear(), 5);
}

// -----------------------------------------------------------------------------
// Tier 4: Stress Workloads & OJ Simulation
// -----------------------------------------------------------------------------

TEST(DesignCircularQueueTest, StressTest) {
  const int k = 1000;
  lc_dsa::CircularQueue queue(k);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < k; ++i) {
    EXPECT_EQ(queue.en_queue(i), true);
  }
  EXPECT_EQ(queue.is_full(), true);

  for (int i = 0; i < 500; ++i) {
    EXPECT_EQ(queue.de_queue(), true);
  }

  for (int i = 0; i < 500; ++i) {
    EXPECT_EQ(queue.en_queue(k + i), true);
  }
  EXPECT_EQ(queue.is_full(), true);

  for (int i = 0; i < k; ++i) {
    EXPECT_EQ(queue.de_queue(), true);
  }
  EXPECT_EQ(queue.is_empty(), true);

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  EXPECT_LT(duration, std::chrono::milliseconds(200));
}

}  // namespace
