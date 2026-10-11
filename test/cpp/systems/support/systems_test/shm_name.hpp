#pragma once

/// @file shm_name.hpp
/// @brief Collision-free POSIX shared-memory names for tests, plus RAII
/// cleanup.
///
/// Tests run in parallel (`ctest -j`) and across sanitizer presets on the
/// same machine, so every test must use its own shared-memory name. Names are
/// `/` + prefix + pid + counter, and always fit in
/// `systems::platform::kMaxShmNameLength` (30) characters so they are valid on
/// macOS, Linux, and QNX.

#include <string>
#include <string_view>

namespace systems_test {

/// @brief Returns a unique, portable shared-memory name such as
/// `/shmps_1a2b_3`.
/// @param prefix Short tag (truncated to 8 characters; `[A-Za-z0-9_]`).
[[nodiscard]] auto unique_shm_name(std::string_view prefix) -> std::string;

/// @brief Calls `shm_unlink(name)` on construction and destruction.
///
/// Create one right after choosing a name: it removes stale objects left by a
/// crashed earlier run and guarantees cleanup when an assertion fails. Errors
/// (for example "does not exist") are ignored.
class ScopedShmUnlink {
 public:
  explicit ScopedShmUnlink(std::string name);
  ~ScopedShmUnlink();

  ScopedShmUnlink(const ScopedShmUnlink&) = delete;
  auto operator=(const ScopedShmUnlink&) -> ScopedShmUnlink& = delete;
  ScopedShmUnlink(ScopedShmUnlink&&) = delete;
  auto operator=(ScopedShmUnlink&&) -> ScopedShmUnlink& = delete;

  [[nodiscard]] auto name() const noexcept -> const std::string& {
    return name_;
  }

 private:
  std::string name_;
};

/// @brief True if a shared-memory object called `name` currently exists.
[[nodiscard]] auto shm_exists(const std::string& name) -> bool;

}  // namespace systems_test
