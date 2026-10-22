/**
 * @file process_rendezvous_c_test.cpp
 * @brief Google Test suite for ISO C23 process_rendezvous component.
 * Traceability: PRV-REQ-001 through PRV-REQ-010.
 */

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <latch>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <systems/platform.h>
#include <systems/process_rendezvous.h>
}

#include "systems_test/allocation_counter.hpp"
#include "systems_test/process.hpp"
#include "systems_test/shared_region.hpp"
#include "systems_test/shm_name.hpp"

namespace {

class ScopedRendezvous {
 public:
  ScopedRendezvous() = default;
  ~ScopedRendezvous() { process_rendezvous_destroy(&rdv_); }
  ScopedRendezvous(const ScopedRendezvous&) = delete;
  auto operator=(const ScopedRendezvous&) -> ScopedRendezvous& = delete;
  ScopedRendezvous(ScopedRendezvous&& other) noexcept {
    rdv_ = other.rdv_;
    other.rdv_ = {};
  }
  auto operator=(ScopedRendezvous&& other) noexcept -> ScopedRendezvous& {
    if (this != &other) {
      process_rendezvous_destroy(&rdv_);
      rdv_ = other.rdv_;
      other.rdv_ = {};
    }
    return *this;
  }

  [[nodiscard]] auto get() noexcept -> struct process_rendezvous* {
    return &rdv_;
  }
  [[nodiscard]] auto get() const noexcept -> const struct process_rendezvous* {
    return &rdv_;
  }
  [[nodiscard]] auto operator->() noexcept -> struct process_rendezvous* {
    return &rdv_;
  }
  [[nodiscard]] auto operator->() const noexcept -> const
      struct process_rendezvous* {
    return &rdv_;
  }

 private:
  struct process_rendezvous rdv_{};
};

class TestRendezvousName {
 public:
  TestRendezvousName()
      : name_{systems_test::unique_shm_name("prvc")}, guard_{name_} {}
  ~TestRendezvousName() {
    static_cast<void>(process_rendezvous_unlink(name_.c_str()));
  }
  TestRendezvousName(const TestRendezvousName&) = delete;
  auto operator=(const TestRendezvousName&) -> TestRendezvousName& = delete;
  TestRendezvousName(TestRendezvousName&&) = delete;
  auto operator=(TestRendezvousName&&) -> TestRendezvousName& = delete;

  [[nodiscard]] auto c_str() const -> const char* { return name_.c_str(); }
  [[nodiscard]] auto str() const -> const std::string& { return name_; }

 private:
  std::string name_;
  systems_test::ScopedShmUnlink guard_;
};

// ============================================================================
// Tier 1: Functional Initialization, Open, Attach, Name Validation, Unlink
// ============================================================================

TEST(ProcessRendezvousCTest, CreateOpenAndAttachInitializeUnsetStages) {
  RecordProperty("requirements", "PRV-REQ-001");
  const TestRendezvousName name;
  ScopedRendezvous created;
  ASSERT_EQ(process_rendezvous_create(created.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_FALSE(process_rendezvous_producer_is_ready(created.get()));
  EXPECT_FALSE(process_rendezvous_consumer_is_ready(created.get()));
  EXPECT_FALSE(process_rendezvous_is_closed(created.get()));
  EXPECT_TRUE(created->is_creator);

  ScopedRendezvous duplicate;
  EXPECT_EQ(process_rendezvous_create(duplicate.get(), name.c_str()),
            PROCESS_RENDEZVOUS_ERR_ALREADY_EXISTS);

  ScopedRendezvous opened;
  ASSERT_EQ(process_rendezvous_open(opened.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_FALSE(process_rendezvous_producer_is_ready(opened.get()));
  EXPECT_FALSE(process_rendezvous_consumer_is_ready(opened.get()));
  EXPECT_FALSE(opened->is_creator);

  ScopedRendezvous attached;
  ASSERT_EQ(
      process_rendezvous_attach(attached.get(), name.c_str(), 100'000'000LL),
      PROCESS_RENDEZVOUS_OK);
  EXPECT_FALSE(process_rendezvous_producer_is_ready(attached.get()));

  const TestRendezvousName fresh_name;
  ScopedRendezvous attached_fresh;
  ASSERT_EQ(process_rendezvous_attach(attached_fresh.get(), fresh_name.c_str(),
                                      100'000'000LL),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_FALSE(process_rendezvous_producer_is_ready(attached_fresh.get()));
  EXPECT_TRUE(attached_fresh->is_creator);
}

TEST(ProcessRendezvousCTest, OpenRejectsMissingAndIncompatibleSegments) {
  RecordProperty("requirements", "PRV-REQ-001");
  const TestRendezvousName missing;
  ScopedRendezvous not_found;
  EXPECT_EQ(process_rendezvous_open(not_found.get(), missing.c_str()),
            PROCESS_RENDEZVOUS_ERR_NOT_FOUND);

  const TestRendezvousName bad_seg;
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic.
  const int fd = ::shm_open(bad_seg.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  // NOLINTEND(*-vararg)
  ASSERT_GE(fd, 0);
  EXPECT_EQ(::ftruncate(fd, 64), 0);
  static_cast<void>(::close(fd));

  ScopedRendezvous mismatch;
  EXPECT_EQ(process_rendezvous_open(mismatch.get(), bad_seg.c_str()),
            PROCESS_RENDEZVOUS_ERR_LAYOUT_MISMATCH);

  ScopedRendezvous attach_timeout;
  EXPECT_EQ(process_rendezvous_attach(attach_timeout.get(), bad_seg.c_str(),
                                      5'000'000LL),
            PROCESS_RENDEZVOUS_ERR_TIMED_OUT);
}

TEST(ProcessRendezvousCTest, RejectsInvalidAndOverlongNames) {
  RecordProperty("requirements", "PRV-REQ-002");
  for (const char* bad : {"", "noslash", "/", "/a/b"}) {
    ScopedRendezvous rdv;
    EXPECT_EQ(process_rendezvous_create(rdv.get(), bad),
              PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(process_rendezvous_open(rdv.get(), bad),
              PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(process_rendezvous_attach(rdv.get(), bad, 1'000'000LL),
              PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(process_rendezvous_unlink(bad),
              PROCESS_RENDEZVOUS_ERR_INVALID_ARGUMENT);
  }

  const std::string too_long =
      "/" + std::string(SYSTEMS_MAX_SHM_NAME_LENGTH, 'r');
  ScopedRendezvous rdv;
  EXPECT_EQ(process_rendezvous_create(rdv.get(), too_long.c_str()),
            PROCESS_RENDEZVOUS_ERR_NAME_TOO_LONG);
}

TEST(ProcessRendezvousCTest, UnlinkRemovesNameWhileExistingHandlesKeepWorking) {
  RecordProperty("requirements", "PRV-REQ-003");
  const TestRendezvousName name;
  ScopedRendezvous created;
  ASSERT_EQ(process_rendezvous_create(created.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
  ScopedRendezvous opened;
  ASSERT_EQ(process_rendezvous_open(opened.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  ASSERT_EQ(process_rendezvous_unlink(name.c_str()), PROCESS_RENDEZVOUS_OK);

  ScopedRendezvous reopen;
  EXPECT_EQ(process_rendezvous_open(reopen.get(), name.c_str()),
            PROCESS_RENDEZVOUS_ERR_NOT_FOUND);

  EXPECT_EQ(process_rendezvous_unlink(name.c_str()),
            PROCESS_RENDEZVOUS_ERR_NOT_FOUND);

  ASSERT_EQ(process_rendezvous_signal_producer_ready(created.get()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_TRUE(process_rendezvous_producer_is_ready(opened.get()));
}

// ============================================================================
// Tier 2: Producer and Consumer Stages Signaling and Waiting
// ============================================================================

TEST(ProcessRendezvousCTest, ProducerAndConsumerStagesSignalAndWait) {
  RecordProperty("requirements", "PRV-REQ-004,PRV-REQ-005");
  const TestRendezvousName name;
  ScopedRendezvous producer;
  ASSERT_EQ(process_rendezvous_create(producer.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
  ScopedRendezvous consumer;
  ASSERT_EQ(process_rendezvous_open(consumer.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  EXPECT_EQ(process_rendezvous_try_wait_producer_ready(consumer.get()),
            PROCESS_RENDEZVOUS_ERR_WOULD_BLOCK);
  EXPECT_EQ(
      process_rendezvous_wait_producer_ready_for(consumer.get(), 3'000'000LL),
      PROCESS_RENDEZVOUS_ERR_TIMED_OUT);

  ASSERT_EQ(process_rendezvous_signal_producer_ready(producer.get()),
            PROCESS_RENDEZVOUS_OK);
  ASSERT_EQ(process_rendezvous_signal_producer_ready(producer.get()),
            PROCESS_RENDEZVOUS_OK);  // idempotent
  EXPECT_TRUE(process_rendezvous_producer_is_ready(consumer.get()));
  EXPECT_EQ(process_rendezvous_try_wait_producer_ready(consumer.get()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_EQ(
      process_rendezvous_wait_producer_ready_for(consumer.get(), 1'000'000LL),
      PROCESS_RENDEZVOUS_OK);
  EXPECT_EQ(process_rendezvous_wait_producer_ready(consumer.get()),
            PROCESS_RENDEZVOUS_OK);

  EXPECT_EQ(process_rendezvous_try_wait_consumer_ready(producer.get()),
            PROCESS_RENDEZVOUS_ERR_WOULD_BLOCK);
  EXPECT_EQ(
      process_rendezvous_wait_consumer_ready_for(producer.get(), 3'000'000LL),
      PROCESS_RENDEZVOUS_ERR_TIMED_OUT);

  ASSERT_EQ(process_rendezvous_signal_consumer_ready(consumer.get()),
            PROCESS_RENDEZVOUS_OK);
  ASSERT_EQ(process_rendezvous_signal_consumer_ready(consumer.get()),
            PROCESS_RENDEZVOUS_OK);  // idempotent
  EXPECT_TRUE(process_rendezvous_consumer_is_ready(producer.get()));
  EXPECT_EQ(process_rendezvous_try_wait_consumer_ready(producer.get()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_EQ(
      process_rendezvous_wait_consumer_ready_for(producer.get(), 1'000'000LL),
      PROCESS_RENDEZVOUS_OK);
  EXPECT_EQ(process_rendezvous_wait_consumer_ready(producer.get()),
            PROCESS_RENDEZVOUS_OK);
}

TEST(ProcessRendezvousCTest, CloseWakesUnsignaledWaitersAndPreservesSignaled) {
  RecordProperty("requirements", "PRV-REQ-006");
  const TestRendezvousName name;
  ScopedRendezvous first;
  ASSERT_EQ(process_rendezvous_create(first.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
  ScopedRendezvous second;
  ASSERT_EQ(process_rendezvous_open(second.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  ASSERT_EQ(process_rendezvous_signal_producer_ready(first.get()),
            PROCESS_RENDEZVOUS_OK);
  process_rendezvous_close(first.get());
  EXPECT_TRUE(process_rendezvous_is_closed(first.get()));
  EXPECT_TRUE(process_rendezvous_is_closed(second.get()));

  // Already-signaled stage stays ready after close
  EXPECT_EQ(process_rendezvous_try_wait_producer_ready(second.get()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_EQ(process_rendezvous_wait_producer_ready(second.get()),
            PROCESS_RENDEZVOUS_OK);

  // Unsignaled stage reports CLOSED
  EXPECT_EQ(process_rendezvous_try_wait_consumer_ready(second.get()),
            PROCESS_RENDEZVOUS_ERR_CLOSED);
  EXPECT_EQ(
      process_rendezvous_wait_consumer_ready_for(second.get(), 50'000'000LL),
      PROCESS_RENDEZVOUS_ERR_CLOSED);

  // Signaling after close fails with CLOSED
  EXPECT_EQ(process_rendezvous_signal_producer_ready(first.get()),
            PROCESS_RENDEZVOUS_ERR_CLOSED);
  EXPECT_EQ(process_rendezvous_signal_consumer_ready(second.get()),
            PROCESS_RENDEZVOUS_ERR_CLOSED);
}

// ============================================================================
// Tier 3: Zero Steady-State Heap Allocation (PRV-REQ-008) & Lifetime
// (PRV-REQ-009)
// ============================================================================

TEST(ProcessRendezvousCTest, NoHeapAllocationAfterInit) {
  RecordProperty("requirements", "PRV-REQ-008");
  const TestRendezvousName name;
  ScopedRendezvous pub;
  ASSERT_EQ(process_rendezvous_create(pub.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
  ScopedRendezvous sub;
  ASSERT_EQ(process_rendezvous_open(sub.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  std::size_t allocations = 0;
  bool ok = false;
  {
    const systems_test::AllocationScope scope;
    const auto s1 = process_rendezvous_signal_producer_ready(pub.get());
    const auto w1 =
        process_rendezvous_wait_producer_ready_for(sub.get(), 10'000'000LL);
    const auto s2 = process_rendezvous_signal_consumer_ready(sub.get());
    const auto w2 =
        process_rendezvous_wait_consumer_ready_for(pub.get(), 10'000'000LL);
    process_rendezvous_close(pub.get());
    ok = (s1 == PROCESS_RENDEZVOUS_OK) && (w1 == PROCESS_RENDEZVOUS_OK) &&
         (s2 == PROCESS_RENDEZVOUS_OK) && (w2 == PROCESS_RENDEZVOUS_OK) &&
         process_rendezvous_is_closed(sub.get());
    allocations = scope.allocations();
  }
  EXPECT_TRUE(ok);
  EXPECT_EQ(allocations, 0U);
}

TEST(ProcessRendezvousCTest, HandleLifetimeAndErrorStrings) {
  RecordProperty("requirements", "PRV-REQ-009");
  const TestRendezvousName name;
  ScopedRendezvous rdv;
  ASSERT_EQ(process_rendezvous_create(rdv.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  // Verify error string mappings are non-empty
  for (int32_t code = PROCESS_RENDEZVOUS_OK; code <= PROCESS_RENDEZVOUS_ERR_OS;
       ++code) {
    const char* str = process_rendezvous_error_to_string(
        static_cast<enum process_rendezvous_error>(code));
    EXPECT_NE(str, nullptr);
    EXPECT_GT(std::strlen(str), 0U);
  }

  // Destroying handle unmaps memory without unlinking
  process_rendezvous_destroy(rdv.get());
  ScopedRendezvous reopen;
  EXPECT_EQ(process_rendezvous_open(reopen.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);
}

// ============================================================================
// Tier 4: Multi-Thread Concurrency (PRV-REQ-007)
// ============================================================================

TEST(ProcessRendezvousCTest, MultipleThreadsWaitAndWakeTogether) {
  RecordProperty("requirements", "PRV-REQ-004,PRV-REQ-005,PRV-REQ-007");
  const TestRendezvousName name;
  ScopedRendezvous coord;
  ASSERT_EQ(process_rendezvous_create(coord.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  constexpr int kWaiters = 6;
  std::latch start_gate{kWaiters + 1};
  std::atomic<int> woke_producer{0};
  std::atomic<int> woke_consumer{0};

  {
    std::vector<std::jthread> threads;
    threads.reserve(kWaiters);
    for (int i = 0; i < kWaiters; ++i) {
      threads.emplace_back([&] {
        ScopedRendezvous local;
        if (process_rendezvous_open(local.get(), name.c_str()) !=
            PROCESS_RENDEZVOUS_OK) {
          return;
        }
        start_gate.arrive_and_wait();
        if (process_rendezvous_wait_producer_ready_for(
                local.get(), 5'000'000'000LL) == PROCESS_RENDEZVOUS_OK) {
          woke_producer.fetch_add(1, std::memory_order_relaxed);
        }
        if (process_rendezvous_wait_consumer_ready_for(
                local.get(), 5'000'000'000LL) == PROCESS_RENDEZVOUS_OK) {
          woke_consumer.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    start_gate.arrive_and_wait();
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_EQ(process_rendezvous_signal_producer_ready(coord.get()),
              PROCESS_RENDEZVOUS_OK);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_EQ(process_rendezvous_signal_consumer_ready(coord.get()),
              PROCESS_RENDEZVOUS_OK);
  }

  EXPECT_EQ(woke_producer.load(), kWaiters);
  EXPECT_EQ(woke_consumer.load(), kWaiters);
}

// ============================================================================
// Tier 5: Multi-Process Handshake (PRV-REQ-001, PRV-REQ-007)
// ============================================================================

TEST(ProcessRendezvousCTest, TwoProcessHandshakeOrdersStartup) {
  RecordProperty("requirements",
                 "PRV-REQ-001,PRV-REQ-004,PRV-REQ-005,PRV-REQ-007");
  const TestRendezvousName name;

  struct SharedOrder {
    std::atomic<std::uint32_t> step{0};
    std::atomic<std::uint32_t> producer_step{0};
    std::atomic<std::uint32_t> consumer_step{0};
  };
  systems_test::SharedRegion<SharedOrder> order;
  ASSERT_TRUE(order.valid());

  auto consumer_child = systems_test::ChildProcess::spawn([&] -> int {
    struct process_rendezvous rdv{};
    if (process_rendezvous_attach(&rdv, name.c_str(), 5'000'000'000LL) !=
        PROCESS_RENDEZVOUS_OK) {
      return 1;
    }
    if (process_rendezvous_wait_producer_ready_for(&rdv, 5'000'000'000LL) !=
        PROCESS_RENDEZVOUS_OK) {
      process_rendezvous_destroy(&rdv);
      return 2;
    }
    order->consumer_step.store(
        order->step.fetch_add(1, std::memory_order_acq_rel) + 1U,
        std::memory_order_release);
    if (process_rendezvous_signal_consumer_ready(&rdv) !=
        PROCESS_RENDEZVOUS_OK) {
      process_rendezvous_destroy(&rdv);
      return 3;
    }
    process_rendezvous_destroy(&rdv);
    return systems_test::kChildOk;
  });

  ScopedRendezvous producer_rdv;
  ASSERT_EQ(process_rendezvous_attach(producer_rdv.get(), name.c_str(),
                                      5'000'000'000LL),
            PROCESS_RENDEZVOUS_OK);
  order->producer_step.store(
      order->step.fetch_add(1, std::memory_order_acq_rel) + 1U,
      std::memory_order_release);
  ASSERT_EQ(process_rendezvous_signal_producer_ready(producer_rdv.get()),
            PROCESS_RENDEZVOUS_OK);
  ASSERT_EQ(process_rendezvous_wait_consumer_ready_for(producer_rdv.get(),
                                                       5'000'000'000LL),
            PROCESS_RENDEZVOUS_OK);

  const auto child_result = consumer_child.wait_for(std::chrono::seconds{10});
  EXPECT_TRUE(child_result.succeeded()) << child_result;
  EXPECT_EQ(order->producer_step.load(), 1U);
  EXPECT_EQ(order->consumer_step.load(), 2U);
}

// ============================================================================
// Tier 6: Fault Injection / SIGKILL Crash Resilience (PRV-REQ-006, PRV-REQ-010)
// ============================================================================

TEST(ProcessRendezvousCTest, SurvivesParticipantKilledWithSigkill) {
  RecordProperty("requirements", "PRV-REQ-006,PRV-REQ-010");
  const TestRendezvousName name;
  ScopedRendezvous parent;
  ASSERT_EQ(process_rendezvous_create(parent.get(), name.c_str()),
            PROCESS_RENDEZVOUS_OK);

  systems_test::SharedRegion<std::atomic<std::uint32_t>> child_attached;
  ASSERT_TRUE(child_attached.valid());

  auto victim = systems_test::ChildProcess::spawn([&] -> int {
    struct process_rendezvous rdv{};
    if (process_rendezvous_open(&rdv, name.c_str()) != PROCESS_RENDEZVOUS_OK) {
      return 1;
    }
    child_attached->store(1, std::memory_order_release);
    static_cast<void>(
        process_rendezvous_wait_producer_ready_for(&rdv, 30'000'000'000LL));
    process_rendezvous_destroy(&rdv);
    return 0;
  });

  ASSERT_TRUE(systems_test::wait_until(
      [&] { return child_attached->load(std::memory_order_acquire) == 1U; },
      std::chrono::seconds{5}));
  EXPECT_TRUE(victim.kill(SIGKILL).killed_by(SIGKILL));

  // Surviving process can still signal, wait with timeout, and close without
  // deadlock
  ASSERT_EQ(process_rendezvous_signal_producer_ready(parent.get()),
            PROCESS_RENDEZVOUS_OK);
  EXPECT_EQ(
      process_rendezvous_wait_producer_ready_for(parent.get(), 50'000'000LL),
      PROCESS_RENDEZVOUS_OK);
  process_rendezvous_close(parent.get());
  EXPECT_TRUE(process_rendezvous_is_closed(parent.get()));
}

}  // namespace
