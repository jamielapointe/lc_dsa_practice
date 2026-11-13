#pragma once

/// @file traits_and_errors_expected.hpp
/// @brief C++23-only `std::expected` adapter for `CalibrationOutcome` (not
/// parsed by Crubit).

#include <expected>

#include "interop/crubit/traits_and_errors.hpp"

namespace interop::crubit {

/// @brief Converts a Crubit-compatible `CalibrationOutcome` into
/// `std::expected`.
[[nodiscard]] inline auto to_expected(CalibrationOutcome outcome) noexcept
    -> std::expected<GainVector2d, CalibrationCode> {
  if (outcome.code == CalibrationCode::kOk) {
    return outcome.gains;
  }
  return std::unexpected{outcome.code};
}

}  // namespace interop::crubit
