#include "interop/crubit/primitives_and_enums.hpp"

#include <cmath>

namespace interop::crubit {

auto euclidean_distance(Waypoint3d origin, Waypoint3d target) noexcept
    -> double {
  const double dx = target.x_m - origin.x_m;
  const double dy = target.y_m - origin.y_m;
  const double dz = target.altitude_m - origin.altitude_m;
  return std::hypot(dx, dy, dz);
}

auto advance_phase(FlightPhase current, double altitude_m,
                   double vertical_speed_mps) noexcept -> FlightPhase {
  switch (current) {
    case FlightPhase::kPreflight:
      if (altitude_m > 10.0 && vertical_speed_mps > 0.5) {
        return FlightPhase::kClimb;
      }
      return FlightPhase::kPreflight;
    case FlightPhase::kClimb:
      if (vertical_speed_mps < -1.0) {
        return FlightPhase::kApproach;
      }
      if (altitude_m >= 3000.0 && vertical_speed_mps >= -1.0 &&
          vertical_speed_mps <= 1.0) {
        return FlightPhase::kCruise;
      }
      return FlightPhase::kClimb;
    case FlightPhase::kCruise:
      if (vertical_speed_mps < -0.5) {
        return FlightPhase::kApproach;
      }
      return FlightPhase::kCruise;
    case FlightPhase::kApproach:
      if (altitude_m <= 1.0 && vertical_speed_mps >= -0.5 &&
          vertical_speed_mps <= 0.5) {
        return FlightPhase::kLanded;
      }
      return FlightPhase::kApproach;
    case FlightPhase::kLanded:
      return FlightPhase::kLanded;
  }
  return FlightPhase::kPreflight;
}

}  // namespace interop::crubit
