#pragma once

/// @file traits_and_errors.hpp
/// @brief Value equality and FFI-safe calibration error outcomes exported to
/// Rust 2024.

namespace interop::crubit {

/// @brief 2-DOF proportional-derivative controller gains.
struct GainVector2d {
  double kp = 0.0;
  double kd = 0.0;

  /// @brief Value equality comparison.
  friend auto operator==(const GainVector2d&, const GainVector2d&)
      -> bool = default;

  /// @brief Returns a copy of the gain vector scaled by `factor`.
  [[nodiscard]] auto scaled(double factor) const noexcept -> GainVector2d;
};

/// @brief Status code produced by PD controller calibration.
// Underlying int matches C ABI / Crubit enum mapping without <cstdint> in
// public headers.
enum class CalibrationCode : int {  // NOLINT(performance-enum-size)
  kOk = 0,
  kOutOfRange = 1,
  kSingular = 2,
};

/// @brief Calibration result pairing `CalibrationCode` with computed
/// `GainVector2d`.
struct CalibrationOutcome {
  CalibrationCode code = CalibrationCode::kOk;
  GainVector2d gains{};
};

/// @brief Computes PD controller gains for unit inertia from natural frequency
/// and damping ratio.
[[nodiscard]] auto calibrate_pd_controller(double natural_freq_hz,
                                           double damping_ratio) noexcept
    -> CalibrationOutcome;

}  // namespace interop::crubit
