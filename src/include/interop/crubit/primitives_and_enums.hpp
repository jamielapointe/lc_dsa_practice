#pragma once

/// @file primitives_and_enums.hpp
/// @brief C++23 POD structs, scoped enums, and pure functions exported to Rust
/// 2024 via Crubit.

namespace interop::crubit {

/// @brief Flight phase state used across the C++23 <-> Rust 2024 boundary.
// Underlying int matches C ABI / Crubit enum mapping without <cstdint> in
// public headers.
enum class FlightPhase : int {  // NOLINT(performance-enum-size)
  kPreflight = 0,
  kClimb = 1,
  kCruise = 2,
  kApproach = 3,
  kLanded = 4,
};

/// @brief 3D Cartesian waypoint in local NED/ENU meters.
struct Waypoint3d {
  double x_m = 0.0;
  double y_m = 0.0;
  double altitude_m = 0.0;
};

/// @brief Computes 3D Euclidean distance between two waypoints in meters.
[[nodiscard]] auto euclidean_distance(Waypoint3d origin,
                                      Waypoint3d target) noexcept -> double;

/// @brief Transitions the flight phase based on altitude (m) and vertical speed
/// (m/s).
[[nodiscard]] auto advance_phase(FlightPhase current, double altitude_m,
                                 double vertical_speed_mps) noexcept
    -> FlightPhase;

}  // namespace interop::crubit
