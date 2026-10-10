#pragma once

/// @file process.hpp
/// @brief Fork-based child-process harness for multi-process GoogleTest
/// tests.
///
/// GoogleTest assertions do not cross a `fork()` boundary, so a child reports
/// its verdict through its exit code: return `kChildOk` (0) on success and a
/// distinct small positive code for each failure point. The parent then
/// asserts on the `ChildResult`, for example:
///
/// @code
///   auto child = systems_test::ChildProcess::spawn([&]() -> int {
///     auto channel = Channel::open(name);
///     if (!channel) { return 1; }
///     return channel->publish(payload) ? systems_test::kChildOk : 2;
///   });
///   const auto result = child.wait_for(std::chrono::seconds{5});
///   EXPECT_TRUE(result.succeeded()) << result;
/// @endcode
///
/// Rules for child bodies:
///   - Fork only while the test process is single-threaded (join every thread
///     first). Forking a multi-threaded process is unsupported under TSan and
///     can deadlock on locks held by other threads.
///   - The child ends with `_exit`, so destructors, `atexit` handlers, gtest
///     reporting, and LeakSanitizer do not run in the child. Do not rely on
///     RAII cleanup there; clean shared resources up in the parent.
///   - Always wait with a timeout. A stub that never signals must turn into a
///     `kTimedOut` result, never a hung test.

#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <ostream>
#include <thread>
#include <type_traits>
#include <utility>

namespace systems_test {

/// @brief Exit code a child returns when every check passed.
inline constexpr int kChildOk = 0;
/// @brief Exit code used when the child body threw an exception.
inline constexpr int kChildThrew = 125;
/// @brief Pseudo exit code reported when `fork()` itself failed.
inline constexpr int kForkFailed = -1;

/// @brief How a child process ended.
enum class ChildStatus : std::uint8_t {
  kExited,     ///< Normal exit; `ChildResult::code` is the exit code.
  kSignaled,   ///< Killed by a signal; `ChildResult::code` is the signal.
  kTimedOut,   ///< Still running at the deadline; it was SIGKILLed and reaped.
  kNotStarted  ///< `fork()` failed; no process exists.
};

/// @brief Outcome of waiting for (or killing) a child.
struct ChildResult {
  ChildStatus status{ChildStatus::kNotStarted};
  int code{kForkFailed};

  /// @brief True when the child exited normally with `kChildOk`.
  [[nodiscard]] auto succeeded() const noexcept -> bool {
    return status == ChildStatus::kExited && code == kChildOk;
  }

  /// @brief True when the child was terminated by `signal`.
  [[nodiscard]] auto killed_by(int signal) const noexcept -> bool {
    return status == ChildStatus::kSignaled && code == signal;
  }
};

/// @brief Human-readable result, for gtest failure messages.
auto operator<<(std::ostream& os, const ChildResult& result) -> std::ostream&;

/// @brief RAII handle to a forked child process.
///
/// The destructor SIGKILLs and reaps a child that is still running, so a
/// failing test never leaks processes. Move-only.
class ChildProcess {
 public:
  /// @brief Forks and runs `body()` in the child, exiting with its result.
  /// @param body Callable returning `int` (the child's exit code). Captures
  ///        are copied into the child's address space by `fork()`.
  /// @return Handle to the child; `valid()` is false when `fork()` failed.
  template <typename Body>
    requires std::is_invocable_r_v<int, Body&>
  [[nodiscard]] static auto spawn(Body&& body) -> ChildProcess {
    // Flush stdio so buffered output is not duplicated into the child.
    static_cast<void>(std::fflush(nullptr));
    const ::pid_t pid = ::fork();
    if (pid == 0) {
      int code = kChildThrew;
      try {
        code = std::invoke(std::forward<Body>(body));
      } catch (...) {  // NOLINT(bugprone-empty-catch): reported via exit code.
        code = kChildThrew;
      }
      ::_exit(code);
    }
    return ChildProcess{pid};
  }

  ChildProcess() noexcept = default;
  ~ChildProcess();

  ChildProcess(const ChildProcess&) = delete;
  auto operator=(const ChildProcess&) -> ChildProcess& = delete;
  ChildProcess(ChildProcess&& other) noexcept
      : pid_{std::exchange(other.pid_, kNoPid)} {}
  auto operator=(ChildProcess&& other) noexcept -> ChildProcess&;

  /// @brief True while the handle refers to an un-reaped child.
  [[nodiscard]] auto valid() const noexcept -> bool { return pid_ > 0; }

  /// @brief Process id, or a non-positive value when not valid.
  [[nodiscard]] auto pid() const noexcept -> ::pid_t { return pid_; }

  /// @brief Waits up to `timeout` for the child to end, then reaps it.
  ///
  /// On timeout the child is SIGKILLed, reaped, and `kTimedOut` is returned.
  /// After this call the handle is no longer `valid()`.
  auto wait_for(std::chrono::milliseconds timeout) -> ChildResult;

  /// @brief Sends `signal` (default SIGKILL) and reaps the child.
  ///
  /// Use this for fault injection: kill a process at an arbitrary point and
  /// check that the survivors recover. A child that ignores a non-fatal
  /// signal is SIGKILLed after one second.
  auto kill(int signal = SIGKILL) -> ChildResult;

 private:
  static constexpr ::pid_t kNoPid = -1;

  explicit ChildProcess(::pid_t pid) noexcept : pid_{pid} {}

  ::pid_t pid_{kNoPid};
};

/// @brief Polls `predicate` until it returns true or `timeout` elapses.
///
/// Intended for parent/child rendezvous through `SharedRegion` atomics (for
/// example "wait until the child has published 100 samples").
/// @return True if the predicate became true before the deadline.
template <typename Predicate>
  requires std::is_invocable_r_v<bool, Predicate&>
[[nodiscard]] auto wait_until(Predicate predicate,
                              std::chrono::milliseconds timeout) -> bool {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!std::invoke(predicate)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds{200});
  }
  return true;
}

}  // namespace systems_test
