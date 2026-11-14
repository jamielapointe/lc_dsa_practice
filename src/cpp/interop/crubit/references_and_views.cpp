#include "interop/crubit/references_and_views.hpp"

namespace interop::crubit {

namespace {
constexpr int kWindowCapacity = 8;
}  // namespace

SignalWindow::SignalWindow(double initial_value) noexcept : count_(1) {
  samples_[0] = initial_value;
}

auto SignalWindow::push_sample(double sample) noexcept -> void {
  if (count_ < kWindowCapacity) {
    samples_[count_] = sample;
    ++count_;
    return;
  }
  for (int idx = 1; idx < kWindowCapacity; ++idx) {
    samples_[idx - 1] = samples_[idx];
  }
  samples_[kWindowCapacity - 1] = sample;
}

auto SignalWindow::peak_ref() const noexcept -> const double& {
  int peak_index = 0;
  for (int idx = 1; idx < count_; ++idx) {
    if (samples_[idx] > samples_[peak_index]) {
      peak_index = idx;
    }
  }
  return samples_[peak_index];
}

auto SignalWindow::count() const noexcept -> int { return count_; }

auto select_dominant_window(const SignalWindow& lhs [[clang::lifetimebound]],
                            const SignalWindow& rhs
                            [[clang::lifetimebound]]) noexcept
    -> const SignalWindow& {
  return lhs.peak_ref() >= rhs.peak_ref() ? lhs : rhs;
}

auto scale_in_place(double& value, double factor) noexcept -> void {
  value *= factor;
}

}  // namespace interop::crubit
