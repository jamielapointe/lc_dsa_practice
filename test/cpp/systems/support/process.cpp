/// @file process.cpp
/// @brief Implementation of the fork-based child-process harness.

#include "systems_test/process.hpp"

#include <sys/types.h>
#include <sys/wait.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <ostream>
#include <thread>
#include <utility>

namespace systems_test {
namespace {

/// Converts a `waitpid` status word into a `ChildResult`.
auto decode_status(int status) noexcept -> ChildResult {
  if (WIFEXITED(status)) {
    return {.status = ChildStatus::kExited, .code = WEXITSTATUS(status)};
  }
  if (WIFSIGNALED(status)) {
    return {.status = ChildStatus::kSignaled, .code = WTERMSIG(status)};
  }
  return {.status = ChildStatus::kExited, .code = status};
}

/// Non-blocking reap. Returns true and fills `status` once the child ended.
auto try_reap(::pid_t pid, int& status) noexcept -> bool {
  while (true) {
    const ::pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      return true;
    }
    if (result == -1 && errno == EINTR) {
      continue;
    }
    return false;
  }
}

/// Blocking reap that retries on EINTR.
auto reap(::pid_t pid) noexcept -> int {
  int status = 0;
  while (::waitpid(pid, &status, 0) == -1 && errno == EINTR) {
  }
  return status;
}

}  // namespace

auto operator<<(std::ostream& os, const ChildResult& result) -> std::ostream& {
  switch (result.status) {
    case ChildStatus::kExited:
      return os << "child exited with code " << result.code;
    case ChildStatus::kSignaled:
      return os << "child killed by signal " << result.code;
    case ChildStatus::kTimedOut:
      return os << "child timed out and was killed";
    case ChildStatus::kNotStarted:
      return os << "child was never started (fork failed)";
  }
  return os << "unknown child status";
}

ChildProcess::~ChildProcess() {
  if (valid()) {
    static_cast<void>(kill(SIGKILL));
  }
}

auto ChildProcess::operator=(ChildProcess&& other) noexcept -> ChildProcess& {
  if (this != &other) {
    if (valid()) {
      static_cast<void>(kill(SIGKILL));
    }
    pid_ = std::exchange(other.pid_, kNoPid);
  }
  return *this;
}

auto ChildProcess::wait_for(std::chrono::milliseconds timeout) -> ChildResult {
  if (!valid()) {
    return {};
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (!try_reap(pid_, status)) {
    if (std::chrono::steady_clock::now() >= deadline) {
      static_cast<void>(::kill(pid_, SIGKILL));
      static_cast<void>(reap(pid_));
      pid_ = kNoPid;
      return {.status = ChildStatus::kTimedOut, .code = SIGKILL};
    }
    std::this_thread::sleep_for(std::chrono::microseconds{500});
  }
  pid_ = kNoPid;
  return decode_status(status);
}

auto ChildProcess::kill(int signal) -> ChildResult {
  if (!valid()) {
    return {};
  }
  static_cast<void>(::kill(pid_, signal));
  if (signal == SIGKILL) {
    const int status = reap(pid_);
    pid_ = kNoPid;
    return decode_status(status);
  }
  // A non-fatal signal may be handled or ignored: give the child a moment,
  // then fall back to SIGKILL so the handle is always reaped.
  constexpr auto kGrace = std::chrono::seconds{1};
  const ChildResult result = wait_for(kGrace);
  if (result.status == ChildStatus::kTimedOut) {
    return {.status = ChildStatus::kSignaled, .code = SIGKILL};
  }
  return result;
}

}  // namespace systems_test
