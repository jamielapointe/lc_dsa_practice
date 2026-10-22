/// @file process_rendezvous_test.cpp
/// @brief Requirements-based tests for `systems::ProcessRendezvous`
/// (`docs/systems/process_rendezvous.md`, prefix PRV).

#include "systems/process_rendezvous.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <latch>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "systems/platform.hpp"
#include "systems_test/allocation_counter.hpp"
#include "systems_test/process.hpp"
#include "systems_test/shared_region.hpp"
#include "systems_test/shm_name.hpp"

namespace {

using systems::ProcessRendezvous;
using systems::ProcessRendezvousError;
using Clock = std::chrono::steady_clock;

// PRV-REQ-009: move-only handle with noexcept operations.
static_assert(!std::is_copy_constructible_v<ProcessRendezvous>);
static_assert(std::is_nothrow_move_constructible_v<ProcessRendezvous>);
static_assert(std::is_nothrow_move_assignable_v<ProcessRendezvous>);
static_assert(noexcept(ProcessRendezvous::create(std::string_view{})));
static_assert(noexcept(ProcessRendezvous::open(std::string_view{})));
static_assert(noexcept(ProcessRendezvous::attach(std::string_view{},
                                                 std::chrono::nanoseconds{})));
static_assert(noexcept(ProcessRendezvous::unlink(std::string_view{})));
static_assert(
    noexcept(std::declval<ProcessRendezvous&>().signal_producer_ready()));
static_assert(
    noexcept(std::declval<ProcessRendezvous&>().signal_consumer_ready()));
static_assert(noexcept(
    std::declval<const ProcessRendezvous&>().try_wait_producer_ready()));
static_assert(noexcept(
    std::declval<const ProcessRendezvous&>().try_wait_consumer_ready()));
static_assert(noexcept(std::declval<ProcessRendezvous&>().close()));

class TestRendezvousName {
 public:
  TestRendezvousName()
      : name_{systems_test::unique_shm_name("prv")}, guard_{name_} {}
  ~TestRendezvousName() { static_cast<void>(ProcessRendezvous::unlink(name_)); }
  TestRendezvousName(const TestRendezvousName&) = delete;
  auto operator=(const TestRendezvousName&) -> TestRendezvousName& = delete;
  TestRendezvousName(TestRendezvousName&&) = delete;
  auto operator=(TestRendezvousName&&) -> TestRendezvousName& = delete;

  [[nodiscard]] auto str() const -> const std::string& { return name_; }

 private:
  std::string name_;
  systems_test::ScopedShmUnlink guard_;
};

auto error_text(ProcessRendezvousError error) -> std::string {
  return std::string{systems::to_string(error)};
}

// ===================================================== Tier 1-3: functional

TEST(ProcessRendezvousTest, CreateOpenAndAttachInitializeUnsetStages) {
  RecordProperty("requirements", "PRV-REQ-001");
  const TestRendezvousName name;
  auto created = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(created.has_value()) << error_text(created.error());
  EXPECT_FALSE(created->is_producer_ready());
  EXPECT_FALSE(created->is_consumer_ready());
  EXPECT_FALSE(created->is_closed());

  auto duplicate = ProcessRendezvous::create(name.str());
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(duplicate.error(), ProcessRendezvousError::kAlreadyExists);

  auto opened = ProcessRendezvous::open(name.str());
  ASSERT_TRUE(opened.has_value()) << error_text(opened.error());
  EXPECT_FALSE(opened->is_producer_ready());
  EXPECT_FALSE(opened->is_consumer_ready());

  auto attached =
      ProcessRendezvous::attach(name.str(), std::chrono::milliseconds{100});
  ASSERT_TRUE(attached.has_value()) << error_text(attached.error());
  EXPECT_FALSE(attached->is_producer_ready());

  const TestRendezvousName fresh_name;
  auto attached_fresh = ProcessRendezvous::attach(
      fresh_name.str(), std::chrono::milliseconds{100});
  ASSERT_TRUE(attached_fresh.has_value()) << error_text(attached_fresh.error());
  EXPECT_FALSE(attached_fresh->is_producer_ready());
}

TEST(ProcessRendezvousTest, OpenRejectsMissingAndIncompatibleSegments) {
  RecordProperty("requirements", "PRV-REQ-001");
  const TestRendezvousName missing;
  auto not_found = ProcessRendezvous::open(missing.str());
  ASSERT_FALSE(not_found.has_value());
  EXPECT_EQ(not_found.error(), ProcessRendezvousError::kNotFound);

  const TestRendezvousName bad_seg;
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  const int fd =
      ::shm_open(bad_seg.str().c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  // NOLINTEND(*-vararg)
  ASSERT_GE(fd, 0);
  EXPECT_EQ(::ftruncate(fd, 64), 0);
  static_cast<void>(::close(fd));

  auto mismatch = ProcessRendezvous::open(bad_seg.str());
  ASSERT_FALSE(mismatch.has_value());
  EXPECT_EQ(mismatch.error(), ProcessRendezvousError::kLayoutMismatch);

  auto attach_timeout =
      ProcessRendezvous::attach(bad_seg.str(), std::chrono::milliseconds{5});
  ASSERT_FALSE(attach_timeout.has_value());
  EXPECT_EQ(attach_timeout.error(), ProcessRendezvousError::kTimedOut);
}

TEST(ProcessRendezvousTest, RejectsInvalidAndOverlongNames) {
  RecordProperty("requirements", "PRV-REQ-002");
  for (const char* bad : {"", "noslash", "/", "/a/b"}) {
    auto created = ProcessRendezvous::create(bad);
    ASSERT_FALSE(created.has_value());
    EXPECT_EQ(created.error(), ProcessRendezvousError::kInvalidArgument);

    auto opened = ProcessRendezvous::open(bad);
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error(), ProcessRendezvousError::kInvalidArgument);

    auto attached =
        ProcessRendezvous::attach(bad, std::chrono::milliseconds{1});
    ASSERT_FALSE(attached.has_value());
    EXPECT_EQ(attached.error(), ProcessRendezvousError::kInvalidArgument);

    auto unlinked = ProcessRendezvous::unlink(bad);
    ASSERT_FALSE(unlinked.has_value());
    EXPECT_EQ(unlinked.error(), ProcessRendezvousError::kInvalidArgument);
  }

  const std::string too_long =
      "/" + std::string(systems::platform::kMaxShmNameLength, 'r');
  auto res = ProcessRendezvous::create(too_long);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), ProcessRendezvousError::kNameTooLong);
}

TEST(ProcessRendezvousTest, UnlinkRemovesNameWhileExistingHandlesKeepWorking) {
  RecordProperty("requirements", "PRV-REQ-003");
  const TestRendezvousName name;
  auto created = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(created.has_value()) << error_text(created.error());
  auto opened = ProcessRendezvous::open(name.str());
  ASSERT_TRUE(opened.has_value()) << error_text(opened.error());

  auto unlinked = ProcessRendezvous::unlink(name.str());
  ASSERT_TRUE(unlinked.has_value()) << error_text(unlinked.error());

  auto reopen = ProcessRendezvous::open(name.str());
  ASSERT_FALSE(reopen.has_value());
  EXPECT_EQ(reopen.error(), ProcessRendezvousError::kNotFound);

  auto unlink_again = ProcessRendezvous::unlink(name.str());
  ASSERT_FALSE(unlink_again.has_value());
  EXPECT_EQ(unlink_again.error(), ProcessRendezvousError::kNotFound);

  ASSERT_TRUE(created->signal_producer_ready().has_value());
  EXPECT_TRUE(opened->is_producer_ready());
}

TEST(ProcessRendezvousTest, ProducerAndConsumerStagesSignalAndWait) {
  RecordProperty("requirements", "PRV-REQ-004,PRV-REQ-005");
  const TestRendezvousName name;
  auto producer = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(producer.has_value()) << error_text(producer.error());
  auto consumer = ProcessRendezvous::open(name.str());
  ASSERT_TRUE(consumer.has_value()) << error_text(consumer.error());

  EXPECT_EQ(consumer->try_wait_producer_ready().error(),
            ProcessRendezvousError::kWouldBlock);
  EXPECT_EQ(
      consumer->wait_producer_ready_for(std::chrono::milliseconds{3}).error(),
      ProcessRendezvousError::kTimedOut);

  ASSERT_TRUE(producer->signal_producer_ready().has_value());
  ASSERT_TRUE(producer->signal_producer_ready().has_value());  // idempotent
  EXPECT_TRUE(consumer->is_producer_ready());
  EXPECT_TRUE(consumer->try_wait_producer_ready().has_value());
  EXPECT_TRUE(consumer->wait_producer_ready_for(std::chrono::milliseconds{1})
                  .has_value());
  EXPECT_TRUE(consumer->wait_producer_ready().has_value());

  EXPECT_EQ(producer->try_wait_consumer_ready().error(),
            ProcessRendezvousError::kWouldBlock);
  EXPECT_EQ(
      producer->wait_consumer_ready_for(std::chrono::milliseconds{3}).error(),
      ProcessRendezvousError::kTimedOut);

  ASSERT_TRUE(consumer->signal_consumer_ready().has_value());
  ASSERT_TRUE(consumer->signal_consumer_ready().has_value());  // idempotent
  EXPECT_TRUE(producer->is_consumer_ready());
  EXPECT_TRUE(producer->try_wait_consumer_ready().has_value());
  EXPECT_TRUE(producer->wait_consumer_ready_for(std::chrono::milliseconds{1})
                  .has_value());
  EXPECT_TRUE(producer->wait_consumer_ready().has_value());
}

TEST(ProcessRendezvousTest, CloseWakesUnsignaledWaitersAndPreservesSignaled) {
  RecordProperty("requirements", "PRV-REQ-006");
  const TestRendezvousName name;
  auto first = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(first.has_value()) << error_text(first.error());
  auto second = ProcessRendezvous::open(name.str());
  ASSERT_TRUE(second.has_value()) << error_text(second.error());

  ASSERT_TRUE(first->signal_producer_ready().has_value());
  first->close();
  EXPECT_TRUE(first->is_closed());
  EXPECT_TRUE(second->is_closed());

  // Already-signaled stage stays ready after close.
  EXPECT_TRUE(second->try_wait_producer_ready().has_value());
  EXPECT_TRUE(second->wait_producer_ready().has_value());

  // Unsignaled stage reports kClosed.
  EXPECT_EQ(second->try_wait_consumer_ready().error(),
            ProcessRendezvousError::kClosed);
  EXPECT_EQ(
      second->wait_consumer_ready_for(std::chrono::milliseconds{50}).error(),
      ProcessRendezvousError::kClosed);

  // Signaling after close fails with kClosed.
  EXPECT_EQ(first->signal_producer_ready().error(),
            ProcessRendezvousError::kClosed);
  EXPECT_EQ(second->signal_consumer_ready().error(),
            ProcessRendezvousError::kClosed);
}

TEST(ProcessRendezvousTest, MoveSemanticsAndErrorStrings) {
  RecordProperty("requirements", "PRV-REQ-009");
  const TestRendezvousName name;
  auto created = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(created.has_value());

  ProcessRendezvous moved{std::move(*created)};
  EXPECT_EQ(created->signal_producer_ready().error(),
            ProcessRendezvousError::kInvalidArgument);
  EXPECT_EQ(created->signal_consumer_ready().error(),
            ProcessRendezvousError::kInvalidArgument);
  EXPECT_EQ(created->try_wait_producer_ready().error(),
            ProcessRendezvousError::kInvalidArgument);
  EXPECT_EQ(created->try_wait_consumer_ready().error(),
            ProcessRendezvousError::kInvalidArgument);
  EXPECT_EQ(
      created->wait_producer_ready_for(std::chrono::milliseconds{1}).error(),
      ProcessRendezvousError::kInvalidArgument);
  EXPECT_EQ(
      created->wait_consumer_ready_for(std::chrono::milliseconds{1}).error(),
      ProcessRendezvousError::kInvalidArgument);
  created->close();
  EXPECT_FALSE(created->is_closed());

  ProcessRendezvous assigned = std::move(moved);
  ASSERT_TRUE(assigned.signal_producer_ready().has_value());
  EXPECT_TRUE(assigned.is_producer_ready());

  for (const auto err :
       {ProcessRendezvousError::kNotImplemented,
        ProcessRendezvousError::kInvalidArgument,
        ProcessRendezvousError::kNameTooLong,
        ProcessRendezvousError::kAlreadyExists,
        ProcessRendezvousError::kNotFound,
        ProcessRendezvousError::kLayoutMismatch,
        ProcessRendezvousError::kWouldBlock, ProcessRendezvousError::kTimedOut,
        ProcessRendezvousError::kClosed,
        ProcessRendezvousError::kMemoryLockFailed,
        ProcessRendezvousError::kOsError}) {
    EXPECT_FALSE(systems::to_string(err).empty());
  }
}

TEST(ProcessRendezvousTest, NoHeapAllocationAfterInit) {
  RecordProperty("requirements", "PRV-REQ-008");
  const TestRendezvousName name;
  auto pub = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(pub.has_value());
  auto sub = ProcessRendezvous::open(name.str());
  ASSERT_TRUE(sub.has_value());

  std::size_t allocations = 0;
  bool ok = false;
  {
    const systems_test::AllocationScope scope;
    const auto s1 = pub->signal_producer_ready();
    const auto w1 = sub->wait_producer_ready_for(std::chrono::milliseconds{10});
    const auto s2 = sub->signal_consumer_ready();
    const auto w2 = pub->wait_consumer_ready_for(std::chrono::milliseconds{10});
    pub->close();
    ok = s1.has_value() && w1.has_value() && s2.has_value() && w2.has_value() &&
         sub->is_closed();
    allocations = scope.allocations();
  }
  EXPECT_TRUE(ok);
  EXPECT_EQ(allocations, 0U);
}

// ===================================================== Tier 4: multi-thread

TEST(ProcessRendezvousConcurrencyTest, MultipleThreadsWaitAndWakeTogether) {
  RecordProperty("requirements", "PRV-REQ-004,PRV-REQ-005,PRV-REQ-007");
  const TestRendezvousName name;
  auto coord = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(coord.has_value());

  constexpr int kWaiters = 6;
  std::latch start_gate{kWaiters + 1};
  std::atomic<int> woke_producer{0};
  std::atomic<int> woke_consumer{0};

  {
    std::vector<std::jthread> threads;
    threads.reserve(kWaiters);
    for (int i = 0; i < kWaiters; ++i) {
      threads.emplace_back([&] {
        auto local = ProcessRendezvous::open(name.str());
        if (!local) {
          return;
        }
        start_gate.arrive_and_wait();
        if (local->wait_producer_ready_for(std::chrono::seconds{5})
                .has_value()) {
          woke_producer.fetch_add(1, std::memory_order_relaxed);
        }
        if (local->wait_consumer_ready_for(std::chrono::seconds{5})
                .has_value()) {
          woke_consumer.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    start_gate.arrive_and_wait();
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_TRUE(coord->signal_producer_ready().has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    ASSERT_TRUE(coord->signal_consumer_ready().has_value());
  }

  EXPECT_EQ(woke_producer.load(), kWaiters);
  EXPECT_EQ(woke_consumer.load(), kWaiters);
}

// ===================================================== Tier 5: multi-process

TEST(ProcessRendezvousMultiProcessTest, TwoProcessHandshakeOrdersStartup) {
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
    auto rdv = ProcessRendezvous::attach(name.str(), std::chrono::seconds{5});
    if (!rdv) {
      return 1;
    }
    if (!rdv->wait_producer_ready_for(std::chrono::seconds{5})) {
      return 2;
    }
    order->consumer_step.store(
        order->step.fetch_add(1, std::memory_order_acq_rel) + 1U,
        std::memory_order_release);
    if (!rdv->signal_consumer_ready()) {
      return 3;
    }
    return systems_test::kChildOk;
  });

  auto producer_rdv =
      ProcessRendezvous::attach(name.str(), std::chrono::seconds{5});
  ASSERT_TRUE(producer_rdv.has_value()) << error_text(producer_rdv.error());
  order->producer_step.store(
      order->step.fetch_add(1, std::memory_order_acq_rel) + 1U,
      std::memory_order_release);
  ASSERT_TRUE(producer_rdv->signal_producer_ready().has_value());
  ASSERT_TRUE(producer_rdv->wait_consumer_ready_for(std::chrono::seconds{5})
                  .has_value());

  const auto child_result = consumer_child.wait_for(std::chrono::seconds{10});
  EXPECT_TRUE(child_result.succeeded()) << child_result;
  EXPECT_EQ(order->producer_step.load(), 1U);
  EXPECT_EQ(order->consumer_step.load(), 2U);
}

// ===================================================== Tier 6: fault injection

TEST(ProcessRendezvousFaultTest, SurvivesParticipantKilledWithSigkill) {
  RecordProperty("requirements", "PRV-REQ-006,PRV-REQ-010");
  const TestRendezvousName name;
  auto parent = ProcessRendezvous::create(name.str());
  ASSERT_TRUE(parent.has_value());

  systems_test::SharedRegion<std::atomic<std::uint32_t>> child_attached;
  ASSERT_TRUE(child_attached.valid());

  auto victim = systems_test::ChildProcess::spawn([&] -> int {
    auto rdv = ProcessRendezvous::open(name.str());
    if (!rdv) {
      return 1;
    }
    child_attached->store(1, std::memory_order_release);
    static_cast<void>(rdv->wait_producer_ready_for(std::chrono::seconds{30}));
    return 0;
  });

  ASSERT_TRUE(systems_test::wait_until(
      [&] { return child_attached->load(std::memory_order_acquire) == 1U; },
      std::chrono::seconds{5}));
  EXPECT_TRUE(victim.kill(SIGKILL).killed_by(SIGKILL));

  // Surviving process can still signal, wait, and close without deadlock.
  ASSERT_TRUE(parent->signal_producer_ready().has_value());
  EXPECT_TRUE(parent->wait_producer_ready_for(std::chrono::milliseconds{50})
                  .has_value());
  parent->close();
  EXPECT_TRUE(parent->is_closed());
}

}  // namespace
