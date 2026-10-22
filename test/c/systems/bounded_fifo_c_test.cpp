/**
 * @file bounded_fifo_c_test.cpp
 * @brief Google Test suite for ISO C23 platform abstractions and bounded_fifo
 * component.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include <systems/bounded_fifo.h>
#include <systems/platform.h>
}

#include "systems_test/allocation_counter.hpp"

namespace {

using std::chrono::milliseconds;

// ---------------------------------------------------------------------------
// Platform abstraction tests
// ---------------------------------------------------------------------------

TEST(PlatformCTest, HostDetectionMatchesCompileTimeConstants) {
  const systems_os_family os = systems_get_os_family();
  EXPECT_EQ(os, SYSTEMS_OS_CURRENT);

  const systems_arch_family arch = systems_get_arch_family();
  EXPECT_EQ(arch, SYSTEMS_ARCH_CURRENT);

#if defined(__APPLE__)
  EXPECT_EQ(os, SYSTEMS_OS_MACOS);
  EXPECT_TRUE(systems_is_macos);
  EXPECT_FALSE(systems_is_linux);
  EXPECT_FALSE(systems_is_qnx);
#elif defined(__linux__)
  EXPECT_EQ(os, SYSTEMS_OS_LINUX);
  EXPECT_TRUE(systems_is_linux);
  EXPECT_FALSE(systems_is_macos);
  EXPECT_FALSE(systems_is_qnx);
#elif defined(__QNX__) || defined(__QNXNTO__)
  EXPECT_EQ(os, SYSTEMS_OS_QNX);
  EXPECT_TRUE(systems_is_qnx);
  EXPECT_FALSE(systems_is_macos);
  EXPECT_FALSE(systems_is_linux);
#endif

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
  EXPECT_EQ(arch, SYSTEMS_ARCH_AARCH64);
  EXPECT_TRUE(systems_is_aarch64);
  EXPECT_FALSE(systems_is_x86_64);
#elif defined(__x86_64__) || defined(_M_X64)
  EXPECT_EQ(arch, SYSTEMS_ARCH_X86_64);
  EXPECT_TRUE(systems_is_x86_64);
  EXPECT_FALSE(systems_is_aarch64);
#endif
}

TEST(PlatformCTest, CacheLineAndShmNameConstantsAreValid) {
  EXPECT_GE(SYSTEMS_CACHE_LINE_SIZE, sizeof(void*));
  EXPECT_EQ(systems_cache_line_size, SYSTEMS_CACHE_LINE_SIZE);
  EXPECT_EQ(SYSTEMS_MAX_SHM_NAME_LENGTH, 30U);
  EXPECT_EQ(systems_max_shm_name_length, 30U);

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
  EXPECT_EQ(SYSTEMS_CACHE_LINE_SIZE, 128U);
#else
  EXPECT_EQ(SYSTEMS_CACHE_LINE_SIZE, 64U);
#endif
}

TEST(PlatformCTest, StdbitUtilitiesProduceAccurateBitCounts) {
  EXPECT_EQ(stdc_leading_zeros_ui(0U), 32U);
  EXPECT_EQ(stdc_leading_zeros_ui(1U), 31U);
  EXPECT_EQ(stdc_leading_zeros_ui(0x80000000U), 0U);

  EXPECT_EQ(stdc_trailing_zeros_ui(0U), 32U);
  EXPECT_EQ(stdc_trailing_zeros_ui(1U), 0U);
  EXPECT_EQ(stdc_trailing_zeros_ui(0x80000000U), 31U);

  EXPECT_EQ(stdc_count_ones_ui(0U), 0U);
  EXPECT_EQ(stdc_count_ones_ui(0x0F0F0F0FU), 16U);
  EXPECT_EQ(stdc_count_ones_ui(0xFFFFFFFFU), 32U);

  EXPECT_EQ(stdc_leading_zeros_uc(0U), 8U);
  EXPECT_EQ(stdc_leading_zeros_uc(1U), 7U);
  EXPECT_EQ(stdc_leading_zeros_uc(0x80U), 0U);

  EXPECT_EQ(stdc_trailing_zeros_uc(0U), 8U);
  EXPECT_EQ(stdc_trailing_zeros_uc(1U), 0U);
  EXPECT_EQ(stdc_trailing_zeros_uc(0x80U), 7U);

  EXPECT_EQ(stdc_count_ones_uc(0U), 0U);
  EXPECT_EQ(stdc_count_ones_uc(0xAAU), 4U);
  EXPECT_EQ(stdc_count_ones_uc(0xFFU), 8U);

  EXPECT_EQ(stdc_leading_zeros_ull(0ULL), 64U);
  EXPECT_EQ(stdc_leading_zeros_ull(1ULL), 63U);
  EXPECT_EQ(stdc_leading_zeros_ull(0x8000000000000000ULL), 0U);

  EXPECT_EQ(stdc_trailing_zeros_ull(0ULL), 64U);
  EXPECT_EQ(stdc_trailing_zeros_ull(1ULL), 0U);
  EXPECT_EQ(stdc_trailing_zeros_ull(0x8000000000000000ULL), 63U);

  EXPECT_EQ(stdc_count_ones_ull(0ULL), 0U);
  EXPECT_EQ(stdc_count_ones_ull(0xFFFFFFFFFFFFFFFFULL), 64U);
}

// ---------------------------------------------------------------------------
// Tier 1: Canonical functional tests
// ---------------------------------------------------------------------------

TEST(BoundedFifoCTest, InitValidationRejectsInvalidParameters) {
  struct bounded_fifo fifo{};
  std::array<int, 4> buffer{};

  EXPECT_EQ(bounded_fifo_init(nullptr, buffer.data(), 4, sizeof(int)),
            BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_init(&fifo, nullptr, 4, sizeof(int)),
            BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_init(&fifo, buffer.data(), 0, sizeof(int)),
            BOUNDED_FIFO_ERR_INVALID_ARG);
  EXPECT_EQ(bounded_fifo_init(&fifo, buffer.data(), 4, 0),
            BOUNDED_FIFO_ERR_INVALID_ARG);
  EXPECT_EQ(bounded_fifo_init(&fifo, buffer.data(), SIZE_MAX, 2),
            BOUNDED_FIFO_ERR_INVALID_ARG);

  EXPECT_EQ(bounded_fifo_init(&fifo, buffer.data(), 4, sizeof(int)),
            BOUNDED_FIFO_OK);
  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, StartsEmptyWithConfiguredCapacity) {
  struct bounded_fifo fifo{};
  std::array<int, 4> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 4, sizeof(int)),
            BOUNDED_FIFO_OK);

  EXPECT_TRUE(bounded_fifo_is_empty(&fifo));
  EXPECT_FALSE(bounded_fifo_is_full(&fifo));
  EXPECT_EQ(bounded_fifo_size(&fifo), 0U);
  EXPECT_EQ(bounded_fifo_capacity(&fifo), 4U);
  EXPECT_FALSE(bounded_fifo_is_closed(&fifo));

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, PreservesFifoOrder) {
  struct bounded_fifo fifo{};
  std::array<int, 8> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 8, sizeof(int)),
            BOUNDED_FIFO_OK);

  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(bounded_fifo_try_push(&fifo, &i), BOUNDED_FIFO_OK);
  }
  EXPECT_EQ(bounded_fifo_size(&fifo), 5U);

  for (int i = 0; i < 5; ++i) {
    int val = -1;
    ASSERT_EQ(bounded_fifo_try_pop(&fifo, &val), BOUNDED_FIFO_OK);
    EXPECT_EQ(val, i);
  }
  EXPECT_TRUE(bounded_fifo_is_empty(&fifo));

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, TryPushFailsWhenFull) {
  struct bounded_fifo fifo{};
  std::array<int, 2> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 2, sizeof(int)),
            BOUNDED_FIFO_OK);

  const int val1 = 1;
  const int val2 = 2;
  const int val3 = 3;

  EXPECT_EQ(bounded_fifo_try_push(&fifo, &val1), BOUNDED_FIFO_OK);
  EXPECT_EQ(bounded_fifo_try_push(&fifo, &val2), BOUNDED_FIFO_OK);
  EXPECT_TRUE(bounded_fifo_is_full(&fifo));
  EXPECT_EQ(bounded_fifo_try_push(&fifo, &val3), BOUNDED_FIFO_ERR_FULL);
  EXPECT_EQ(bounded_fifo_size(&fifo), 2U);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, TryPopReturnsEmptyWhenEmpty) {
  struct bounded_fifo fifo{};
  std::array<int, 2> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 2, sizeof(int)),
            BOUNDED_FIFO_OK);

  int val = -1;
  EXPECT_EQ(bounded_fifo_try_pop(&fifo, &val), BOUNDED_FIFO_ERR_EMPTY);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, BlockingPushAndPopWorkWithoutContention) {
  struct bounded_fifo fifo{};
  std::array<int, 2> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 2, sizeof(int)),
            BOUNDED_FIFO_OK);

  const int val1 = 10;
  const int val2 = 20;
  ASSERT_EQ(bounded_fifo_push(&fifo, &val1), BOUNDED_FIFO_OK);
  ASSERT_EQ(bounded_fifo_push(&fifo, &val2), BOUNDED_FIFO_OK);

  int out1 = 0;
  int out2 = 0;
  ASSERT_EQ(bounded_fifo_pop(&fifo, &out1), BOUNDED_FIFO_OK);
  ASSERT_EQ(bounded_fifo_pop(&fifo, &out2), BOUNDED_FIFO_OK);

  EXPECT_EQ(out1, 10);
  EXPECT_EQ(out2, 20);

  bounded_fifo_destroy(&fifo);
}

// ---------------------------------------------------------------------------
// Tier 2: Boundary, corner & edge cases
// ---------------------------------------------------------------------------

TEST(BoundedFifoCTest, CapacityOneAlternatesPushPop) {
  struct bounded_fifo fifo{};
  std::array<int, 1> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 1, sizeof(int)),
            BOUNDED_FIFO_OK);

  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(bounded_fifo_try_push(&fifo, &i), BOUNDED_FIFO_OK);
    const int err = -1;
    EXPECT_EQ(bounded_fifo_try_push(&fifo, &err), BOUNDED_FIFO_ERR_FULL);

    int val = -1;
    EXPECT_EQ(bounded_fifo_try_pop(&fifo, &val), BOUNDED_FIFO_OK);
    EXPECT_EQ(val, i);
    EXPECT_EQ(bounded_fifo_try_pop(&fifo, &val), BOUNDED_FIFO_ERR_EMPTY);
  }

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, WrapsAroundTheRingBuffer) {
  struct bounded_fifo fifo{};
  std::array<int, 3> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 3, sizeof(int)),
            BOUNDED_FIFO_OK);

  int next_in = 0;
  int next_out = 0;

  for (int round = 0; round < 20; ++round) {
    while (bounded_fifo_try_push(&fifo, &next_in) == BOUNDED_FIFO_OK) {
      ++next_in;
    }
    // Drain two of three so head advances across the wrap point every round.
    for (int i = 0; i < 2; ++i) {
      int val = -1;
      ASSERT_EQ(bounded_fifo_try_pop(&fifo, &val), BOUNDED_FIFO_OK);
      EXPECT_EQ(val, next_out++);
    }
  }

  int val = -1;
  while (bounded_fifo_try_pop(&fifo, &val) == BOUNDED_FIFO_OK) {
    EXPECT_EQ(val, next_out++);
  }
  EXPECT_EQ(next_in, next_out);

  bounded_fifo_destroy(&fifo);
}

struct TestPayload {
  uint64_t seq;
  double timestamp;
  std::array<char, 16> label;
};

TEST(BoundedFifoCTest, SupportsCustomStructPayloads) {
  struct bounded_fifo fifo{};
  std::array<TestPayload, 4> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 4, sizeof(TestPayload)),
            BOUNDED_FIFO_OK);

  TestPayload in1 = {.seq = 1ULL, .timestamp = 123.456, .label = {}};
  std::strncpy(in1.label.data(), "payload-1", in1.label.size() - 1);
  TestPayload in2 = {.seq = 2ULL, .timestamp = 789.012, .label = {}};
  std::strncpy(in2.label.data(), "payload-2", in2.label.size() - 1);

  ASSERT_EQ(bounded_fifo_try_push(&fifo, &in1), BOUNDED_FIFO_OK);
  ASSERT_EQ(bounded_fifo_try_push(&fifo, &in2), BOUNDED_FIFO_OK);

  TestPayload out = {};
  ASSERT_EQ(bounded_fifo_try_pop(&fifo, &out), BOUNDED_FIFO_OK);
  EXPECT_EQ(out.seq, 1ULL);
  EXPECT_DOUBLE_EQ(out.timestamp, 123.456);
  EXPECT_STREQ(out.label.data(), "payload-1");

  ASSERT_EQ(bounded_fifo_try_pop(&fifo, &out), BOUNDED_FIFO_OK);
  EXPECT_EQ(out.seq, 2ULL);
  EXPECT_DOUBLE_EQ(out.timestamp, 789.012);
  EXPECT_STREQ(out.label.data(), "payload-2");

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, NullPointersRejectedAcrossAllOperations) {
  struct bounded_fifo fifo{};
  std::array<int, 2> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 2, sizeof(int)),
            BOUNDED_FIFO_OK);

  const int val = 42;
  int out = 0;

  EXPECT_EQ(bounded_fifo_try_push(nullptr, &val), BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_try_push(&fifo, nullptr), BOUNDED_FIFO_ERR_NULL_PTR);

  EXPECT_EQ(bounded_fifo_push(nullptr, &val), BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_push(&fifo, nullptr), BOUNDED_FIFO_ERR_NULL_PTR);

  EXPECT_EQ(bounded_fifo_push_for(nullptr, &val, 1000),
            BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_push_for(&fifo, nullptr, 1000),
            BOUNDED_FIFO_ERR_NULL_PTR);

  EXPECT_EQ(bounded_fifo_try_pop(nullptr, &out), BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_try_pop(&fifo, nullptr), BOUNDED_FIFO_ERR_NULL_PTR);

  EXPECT_EQ(bounded_fifo_pop(nullptr, &out), BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_pop(&fifo, nullptr), BOUNDED_FIFO_ERR_NULL_PTR);

  EXPECT_EQ(bounded_fifo_pop_for(nullptr, &out, 1000),
            BOUNDED_FIFO_ERR_NULL_PTR);
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, nullptr, 1000),
            BOUNDED_FIFO_ERR_NULL_PTR);

  bounded_fifo_close(nullptr);
  bounded_fifo_destroy(nullptr);
  EXPECT_FALSE(bounded_fifo_is_closed(nullptr));
  EXPECT_EQ(bounded_fifo_size(nullptr), 0U);
  EXPECT_EQ(bounded_fifo_capacity(nullptr), 0U);
  EXPECT_TRUE(bounded_fifo_is_empty(nullptr));
  EXPECT_FALSE(bounded_fifo_is_full(nullptr));

  bounded_fifo_destroy(&fifo);
}

// ---------------------------------------------------------------------------
// Tier 3: Resource allocation tests (zero steady-state heap)
// ---------------------------------------------------------------------------

TEST(BoundedFifoCTest, SteadyStatePushPopProducesZeroHeapAllocations) {
  struct bounded_fifo fifo{};
  std::array<int, 16> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 16, sizeof(int)),
            BOUNDED_FIFO_OK);

  std::size_t allocations = 0;
  {
    const systems_test::AllocationScope scope;
    for (int i = 0; i < 100; ++i) {
      const int in = i;
      EXPECT_EQ(bounded_fifo_try_push(&fifo, &in), BOUNDED_FIFO_OK);
      int out = 0;
      EXPECT_EQ(bounded_fifo_try_pop(&fifo, &out), BOUNDED_FIFO_OK);
      EXPECT_EQ(out, in);
    }
    allocations = scope.allocations();
  }
  EXPECT_EQ(allocations, 0U);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, StaticStorageLifecycleProducesZeroHeapAllocations) {
  static struct bounded_fifo static_fifo;
  static std::array<int, 8> static_buffer{};

  std::size_t allocations = 0;
  {
    const systems_test::AllocationScope scope;
    EXPECT_EQ(
        bounded_fifo_init(&static_fifo, static_buffer.data(), 8, sizeof(int)),
        BOUNDED_FIFO_OK);
    const int val = 999;
    EXPECT_EQ(bounded_fifo_push(&static_fifo, &val), BOUNDED_FIFO_OK);
    int popped = 0;
    EXPECT_EQ(bounded_fifo_pop(&static_fifo, &popped), BOUNDED_FIFO_OK);
    EXPECT_EQ(popped, 999);
    bounded_fifo_close(&static_fifo);
    bounded_fifo_destroy(&static_fifo);
    allocations = scope.allocations();
  }
  EXPECT_EQ(allocations, 0U);
}

// ---------------------------------------------------------------------------
// Tier 4: Timed operations and close semantics
// ---------------------------------------------------------------------------

TEST(BoundedFifoCTest, TimedOperationsTimeOutOnFullAndEmpty) {
  struct bounded_fifo fifo{};
  std::array<int, 1> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 1, sizeof(int)),
            BOUNDED_FIFO_OK);

  int val = 0;
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, 10'000'000LL),
            BOUNDED_FIFO_ERR_TIMED_OUT);

  const int in1 = 1;
  ASSERT_EQ(bounded_fifo_try_push(&fifo, &in1), BOUNDED_FIFO_OK);

  const int in2 = 2;
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in2, 10'000'000LL),
            BOUNDED_FIFO_ERR_TIMED_OUT);

  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, 10'000'000LL), BOUNDED_FIFO_OK);
  EXPECT_EQ(val, 1);

  const int in3 = 3;
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in3, 10'000'000LL), BOUNDED_FIFO_OK);
  EXPECT_EQ(bounded_fifo_size(&fifo), 1U);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, ZeroAndNegativeTimeoutsHandledCorrectly) {
  struct bounded_fifo fifo{};
  std::array<int, 2> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 2, sizeof(int)),
            BOUNDED_FIFO_OK);

  int val = -1;
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, 0), BOUNDED_FIFO_ERR_TIMED_OUT);
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, -1LL),
            BOUNDED_FIFO_ERR_TIMED_OUT);
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, -1'000'000LL),
            BOUNDED_FIFO_ERR_TIMED_OUT);

  const int in1 = 100;
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in1, -1LL), BOUNDED_FIFO_OK);
  const int in2 = 200;
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in2, 0), BOUNDED_FIFO_OK);

  const int in3 = 300;
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in3, 0), BOUNDED_FIFO_ERR_TIMED_OUT);
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in3, -1LL),
            BOUNDED_FIFO_ERR_TIMED_OUT);

  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, -1LL), BOUNDED_FIFO_OK);
  EXPECT_EQ(val, 100);
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &val, 0), BOUNDED_FIFO_OK);
  EXPECT_EQ(val, 200);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, ClosedQueueRejectsPushesButDrainsExistingElements) {
  struct bounded_fifo fifo{};
  std::array<int, 4> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 4, sizeof(int)),
            BOUNDED_FIFO_OK);

  const int in1 = 1;
  const int in2 = 2;
  ASSERT_EQ(bounded_fifo_try_push(&fifo, &in1), BOUNDED_FIFO_OK);
  ASSERT_EQ(bounded_fifo_try_push(&fifo, &in2), BOUNDED_FIFO_OK);

  bounded_fifo_close(&fifo);
  EXPECT_TRUE(bounded_fifo_is_closed(&fifo));

  const int in3 = 3;
  EXPECT_EQ(bounded_fifo_try_push(&fifo, &in3), BOUNDED_FIFO_ERR_CLOSED);
  EXPECT_EQ(bounded_fifo_push(&fifo, &in3), BOUNDED_FIFO_ERR_CLOSED);
  EXPECT_EQ(bounded_fifo_push_for(&fifo, &in3, 1'000'000LL),
            BOUNDED_FIFO_ERR_CLOSED);

  int out = 0;
  EXPECT_EQ(bounded_fifo_pop(&fifo, &out), BOUNDED_FIFO_OK);
  EXPECT_EQ(out, 1);
  EXPECT_EQ(bounded_fifo_pop(&fifo, &out), BOUNDED_FIFO_OK);
  EXPECT_EQ(out, 2);

  EXPECT_EQ(bounded_fifo_pop(&fifo, &out), BOUNDED_FIFO_ERR_CLOSED);
  EXPECT_EQ(bounded_fifo_try_pop(&fifo, &out), BOUNDED_FIFO_ERR_CLOSED);
  EXPECT_EQ(bounded_fifo_pop_for(&fifo, &out, 1'000'000LL),
            BOUNDED_FIFO_ERR_CLOSED);

  bounded_fifo_destroy(&fifo);
}

// ---------------------------------------------------------------------------
// Tier 5: Concurrency, shutdown, and multi-thread stress
// ---------------------------------------------------------------------------

TEST(BoundedFifoCTest, BlockedPopIsWokenByPush) {
  struct bounded_fifo fifo{};
  std::array<int, 2> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 2, sizeof(int)),
            BOUNDED_FIFO_OK);

  int received = -1;
  std::thread consumer(
      [&] { EXPECT_EQ(bounded_fifo_pop(&fifo, &received), BOUNDED_FIFO_OK); });

  std::this_thread::sleep_for(milliseconds{20});
  const int val = 42;
  ASSERT_EQ(bounded_fifo_try_push(&fifo, &val), BOUNDED_FIFO_OK);

  consumer.join();
  EXPECT_EQ(received, 42);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, BlockedPushIsWokenByPop) {
  struct bounded_fifo fifo{};
  std::array<int, 1> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 1, sizeof(int)),
            BOUNDED_FIFO_OK);

  const int val1 = 1;
  ASSERT_EQ(bounded_fifo_try_push(&fifo, &val1), BOUNDED_FIFO_OK);

  std::atomic<bool> pushed{false};
  std::thread producer([&] {
    const int val2 = 2;
    EXPECT_EQ(bounded_fifo_push(&fifo, &val2), BOUNDED_FIFO_OK);
    pushed = true;
  });

  std::this_thread::sleep_for(milliseconds{20});
  EXPECT_FALSE(pushed.load());

  int popped1 = 0;
  EXPECT_EQ(bounded_fifo_pop(&fifo, &popped1), BOUNDED_FIFO_OK);
  EXPECT_EQ(popped1, 1);

  producer.join();
  EXPECT_TRUE(pushed.load());

  int popped2 = 0;
  EXPECT_EQ(bounded_fifo_try_pop(&fifo, &popped2), BOUNDED_FIFO_OK);
  EXPECT_EQ(popped2, 2);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoCTest, CloseWakesBlockedConsumersAndProducers) {
  struct bounded_fifo empty_fifo{};
  std::array<int, 1> empty_buf{};
  ASSERT_EQ(bounded_fifo_init(&empty_fifo, empty_buf.data(), 1, sizeof(int)),
            BOUNDED_FIFO_OK);

  struct bounded_fifo full_fifo{};
  std::array<int, 1> full_buf{};
  ASSERT_EQ(bounded_fifo_init(&full_fifo, full_buf.data(), 1, sizeof(int)),
            BOUNDED_FIFO_OK);

  const int full_val = 1;
  ASSERT_EQ(bounded_fifo_try_push(&full_fifo, &full_val), BOUNDED_FIFO_OK);

  std::atomic<int> consumer_status{-1};
  std::atomic<int> producer_status{-1};

  std::thread consumer([&] {
    int out = 0;
    consumer_status = bounded_fifo_pop(&empty_fifo, &out);
  });

  std::thread producer([&] {
    const int in = 2;
    producer_status = bounded_fifo_push(&full_fifo, &in);
  });

  std::this_thread::sleep_for(milliseconds{20});
  bounded_fifo_close(&empty_fifo);
  bounded_fifo_close(&full_fifo);

  consumer.join();
  producer.join();

  EXPECT_EQ(consumer_status.load(), BOUNDED_FIFO_ERR_CLOSED);
  EXPECT_EQ(producer_status.load(), BOUNDED_FIFO_ERR_CLOSED);

  bounded_fifo_destroy(&empty_fifo);
  bounded_fifo_destroy(&full_fifo);
}

TEST(BoundedFifoStressCTest, SingleProducerSingleConsumerKeepsOrder) {
  constexpr int kCount = 50000;
  struct bounded_fifo fifo{};
  std::array<int, 64> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 64, sizeof(int)),
            BOUNDED_FIFO_OK);

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      ASSERT_EQ(bounded_fifo_push(&fifo, &i), BOUNDED_FIFO_OK);
    }
    bounded_fifo_close(&fifo);
  });

  int expected = 0;
  int val = -1;
  while (bounded_fifo_pop(&fifo, &val) == BOUNDED_FIFO_OK) {
    ASSERT_EQ(val, expected);
    ++expected;
  }
  producer.join();
  EXPECT_EQ(expected, kCount);

  bounded_fifo_destroy(&fifo);
}

TEST(BoundedFifoStressCTest, MultiProducerMultiConsumerDeliversEverythingOnce) {
  constexpr int kProducers = 4;
  constexpr int kConsumers = 4;
  constexpr int kPerProducer = 20000;
  struct bounded_fifo fifo{};
  std::array<uint64_t, 32> buffer{};
  ASSERT_EQ(bounded_fifo_init(&fifo, buffer.data(), 32, sizeof(uint64_t)),
            BOUNDED_FIFO_OK);

  std::atomic<uint64_t> sum{0};
  std::atomic<uint64_t> received{0};
  std::atomic<bool> order_violation{false};

  std::vector<std::thread> consumers;
  consumers.reserve(kConsumers);
  for (int c = 0; c < kConsumers; ++c) {
    consumers.emplace_back([&] {
      std::vector<int64_t> last_seen(kProducers, -1);
      uint64_t val = 0;
      while (bounded_fifo_pop(&fifo, &val) == BOUNDED_FIFO_OK) {
        const auto producer_id = static_cast<size_t>(val >> 32U);
        const auto sequence = static_cast<int64_t>(val & 0xFFFFFFFFU);
        if (sequence <= last_seen[producer_id]) {
          order_violation = true;
        }
        last_seen[producer_id] = sequence;
        sum += val & 0xFFFFFFFFU;
        ++received;
      }
    });
  }

  std::vector<std::thread> producers;
  producers.reserve(kProducers);
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&fifo, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        const uint64_t item =
            (static_cast<uint64_t>(p) << 32U) | static_cast<uint64_t>(i);
        if (bounded_fifo_push(&fifo, &item) != BOUNDED_FIFO_OK) {
          return;
        }
      }
    });
  }

  for (auto& producer : producers) {
    producer.join();
  }
  bounded_fifo_close(&fifo);
  for (auto& consumer : consumers) {
    consumer.join();
  }

  constexpr uint64_t kPerProducerSum =
      static_cast<uint64_t>(kPerProducer) *
      (static_cast<uint64_t>(kPerProducer) - 1U) / 2U;

  EXPECT_FALSE(order_violation.load());
  EXPECT_EQ(received.load(), static_cast<uint64_t>(kProducers) * kPerProducer);
  EXPECT_EQ(sum.load(), static_cast<uint64_t>(kProducers) * kPerProducerSum);
  EXPECT_TRUE(bounded_fifo_is_empty(&fifo));

  bounded_fifo_destroy(&fifo);
}

}  // namespace
