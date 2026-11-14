#pragma once

/// @file ownership_and_raii.hpp
/// @brief Non-trivially relocatable RAII lease guard exported to Rust 2024
/// (`!Unpin` + `ctor`).

namespace interop::crubit {

/// @brief Exclusive RAII lease handle tracking live lease count via an atomic
/// counter.
///
/// Because this class defines a user-provided destructor and move operations,
/// Crubit projects it to Rust as a `!Unpin` type initialized via
/// `ctor::emplace!` whose deterministic C++ destructor runs on Rust drop.
class LeaseGuard {
 public:
  /// @brief Acquires a new active lease with `lease_id` and 1 initial epoch.
  explicit LeaseGuard(int lease_id) noexcept;

  LeaseGuard(const LeaseGuard&) = delete;
  auto operator=(const LeaseGuard&) -> LeaseGuard& = delete;

  /// @brief Transfers lease ownership from `other`, leaving `other` inactive.
  LeaseGuard(LeaseGuard&& other) noexcept;

  /// @brief Releases any active lease held by `*this` and transfers ownership
  /// from `other`.
  auto operator=(LeaseGuard&& other) noexcept -> LeaseGuard&;

  /// @brief Releases the lease if active and decrements the global active lease
  /// count.
  ~LeaseGuard() noexcept;

  /// @brief Returns the lease identifier (or `-1` if moved-from).
  [[nodiscard]] auto lease_id() const noexcept -> int;

  /// @brief Adds positive `extra_epochs` if active and returns the updated
  /// epoch count.
  auto renew(int extra_epochs) noexcept -> int;

  /// @brief Returns remaining epochs (0 if moved-from).
  [[nodiscard]] auto epochs_remaining() const noexcept -> int;

  /// @brief Returns the number of currently active `LeaseGuard` instances
  /// across threads.
  [[nodiscard]] static auto active_lease_count() noexcept -> int;

 private:
  int lease_id_ = -1;
  int epochs_remaining_ = 0;
  bool active_ = false;
};

}  // namespace interop::crubit
