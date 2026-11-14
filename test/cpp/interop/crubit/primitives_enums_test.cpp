#include <cmath>

#include "crubit/rs_primitives_enums.h"
#include "interop/crubit/primitives_and_enums.hpp"
#include "interop_test_harness.hpp"

auto main() -> int {
  interop::crubit::test::TestContext ctx;

  // Test C++ primitives & enums directly.
  const interop::crubit::Waypoint3d origin{
      .x_m = 0.0, .y_m = 0.0, .altitude_m = 0.0};
  const interop::crubit::Waypoint3d target{
      .x_m = 3.0, .y_m = 4.0, .altitude_m = 12.0};
  INTEROP_EXPECT_NEAR(ctx, interop::crubit::euclidean_distance(origin, target),
                      13.0, 1e-9);
  INTEROP_EXPECT_EQ(ctx,
                    interop::crubit::advance_phase(
                        interop::crubit::FlightPhase::kPreflight, 25.0, 2.0),
                    interop::crubit::FlightPhase::kClimb);

  // Test Rust KinematicSample + integrate_kinematics across Crubit.
  const rs_primitives_enums::KinematicSample initial{
      .position_m = 100.0, .velocity_mps = 10.0, .acceleration_mps2 = 2.0};
  const rs_primitives_enums::KinematicSample next =
      rs_primitives_enums::integrate_kinematics(initial, 2.0);
  // Kinematic integration: p' = p + v*dt + 0.5*a*dt^2 = 100 + 20 + 4 = 124; v'
  // = 10 + 4 = 14.
  INTEROP_EXPECT_NEAR(ctx, next.position_m, 124.0, 1e-9);
  INTEROP_EXPECT_NEAR(ctx, next.velocity_mps, 14.0, 1e-9);
  INTEROP_EXPECT_NEAR(ctx, next.acceleration_mps2, 2.0, 1e-9);

  const rs_primitives_enums::KinematicSample copy_next = next;
  INTEROP_EXPECT_TRUE(ctx, next == copy_next);

  // Test Rust ControlCommand payload enum + evaluate_command_target.
  const auto hold_cmd =
      rs_primitives_enums::ControlCommand::HoldAltitude::Make(1500.0);
  INTEROP_EXPECT_NEAR(
      ctx, rs_primitives_enums::evaluate_command_target(hold_cmd, 1200.0),
      1500.0, 1e-9);

  const auto climb_cmd = rs_primitives_enums::make_climb_command(5.5, 1.8);
  INTEROP_EXPECT_TRUE(
      ctx, std::isfinite(rs_primitives_enums::evaluate_command_target(climb_cmd,
                                                                      1200.0)));

  const auto abort_cmd = rs_primitives_enums::ControlCommand::Abort::Make(7);
  INTEROP_EXPECT_NEAR(
      ctx, rs_primitives_enums::evaluate_command_target(abort_cmd, 1200.0),
      1200.0, 1e-9);

  return ctx.exit_code();
}
