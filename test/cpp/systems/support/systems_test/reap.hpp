#pragma once

/// @file reap.hpp
/// @brief `waitpid` plumbing behind `ChildProcess`: reap a child and decode
/// its status word. Internal to `systems::test_support`; tests use
/// `process.hpp`.
///
/// This header deliberately includes only `<sys/types.h>`. It is the main
/// header of `reap.cpp`, so it is the first thing that file includes. glibc
/// defines `WIFEXITED`, `WNOHANG`, and the other wait macros in whichever of
/// `<stdlib.h>` / `<sys/wait.h>` comes first, and almost every C++ standard
/// header pulls in `<stdlib.h>`. With nothing heavier in front of it,
/// `reap.cpp` gets the macros from `<sys/wait.h>` (the POSIX header it
/// includes for them), which is what clang-tidy's `misc-include-cleaner`
/// expects. Adding a C++ standard header here would move the definitions back
/// to `<stdlib.h>`.

#include <sys/types.h>

namespace systems_test::detail {

/// @brief How a reaped child ended, decoded from its `waitpid` status word.
struct WaitStatus {
  /// True: killed by signal `code`. False: exited with code `code`.
  bool signaled{false};
  /// Exit code or signal number.
  int code{0};
};

/// @brief Decodes a `waitpid` status word.
///
/// A status that is neither a normal exit nor a signal death (not expected
/// for reaped children) is reported as an exit with the raw status as code.
[[nodiscard]] auto decode_wait_status(int status) noexcept -> WaitStatus;

/// @brief Non-blocking reap (`WNOHANG`) that retries on EINTR.
/// @return True, with `status` filled in, once the child has ended and was
/// reaped; false while it is still running or on error.
[[nodiscard]] auto try_reap(::pid_t pid, int& status) noexcept -> bool;

/// @brief Blocking reap that retries on EINTR.
/// @return The child's `waitpid` status word.
[[nodiscard]] auto reap(::pid_t pid) noexcept -> int;

}  // namespace systems_test::detail
