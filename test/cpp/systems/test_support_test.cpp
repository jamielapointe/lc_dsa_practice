/// @file test_support_test.cpp
/// @brief Self-tests for the systems test-support library
/// (`systems::test_support`) and `systems/platform.hpp`.
///
/// Unlike scaffolded component tests, these must always pass: every
/// multi-process and fault-injection test in the systems track relies on this
/// harness reporting results correctly.

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "systems/platform.hpp"
#include "systems_test/allocation_counter.hpp"
#include "systems_test/process.hpp"
#include "systems_test/shared_region.hpp"
#include "systems_test/shm_name.hpp"

namespace {

using systems_test::ChildProcess;
using systems_test::ChildStatus;

constexpr auto kWait = std::chrono::seconds{10};

/// Keeps the optimizer from eliding a heap allocation (clang may remove
/// matched new/delete pairs otherwise).
void escape(const void* ptr) { asm volatile("" : : "r"(ptr) : "memory"); }

// ---------------------------------------------------------------- platform

TEST(PlatformTest, CapabilityFlagsAreConsistent) {
  static_assert(systems::platform::kIsSupportedTarget,
                "must be built on a supported OS and CPU architecture");
  static_assert(systems::platform::kIsX86_64 != systems::platform::kIsAarch64,
                "supported targets are either x86_64 or aarch64");
  static_assert(!systems::platform::kHasRobustMutex ||
                    systems::platform::kIsProductionTarget,
                "robust mutexes are only expected on Linux / QNX");
  static_assert(systems::platform::kMaxShmNameLength <= 31);
  static_assert(systems::platform::kCacheLineSize >= 32);
  if constexpr (systems::platform::kIsMacos) {
    EXPECT_FALSE(systems::platform::kHasRobustMutex);
    EXPECT_TRUE(systems::platform::kIsAarch64);
    EXPECT_EQ(systems::platform::kCacheLineSize, 128U);
  }
  EXPECT_TRUE(systems::platform::kHasPosixSharedMemory);
  EXPECT_TRUE(systems::platform::kIsSupportedTarget);
}

// ----------------------------------------------------------------- process

TEST(ChildProcessTest, ReportsExitCode) {
  auto child = ChildProcess::spawn([] { return 7; });
  ASSERT_TRUE(child.valid());
  const auto result = child.wait_for(kWait);
  EXPECT_EQ(result.status, ChildStatus::kExited) << result;
  EXPECT_EQ(result.code, 7);
  EXPECT_FALSE(result.succeeded());
  EXPECT_FALSE(child.valid());
}

TEST(ChildProcessTest, SucceededMeansExitCodeZero) {
  auto child = ChildProcess::spawn([] { return systems_test::kChildOk; });
  const auto result = child.wait_for(kWait);
  EXPECT_TRUE(result.succeeded()) << result;
}

TEST(ChildProcessTest, ReportsTerminatingSignal) {
  auto child = ChildProcess::spawn([] {
    static_cast<void>(::kill(::getpid(), SIGKILL));
    return 0;
  });
  const auto result = child.wait_for(kWait);
  EXPECT_TRUE(result.killed_by(SIGKILL)) << result;
}

TEST(ChildProcessTest, ThrowingBodyReportsDedicatedCode) {
  auto child =
      ChildProcess::spawn([] -> int { throw std::runtime_error{"boom"}; });
  const auto result = child.wait_for(kWait);
  EXPECT_EQ(result.status, ChildStatus::kExited) << result;
  EXPECT_EQ(result.code, systems_test::kChildThrew);
}

TEST(ChildProcessTest, HangingChildTimesOutAndIsKilled) {
  auto child = ChildProcess::spawn([] {
    std::this_thread::sleep_for(std::chrono::seconds{30});
    return 0;
  });
  const auto start = std::chrono::steady_clock::now();
  const auto result = child.wait_for(std::chrono::milliseconds{100});
  EXPECT_EQ(result.status, ChildStatus::kTimedOut) << result;
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds{5});
  EXPECT_FALSE(child.valid());
}

TEST(ChildProcessTest, KillTerminatesAndReapsRunningChild) {
  auto child = ChildProcess::spawn([] {
    std::this_thread::sleep_for(std::chrono::seconds{30});
    return 0;
  });
  const auto result = child.kill();
  EXPECT_TRUE(result.killed_by(SIGKILL)) << result;
  EXPECT_FALSE(child.valid());
}

TEST(ChildProcessTest, KillWithIgnoredSignalFallsBackToSigkill) {
  auto child = ChildProcess::spawn([] {
    static_cast<void>(std::signal(SIGUSR1, SIG_IGN));
    std::this_thread::sleep_for(std::chrono::seconds{30});
    return 0;
  });
  std::this_thread::sleep_for(
      std::chrono::milliseconds{50});  // let the child install SIG_IGN
  const auto result = child.kill(SIGUSR1);
  EXPECT_EQ(result.status, ChildStatus::kSignaled) << result;
  EXPECT_FALSE(child.valid());
}

TEST(ChildProcessTest, DestructorReapsRunningChild) {
  ::pid_t pid = -1;
  {
    auto child = ChildProcess::spawn([] {
      std::this_thread::sleep_for(std::chrono::seconds{30});
      return 0;
    });
    pid = child.pid();
    ASSERT_GT(pid, 0);
  }
  errno = 0;
  EXPECT_EQ(::kill(pid, 0), -1);
  EXPECT_EQ(errno, ESRCH);
}

TEST(ChildProcessTest, MoveTransfersOwnership) {
  auto first = ChildProcess::spawn([] { return 3; });
  ChildProcess second{std::move(first)};
  EXPECT_FALSE(first.valid());  // NOLINT(bugprone-use-after-move): checked.
  ChildProcess third;
  third = std::move(second);
  const auto result = third.wait_for(kWait);
  EXPECT_EQ(result.code, 3) << result;
}

TEST(ChildProcessTest, SendSignalStopsAndResumesWithoutReaping) {
  systems_test::SharedRegion<std::atomic<std::uint64_t>> beats;
  ASSERT_TRUE(beats.valid());
  auto child = ChildProcess::spawn([&beats] {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline) {
      beats->fetch_add(1, std::memory_order_relaxed);
      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return 0;
  });
  const auto is_beating = [&beats] {
    const std::uint64_t start = beats->load(std::memory_order_relaxed);
    return systems_test::wait_until(
        [&beats, start] {
          return beats->load(std::memory_order_relaxed) > start;
        },
        kWait);
  };
  ASSERT_TRUE(is_beating());

  ASSERT_TRUE(child.send_signal(SIGSTOP));
  // Delivery is asynchronous: give the stop a moment to land, then check
  // that a frozen child makes no progress.
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  const std::uint64_t frozen = beats->load(std::memory_order_relaxed);
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  EXPECT_EQ(beats->load(std::memory_order_relaxed), frozen);
  EXPECT_TRUE(child.valid());  // stopped, not reaped

  ASSERT_TRUE(child.send_signal(SIGCONT));
  EXPECT_TRUE(is_beating());
  EXPECT_TRUE(child.kill().killed_by(SIGKILL));
}

TEST(ChildProcessTest, InvalidHandleReportsNotStarted) {
  ChildProcess child;
  EXPECT_FALSE(child.valid());
  EXPECT_EQ(child.wait_for(std::chrono::milliseconds{1}).status,
            ChildStatus::kNotStarted);
  EXPECT_EQ(child.kill().status, ChildStatus::kNotStarted);
  EXPECT_FALSE(child.send_signal(SIGCONT));
}

TEST(ChildProcessTest, ResultsPrintReadably) {
  const auto text = [](systems_test::ChildResult result) {
    std::ostringstream os;
    os << result;
    return os.str();
  };
  EXPECT_EQ(text({.status = ChildStatus::kExited, .code = 2}),
            "child exited with code 2");
  EXPECT_EQ(text({.status = ChildStatus::kSignaled, .code = 9}),
            "child killed by signal 9");
  EXPECT_EQ(text({.status = ChildStatus::kTimedOut, .code = 9}),
            "child timed out and was killed");
  EXPECT_EQ(text({}), "child was never started (fork failed)");
}

// ------------------------------------------------------- shared rendezvous

struct Rendezvous {
  std::atomic<std::uint32_t> ready{0};
  std::atomic<std::uint64_t> counter{0};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

TEST(SharedRegionTest, WritesAreVisibleAcrossFork) {
  systems_test::SharedRegion<Rendezvous> shared;
  ASSERT_TRUE(shared.valid());

  auto child = ChildProcess::spawn([&shared] {
    constexpr std::uint64_t kCount = 1000;
    for (std::uint64_t i = 0; i < kCount; ++i) {
      shared->counter.fetch_add(1, std::memory_order_relaxed);
    }
    shared->ready.store(1, std::memory_order_release);
    return systems_test::kChildOk;
  });

  EXPECT_TRUE(systems_test::wait_until(
      [&shared] { return shared->ready.load(std::memory_order_acquire) == 1; },
      kWait));
  EXPECT_TRUE(child.wait_for(kWait).succeeded());
  EXPECT_EQ(shared->counter.load(), 1000U);
}

TEST(SharedRegionTest, ValueInitializesContents) {
  const systems_test::SharedRegion<std::uint64_t> shared;
  ASSERT_TRUE(shared.valid());
  EXPECT_EQ(*shared, 0U);
}

TEST(WaitUntilTest, ReturnsFalseAtDeadline) {
  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(systems_test::wait_until([] { return false; },
                                        std::chrono::milliseconds{20}));
  EXPECT_GE(std::chrono::steady_clock::now() - start,
            std::chrono::milliseconds{20});
}

TEST(WaitUntilTest, ReturnsTrueImmediatelyWhenSatisfied) {
  EXPECT_TRUE(systems_test::wait_until([] { return true; },
                                       std::chrono::milliseconds{0}));
}

// ---------------------------------------------------------------- shm name

TEST(ShmNameTest, NamesAreUniquePortableAndSanitized) {
  std::set<std::string> names;
  for (int i = 0; i < 100; ++i) {
    const auto name = systems_test::unique_shm_name("a-very/long:prefix");
    EXPECT_EQ(name.front(), '/');
    EXPECT_LE(name.size(), systems::platform::kMaxShmNameLength);
    EXPECT_EQ(name.find('/', 1), std::string::npos) << name;
    EXPECT_EQ(name.find(':'), std::string::npos) << name;
    names.insert(name);
  }
  EXPECT_EQ(names.size(), 100U);
}

TEST(ShmNameTest, ScopedUnlinkRemovesObject) {
  const auto name = systems_test::unique_shm_name("unlink");
  {
    const systems_test::ScopedShmUnlink guard{name};
    EXPECT_EQ(guard.name(), name);
    // NOLINTNEXTLINE(*-vararg): POSIX API.
    const int fd = ::shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    ASSERT_GE(fd, 0);
    static_cast<void>(::close(fd));
    EXPECT_TRUE(systems_test::shm_exists(name));
  }
  EXPECT_FALSE(systems_test::shm_exists(name));
}

// ------------------------------------------------------ allocation counter

TEST(AllocationScopeTest, CountsHeapAllocationsInsideScope) {
  const systems_test::AllocationScope scope;
  {
    auto one = std::make_unique<int>(1);
    escape(one.get());
    auto many = std::make_unique<std::uint64_t[]>(16);  // NOLINT(*-c-arrays)
    escape(many.get());
  }
  EXPECT_EQ(scope.allocations(), 2U);
}

TEST(AllocationScopeTest, CountsAlignedAndNothrowAllocations) {
  struct alignas(128) Wide {
    std::uint8_t byte{0};
  };
  const systems_test::AllocationScope scope;
  {
    auto wide = std::make_unique<Wide>();
    escape(wide.get());
    std::unique_ptr<int> nothrow{new (std::nothrow) int{4}};
    escape(nothrow.get());
  }
  EXPECT_EQ(scope.allocations(), 2U);
}

TEST(AllocationScopeTest, IgnoresAllocationsOutsideScope) {
  auto before = std::make_unique<int>(1);
  escape(before.get());
  std::size_t count = 0;
  {
    const systems_test::AllocationScope scope;
    count = scope.allocations();
  }
  auto after = std::make_unique<int>(2);
  escape(after.get());
  EXPECT_EQ(count, 0U);
}

TEST(AllocationScopeTest, NestedScopeIsReported) {
  const systems_test::AllocationScope outer;
  const systems_test::AllocationScope inner;
  EXPECT_EQ(inner.allocations(), systems_test::AllocationScope::kNestedScope);
  EXPECT_NE(outer.allocations(), systems_test::AllocationScope::kNestedScope);
}

TEST(AllocationScopeTest, CountsAllocationsFromOtherThreads) {
  const systems_test::AllocationScope scope;
  std::size_t after_thread_start = 0;
  {
    std::thread worker{[] {
      auto value = std::make_unique<int>(5);
      escape(value.get());
    }};
    worker.join();
    after_thread_start = scope.allocations();
  }
  // At least the worker's allocation (std::thread may allocate its state).
  EXPECT_GE(after_thread_start, 1U);
}

}  // namespace
