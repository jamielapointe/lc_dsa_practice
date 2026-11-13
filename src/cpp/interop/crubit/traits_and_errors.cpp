#include "interop/crubit/traits_and_errors.hpp"

#include <cmath>
#include <numbers>

namespace interop::crubit {

auto GainVector2d::scaled(double factor) const noexcept -> GainVector2d {
  return GainVector2d{
      .kp = kp * factor,
      .kd = kd * factor,
  };
}

auto calibrate_pd_controller(double natural_freq_hz,
                             double damping_ratio) noexcept
    -> CalibrationOutcome {
  constexpr double kMaxFreqHz = 1000.0;
  constexpr double kMaxDamping = 10.0;
  constexpr double kSingularityEps = 1e-9;

  if (!std::isfinite(natural_freq_hz) || !std::isfinite(damping_ratio) ||
      natural_freq_hz < 0.0 || natural_freq_hz > kMaxFreqHz ||
      damping_ratio < 0.0 || damping_ratio > kMaxDamping) {
    return CalibrationOutcome{
        .code = CalibrationCode::kOutOfRange,
        .gains = GainVector2d{},
    };
  }

  if (natural_freq_hz <= kSingularityEps || damping_ratio <= kSingularityEps) {
    return CalibrationOutcome{
        .code = CalibrationCode::kSingular,
        .gains = GainVector2d{},
    };
  }

  const double omega_n = 2.0 * std::numbers::pi * natural_freq_hz;
  return CalibrationOutcome{
      .code = CalibrationCode::kOk,
      .gains =
          GainVector2d{
              .kp = omega_n * omega_n,
              .kd = 2.0 * damping_ratio * omega_n,
          },
  };
}

}  // namespace interop::crubit
