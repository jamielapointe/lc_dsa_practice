#include <iostream>

#include "crubit/rs_primitives_enums.h"
#include "interop/crubit/primitives_and_enums.hpp"

auto main() -> int {
  const interop::crubit::Waypoint3d origin{
      .x_m = 0.0, .y_m = 0.0, .altitude_m = 100.0};
  const interop::crubit::Waypoint3d target{
      .x_m = 30.0, .y_m = 40.0, .altitude_m = 100.0};
  const double dist_m = interop::crubit::euclidean_distance(origin, target);
  const auto phase = interop::crubit::advance_phase(
      interop::crubit::FlightPhase::kPreflight, 50.0, 3.0);

  const rs_primitives_enums::KinematicSample sample{
      .position_m = 100.0, .velocity_mps = 12.0, .acceleration_mps2 = 2.0};
  const auto integrated =
      rs_primitives_enums::integrate_kinematics(sample, 1.5);
  const auto cmd = rs_primitives_enums::make_climb_command(6.0, 1.5);
  const double target_alt =
      rs_primitives_enums::evaluate_command_target(cmd, integrated.position_m);

  if (phase != interop::crubit::FlightPhase::kClimb) {
    return 1;
  }

  std::cout << "[CRUBIT_CPP_DEMO] module=primitives_enums status=ok dist_m="
            << dist_m << " next_pos_m=" << integrated.position_m
            << " target_alt_m=" << target_alt << '\n';
  return 0;
}
