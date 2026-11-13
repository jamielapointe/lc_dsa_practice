#pragma once

/// @file references_and_views.hpp
/// @brief Lifetime-annotated C++23 references and fixed inline storage exported
/// to Rust 2024.

namespace interop::crubit {

/// @brief Fixed-capacity sliding window of up to 8 signal samples with inline
/// storage.
class SignalWindow {
 public:
  /// @brief Initializes the window with one starting sample.
  explicit SignalWindow(double initial_value) noexcept;

  /// @brief Appends a sample, shifting out the oldest sample when full
  /// (capacity 8).
  auto push_sample(double sample) noexcept -> void;

  /// @brief Returns a reference to the maximum sample currently in the window.
  /// Lifetime is bound to `this`.
  [[nodiscard]] auto peak_ref() const noexcept [[clang::lifetimebound]] -> const
      double&;

  /// @brief Returns the number of samples currently stored (1..=8).
  [[nodiscard]] auto count() const noexcept -> int;

 private:
  // Fixed inline array avoids std::array/cc_std dependencies in Crubit CMake
  // mode.
  double samples_[8]{};  // NOLINT(modernize-avoid-c-arrays)
  int count_ = 0;
};

/// @brief Selects the window whose peak sample is greater (returns `lhs` on
/// ties).
[[nodiscard]] auto select_dominant_window(const SignalWindow& lhs
                                          [[clang::lifetimebound]],
                                          const SignalWindow& rhs
                                          [[clang::lifetimebound]]) noexcept
    -> const SignalWindow&;

/// @brief Scales a caller-owned scalar in place by `factor`.
auto scale_in_place(double& value, double factor) noexcept -> void;

}  // namespace interop::crubit
