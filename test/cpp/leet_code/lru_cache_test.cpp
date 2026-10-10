#include "leet_code/lru_cache.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>

namespace {

// Tier 1: Canonical Feature Coverage
TEST(LruCacheTest, Example1Modern) {
  leet_code::LruCache cache(2);
  cache.put(1, 1);
  cache.put(2, 2);
  EXPECT_EQ(cache.get(1), 1);
  cache.put(3, 3);
  EXPECT_EQ(cache.get(2), -1);
  cache.put(4, 4);
  EXPECT_EQ(cache.get(1), -1);
  EXPECT_EQ(cache.get(3), 3);
  EXPECT_EQ(cache.get(4), 4);
}

TEST(LruCacheTest, ZeroOrNegativeCapacityThrows) {
  EXPECT_THROW(leet_code::LruCache(0), std::invalid_argument);
  EXPECT_THROW(leet_code::LruCache(-1), std::invalid_argument);
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(LruCacheTest, CapacityOne) {
  leet_code::LruCache cache(1);
  cache.put(1, 10);
  EXPECT_EQ(cache.get(1), 10);
  cache.put(2, 20);
  EXPECT_EQ(cache.get(1), -1);
  EXPECT_EQ(cache.get(2), 20);
}

TEST(LruCacheTest, UpdateExistingKey) {
  leet_code::LruCache cache(2);
  cache.put(1, 10);
  cache.put(1, 100);
  EXPECT_EQ(cache.get(1), 100);
}

// Tier 3: Sanitizer & Memory Invariants
TEST(LruCacheTest, ManyEvictionsMemoryCheck) {
  leet_code::LruCache cache(10);
  for (int i = 0; i < 100; ++i) {
    cache.put(i, i * 10);
  }
  EXPECT_EQ(cache.get(0), -1);
  EXPECT_EQ(cache.get(99), 990);
}

// Tier 4: Stress Workloads & OJ Simulation
TEST(LruCacheTest, StressTest) {
  constexpr int kCapacity = 3000;
  constexpr int kOperations = 200000;
  leet_code::LruCache cache(kCapacity);

  auto start = std::chrono::steady_clock::now();

  for (int i = 0; i < kOperations; ++i) {
    cache.put(i, i);
  }
  for (int i = kOperations - kCapacity; i < kOperations; ++i) {
    EXPECT_EQ(cache.get(i), i);
  }
  for (int i = 0; i < kOperations - kCapacity; ++i) {
    EXPECT_EQ(cache.get(i), -1);
  }

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  // O(1) ops should finish well within 500ms (especially with ASan enabled)
  EXPECT_LT(duration, std::chrono::milliseconds(500));
}

}  // namespace
