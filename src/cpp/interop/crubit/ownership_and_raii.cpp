#include "interop/crubit/ownership_and_raii.hpp"

#include <atomic>

namespace interop::crubit {

namespace {
auto active_lease_counter() noexcept -> std::atomic<int>& {
  static std::atomic<int> counter{0};
  return counter;
}
}  // namespace

LeaseGuard::LeaseGuard(int lease_id) noexcept
    : lease_id_(lease_id), epochs_remaining_(1), active_(true) {
  active_lease_counter().fetch_add(1, std::memory_order_relaxed);
}

LeaseGuard::LeaseGuard(LeaseGuard&& other) noexcept
    : lease_id_(other.lease_id_),
      epochs_remaining_(other.epochs_remaining_),
      active_(other.active_) {
  other.lease_id_ = -1;
  other.epochs_remaining_ = 0;
  other.active_ = false;
}

auto LeaseGuard::operator=(LeaseGuard&& other) noexcept -> LeaseGuard& {
  if (this == &other) {
    return *this;
  }
  if (active_) {
    active_lease_counter().fetch_sub(1, std::memory_order_acq_rel);
  }
  lease_id_ = other.lease_id_;
  epochs_remaining_ = other.epochs_remaining_;
  active_ = other.active_;
  other.lease_id_ = -1;
  other.epochs_remaining_ = 0;
  other.active_ = false;
  return *this;
}

LeaseGuard::~LeaseGuard() noexcept {
  if (active_) {
    active_lease_counter().fetch_sub(1, std::memory_order_acq_rel);
  }
}

auto LeaseGuard::lease_id() const noexcept -> int { return lease_id_; }

auto LeaseGuard::renew(int extra_epochs) noexcept -> int {
  if (active_ && extra_epochs > 0) {
    epochs_remaining_ += extra_epochs;
  }
  return epochs_remaining_;
}

auto LeaseGuard::epochs_remaining() const noexcept -> int {
  return epochs_remaining_;
}

auto LeaseGuard::active_lease_count() noexcept -> int {
  return active_lease_counter().load(std::memory_order_acquire);
}

}  // namespace interop::crubit
