/**
 * @file bounded_fifo_test.cpp
 * @brief Google Test suite for systems::BoundedFifo.
 *
 * Tiers:
 *   1. Canonical FIFO behaviour
 *   2. Boundary / corner cases (capacity 1, move-only types, in-place
 * construction)
 *   3. Lifetime & exception-safety invariants (checked further by ASan / UBSan)
 *   4. Concurrency: blocking, close(), timeouts, SPSC / MPMC stress (checked
 * further by TSan)
 */

#include "systems/bounded_fifo.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using std::chrono::milliseconds;
using systems::BoundedFifo;

// ---------------------------------------------------------------------------
// Test helper types
// ---------------------------------------------------------------------------

/// Counts live instances through an externally owned counter (no global state).
class Tracked {
 public:
  Tracked(int value, int* live) : value_(value), live_(live) { ++*live_; }
  Tracked(const Tracked& other) : value_(other.value_), live_(other.live_) {
    ++*live_;
  }
  Tracked(Tracked&& other) noexcept : value_(other.value_), live_(other.live_) {
    ++*live_;
  }
  auto operator=(const Tracked&) -> Tracked& = default;
  auto operator=(Tracked&&) noexcept -> Tracked& = default;
  ~Tracked() { --*live_; }

  [[nodiscard]] auto value() const -> int { return value_; }

 private:
  int value_;
  int* live_;
};

/// Throws from its constructor on demand to exercise the strong exception
/// guarantee.
struct MayThrow {
  explicit MayThrow(bool should_throw, int v) : value(v) {
    if (should_throw) {
      throw std::runtime_error{"boom"};
    }
  }
  int value;
};

// ---------------------------------------------------------------------------
// Tier 1: Canonical feature coverage
// ---------------------------------------------------------------------------

TEST(BoundedFifoTest, StartsEmptyWithConfiguredCapacity) {
  BoundedFifo<int, 4> fifo;
  EXPECT_TRUE(fifo.empty());
  EXPECT_FALSE(fifo.full());
  EXPECT_EQ(fifo.size(), 0U);
  EXPECT_EQ((BoundedFifo<int, 4>::capacity()), 4U);
  EXPECT_FALSE(fifo.closed());
}

TEST(BoundedFifoTest, PreservesFifoOrder) {
  BoundedFifo<int, 8> fifo;
  for (int i = 0; i < 5; ++i) {
    ASSERT_TRUE(fifo.try_push(i));
  }
  EXPECT_EQ(fifo.size(), 5U);
  for (int i = 0; i < 5; ++i) {
    const auto value = fifo.try_pop();
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, i);
  }
  EXPECT_TRUE(fifo.empty());
}

TEST(BoundedFifoTest, TryPushFailsWhenFull) {
  BoundedFifo<int, 2> fifo;
  EXPECT_TRUE(fifo.try_push(1));
  EXPECT_TRUE(fifo.try_push(2));
  EXPECT_TRUE(fifo.full());
  EXPECT_FALSE(fifo.try_push(3));
  EXPECT_EQ(fifo.size(), 2U);
}

TEST(BoundedFifoTest, TryPopReturnsNulloptWhenEmpty) {
  BoundedFifo<int, 2> fifo;
  EXPECT_EQ(fifo.try_pop(), std::nullopt);
}

TEST(BoundedFifoTest, WrapsAroundTheRingBuffer) {
  BoundedFifo<int, 3> fifo;
  int next_in = 0;
  int next_out = 0;
  for (int round = 0; round < 20; ++round) {
    while (fifo.try_push(next_in)) {
      ++next_in;
    }
    // Drain two of the three so head_ advances across the wrap point every
    // round.
    for (int i = 0; i < 2; ++i) {
      const auto value = fifo.try_pop();
      ASSERT_TRUE(value.has_value());
      EXPECT_EQ(*value, next_out++);
    }
  }
  while (const auto value = fifo.try_pop()) {
    EXPECT_EQ(*value, next_out++);
  }
  EXPECT_EQ(next_in, next_out);
}

TEST(BoundedFifoTest, BlockingPushAndPopWorkWithoutContention) {
  BoundedFifo<std::string, 2> fifo;
  ASSERT_TRUE(fifo.push(std::string{"a"}));
  const std::string b = "b";
  ASSERT_TRUE(fifo.push(b));
  EXPECT_EQ(fifo.pop(), "a");
  EXPECT_EQ(fifo.pop(), "b");
}

// ---------------------------------------------------------------------------
// Tier 2: Boundary, corner & edge cases
// ---------------------------------------------------------------------------

TEST(BoundedFifoTest, CapacityOneAlternatesPushPop) {
  BoundedFifo<int, 1> fifo;
  for (int i = 0; i < 10; ++i) {
    ASSERT_TRUE(fifo.try_push(i));
    EXPECT_FALSE(fifo.try_push(-1));
    EXPECT_EQ(fifo.try_pop(), i);
    EXPECT_EQ(fifo.try_pop(), std::nullopt);
  }
}

TEST(BoundedFifoTest, SupportsMoveOnlyTypes) {
  BoundedFifo<std::unique_ptr<int>, 2> fifo;
  ASSERT_TRUE(fifo.try_push(std::make_unique<int>(7)));
  ASSERT_TRUE(fifo.try_emplace(std::make_unique<int>(9)));
  auto first = fifo.try_pop();
  ASSERT_TRUE(first.has_value());
  ASSERT_NE(*first, nullptr);
  EXPECT_EQ(**first, 7);
  auto second = fifo.pop();
  ASSERT_TRUE(second.has_value());
  ASSERT_NE(*second, nullptr);
  EXPECT_EQ(**second, 9);
}

TEST(BoundedFifoTest, FailedMovePushLeavesArgumentUntouched) {
  BoundedFifo<std::unique_ptr<int>, 1> fifo;
  ASSERT_TRUE(fifo.try_push(std::make_unique<int>(1)));
  auto rejected = std::make_unique<int>(2);
  EXPECT_FALSE(fifo.try_push(std::move(rejected)));
  // Documented guarantee: a failed try_push leaves its rvalue argument
  // untouched. NOLINTBEGIN(bugprone-use-after-move)
  ASSERT_NE(rejected, nullptr);
  EXPECT_EQ(*rejected, 2);
  // NOLINTEND(bugprone-use-after-move)
}

TEST(BoundedFifoTest, ConstructsElementsInPlace) {
  BoundedFifo<std::string, 2> fifo;
  ASSERT_TRUE(fifo.try_emplace(3U, 'x'));
  ASSERT_TRUE(fifo.emplace("hello"));
  EXPECT_EQ(fifo.try_pop(), "xxx");
  EXPECT_EQ(fifo.try_pop(), "hello");
}

TEST(BoundedFifoTest, TimedOperationsTimeOutOnFullAndEmpty) {
  BoundedFifo<int, 1> fifo;
  EXPECT_EQ(fifo.pop_for(milliseconds{10}), std::nullopt);
  ASSERT_TRUE(fifo.try_push(1));
  EXPECT_FALSE(fifo.emplace_for(milliseconds{10}, 2));
  EXPECT_EQ(fifo.pop_for(milliseconds{10}), 1);
  EXPECT_TRUE(fifo.emplace_for(milliseconds{10}, 3));
  EXPECT_EQ(fifo.size(), 1U);
}

TEST(BoundedFifoTest, ClosedQueueRejectsPushesButDrainsExistingElements) {
  BoundedFifo<int, 4> fifo;
  ASSERT_TRUE(fifo.try_push(1));
  ASSERT_TRUE(fifo.try_push(2));
  fifo.close();
  EXPECT_TRUE(fifo.closed());
  EXPECT_FALSE(fifo.try_push(3));
  EXPECT_FALSE(fifo.push(3));
  EXPECT_FALSE(fifo.emplace_for(milliseconds{1}, 3));
  EXPECT_EQ(fifo.pop(), 1);
  EXPECT_EQ(fifo.pop(), 2);
  EXPECT_EQ(fifo.pop(), std::nullopt);
  EXPECT_EQ(fifo.try_pop(), std::nullopt);
}

// ---------------------------------------------------------------------------
// Tier 3: Lifetime & exception-safety invariants
// ---------------------------------------------------------------------------

TEST(BoundedFifoTest, DestructorReleasesQueuedElements) {
  int live = 0;
  {
    BoundedFifo<Tracked, 4> fifo;
    ASSERT_TRUE(fifo.try_emplace(1, &live));
    ASSERT_TRUE(fifo.try_emplace(2, &live));
    ASSERT_TRUE(fifo.try_emplace(3, &live));
    EXPECT_EQ(live, 3);
  }
  EXPECT_EQ(live, 0);
}

TEST(BoundedFifoTest, PopReleasesTheSlot) {
  int live = 0;
  BoundedFifo<Tracked, 2> fifo;
  ASSERT_TRUE(fifo.try_emplace(1, &live));
  ASSERT_TRUE(fifo.try_emplace(2, &live));
  EXPECT_EQ(live, 2);
  {
    const auto popped = fifo.try_pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(popped->value(), 1);
    EXPECT_EQ(live, 2);  // One in the queue, one held by `popped`.
  }
  EXPECT_EQ(live, 1);
  ASSERT_TRUE(fifo.try_emplace(3, &live));
  EXPECT_EQ(fifo.size(), 2U);
}

TEST(BoundedFifoTest, ThrowingConstructorLeavesQueueUnchanged) {
  BoundedFifo<MayThrow, 2> fifo;
  ASSERT_TRUE(fifo.try_emplace(false, 1));
  EXPECT_THROW(std::ignore = fifo.try_emplace(true, 2), std::runtime_error);
  EXPECT_EQ(fifo.size(), 1U);
  ASSERT_TRUE(fifo.try_emplace(false, 3));
  const auto first = fifo.try_pop();
  const auto second = fifo.try_pop();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(first->value, 1);
  EXPECT_EQ(second->value, 3);
}

TEST(BoundedFifoTest, ObjectLivesInStaticStorageWithoutHeap) {
  static BoundedFifo<std::uint8_t, 8>
      fifo;  // Large, trivially constructible; no allocation involved.
  ASSERT_TRUE(fifo.try_push(std::uint8_t{0xAB}));
  EXPECT_EQ(fifo.try_pop(), std::uint8_t{0xAB});
}

// ---------------------------------------------------------------------------
// Tier 4: Concurrency, shutdown, and stress workloads
// ---------------------------------------------------------------------------

TEST(BoundedFifoTest, BlockedPopIsWokenByPush) {
  BoundedFifo<int, 2> fifo;
  std::optional<int> received;
  std::thread consumer([&] { received = fifo.pop(); });
  std::this_thread::sleep_for(milliseconds{20});
  ASSERT_TRUE(fifo.try_push(42));
  consumer.join();
  EXPECT_EQ(received, 42);
}

TEST(BoundedFifoTest, BlockedPushIsWokenByPop) {
  BoundedFifo<int, 1> fifo;
  ASSERT_TRUE(fifo.try_push(1));
  std::atomic<bool> pushed{false};
  std::thread producer([&] { pushed = fifo.push(2); });
  std::this_thread::sleep_for(milliseconds{20});
  EXPECT_FALSE(pushed.load());
  EXPECT_EQ(fifo.pop(), 1);
  producer.join();
  EXPECT_TRUE(pushed.load());
  EXPECT_EQ(fifo.try_pop(), 2);
}

TEST(BoundedFifoTest, CloseWakesBlockedConsumersAndProducers) {
  BoundedFifo<int, 1> empty_fifo;
  BoundedFifo<int, 1> full_fifo;
  ASSERT_TRUE(full_fifo.try_push(1));

  std::optional<int> popped{123};
  std::atomic<bool> push_result{true};
  std::thread consumer([&] { popped = empty_fifo.pop(); });
  std::thread producer([&] { push_result = full_fifo.push(2); });
  std::this_thread::sleep_for(milliseconds{20});
  empty_fifo.close();
  full_fifo.close();
  consumer.join();
  producer.join();

  EXPECT_EQ(popped, std::nullopt);
  EXPECT_FALSE(push_result.load());
}

TEST(BoundedFifoStressTest, SingleProducerSingleConsumerKeepsOrder) {
  constexpr int kCount = 100000;
  BoundedFifo<int, 64> fifo;
  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      ASSERT_TRUE(fifo.push(i));
    }
    fifo.close();
  });

  int expected = 0;
  while (const auto value = fifo.pop()) {
    ASSERT_EQ(*value, expected);
    ++expected;
  }
  producer.join();
  EXPECT_EQ(expected, kCount);
}

TEST(BoundedFifoStressTest, MultiProducerMultiConsumerDeliversEverythingOnce) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kPerProducer = 25000;
  BoundedFifo<std::uint64_t, 32> fifo;

  std::atomic<std::uint64_t> sum{0};
  std::atomic<std::uint64_t> received{0};
  // Per-producer last-seen sequence number: each consumer checks monotonicity
  // per producer id using its own local table, which is valid because a single
  // consumer pops in FIFO order.
  std::vector<std::thread> consumers;
  consumers.reserve(kConsumers);
  std::atomic<bool> order_violation{false};
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([&] {
      std::vector<std::int64_t> last_seen(kProducers, -1);
      while (const auto value = fifo.pop()) {
        const auto producer_id = static_cast<std::size_t>(*value >> 32U);
        const auto sequence = static_cast<std::int64_t>(*value & 0xFFFFFFFFU);
        if (sequence <= last_seen[producer_id]) {
          order_violation = true;
        }
        last_seen[producer_id] = sequence;
        sum += *value & 0xFFFFFFFFU;
        ++received;
      }
    });
  }

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&fifo, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        const auto item = (static_cast<std::uint64_t>(p) << 32U) |
                          static_cast<std::uint64_t>(i);
        if (!fifo.push(item)) {
          return;
        }
      }
    });
  }
  for (auto& producer : producers) {
    producer.join();
  }
  fifo.close();
  for (auto& consumer : consumers) {
    consumer.join();
  }

  constexpr std::uint64_t kPerProducerSum =
      static_cast<std::uint64_t>(kPerProducer) *
      (static_cast<std::uint64_t>(kPerProducer) - 1U) / 2U;
  EXPECT_FALSE(order_violation.load());
  EXPECT_EQ(received.load(),
            static_cast<std::uint64_t>(kProducers) * kPerProducer);
  EXPECT_EQ(sum.load(),
            static_cast<std::uint64_t>(kProducers) * kPerProducerSum);
  EXPECT_TRUE(fifo.empty());
}

}  // namespace
