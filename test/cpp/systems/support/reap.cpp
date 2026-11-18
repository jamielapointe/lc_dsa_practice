/// @file reap.cpp
/// @brief `waitpid` helpers behind `ChildProcess` (see `reap.hpp` for why
/// they live in their own translation unit).

#if defined(SYSTEMS_POSIX_SUPPORTED)

#include "systems_test/reap.hpp"

#include <sys/types.h>
#include <sys/wait.h>

#include <cerrno>

#include "systems/platform.hpp"

static_assert(systems::platform::kIsSupportedTarget,
              "reap.cpp compiled on an unsupported target");

namespace systems_test::detail {

auto decode_wait_status(int status) noexcept -> WaitStatus {
  if (WIFEXITED(status)) {
    return {.signaled = false, .code = WEXITSTATUS(status)};
  }
  if (WIFSIGNALED(status)) {
    return {.signaled = true, .code = WTERMSIG(status)};
  }
  return {.signaled = false, .code = status};
}

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

auto reap(::pid_t pid) noexcept -> int {
  int status = 0;
  while (::waitpid(pid, &status, 0) == -1 && errno == EINTR) {
  }
  return status;
}

}  // namespace systems_test::detail

#else
#error \
    "reap.cpp requires a supported POSIX target (Linux x86_64/aarch64/riscv64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
