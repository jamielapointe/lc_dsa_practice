#include <cstdint>

#include "crubit/rs_telemetry_validator.h"
#include "interop/crubit/telemetry_safety_bridge.hpp"
#include "interop_test_harness.hpp"

auto main() -> int {
  interop::crubit::test::TestContext ctx;

  constexpr uint64_t kValidAuthToken = 0xA5A55A5ACAFEBABEU;
  constexpr uint64_t kWrongAuthToken = kValidAuthToken ^ 0x1U;

  auto interceptor = rs_telemetry_validator::SafetyInterceptor::new_armed();
  INTEROP_EXPECT_TRUE(ctx,
                      interceptor.state() ==
                          rs_telemetry_validator::SafetyState::Armed::Make());

  interop::crubit::ActuatorSink sink;

  // 1. Healthy telemetry frame: C++ builds & signs -> Rust validates -> C++
  // actuator commits.
  const auto frame1 =
      interop::crubit::make_verified_frame(1U, uint64_t{10'000'000}, 25.0, 1.5);
  const bool crc1_ok =
      interop::crubit::compute_frame_crc32(
          frame1.sequence_id, frame1.timestamp_ns, frame1.torque_cmd_nm,
          frame1.velocity_rad_s) == frame1.crc32;
  INTEROP_EXPECT_TRUE(ctx, crc1_ok);

  const auto dec1 = interceptor.validate_command(
      frame1.timestamp_ns, uint64_t{11'000'000}, frame1.torque_cmd_nm, crc1_ok);
  INTEROP_EXPECT_TRUE(
      ctx, dec1.state == rs_telemetry_validator::SafetyState::Active::Make());
  INTEROP_EXPECT_EQ(ctx, dec1.fault_mask, 0U);
  INTEROP_EXPECT_NEAR(ctx, dec1.safe_torque_nm, 25.0, 1e-9);
  INTEROP_EXPECT_TRUE(ctx, sink.commit(dec1.safe_torque_nm, dec1.fault_mask));
  INTEROP_EXPECT_EQ(ctx, sink.committed_count(), 1U);
  INTEROP_EXPECT_NEAR(ctx, sink.last_safe_torque_nm(), 25.0, 1e-9);

  // 2. Fault injection: over-torque (> 80.0 N*m) latches FaultEstop on both
  // sides.
  const auto frame2 = interop::crubit::make_verified_frame(
      2U, uint64_t{12'000'000}, 120.0, 2.0);
  const auto dec2 = interceptor.validate_command(
      frame2.timestamp_ns, uint64_t{13'000'000}, frame2.torque_cmd_nm, true);
  INTEROP_EXPECT_TRUE(
      ctx,
      dec2.state == rs_telemetry_validator::SafetyState::FaultEstop::Make());
  INTEROP_EXPECT_TRUE(ctx, dec2.fault_mask != 0U);
  INTEROP_EXPECT_NEAR(ctx, dec2.safe_torque_nm, 0.0, 1e-9);
  INTEROP_EXPECT_TRUE(ctx, !sink.commit(dec2.safe_torque_nm, dec2.fault_mask));
  INTEROP_EXPECT_TRUE(ctx, sink.latched_fault_mask() != 0U);
  INTEROP_EXPECT_NEAR(ctx, sink.last_safe_torque_nm(), 0.0, 1e-9);

  // 3. Subsequent healthy frame remains blocked while fault is latched.
  const auto frame3 =
      interop::crubit::make_verified_frame(3U, uint64_t{14'000'000}, 15.0, 1.0);
  const auto dec3 = interceptor.validate_command(
      frame3.timestamp_ns, uint64_t{15'000'000}, frame3.torque_cmd_nm, true);
  INTEROP_EXPECT_TRUE(
      ctx,
      dec3.state == rs_telemetry_validator::SafetyState::FaultEstop::Make());
  INTEROP_EXPECT_TRUE(ctx, !sink.commit(dec3.safe_torque_nm, dec3.fault_mask));
  INTEROP_EXPECT_EQ(ctx, sink.committed_count(), 1U);

  // 4. Unauthenticated reset fails; authenticated reset clears both Rust and
  // C++ latches.
  INTEROP_EXPECT_TRUE(ctx, !interceptor.reset_authenticated(kWrongAuthToken));
  INTEROP_EXPECT_TRUE(ctx, interceptor.reset_authenticated(kValidAuthToken));
  INTEROP_EXPECT_TRUE(ctx,
                      interceptor.state() ==
                          rs_telemetry_validator::SafetyState::Armed::Make());
  sink.clear_latched_fault();
  INTEROP_EXPECT_EQ(ctx, sink.latched_fault_mask(), 0U);

  // 5. Recovery: new verified frame succeeds.
  const auto frame4 =
      interop::crubit::make_verified_frame(4U, uint64_t{16'000'000}, 18.5, 1.2);
  const auto dec4 = interceptor.validate_command(
      frame4.timestamp_ns, uint64_t{17'000'000}, frame4.torque_cmd_nm, true);
  INTEROP_EXPECT_TRUE(
      ctx, dec4.state == rs_telemetry_validator::SafetyState::Active::Make());
  INTEROP_EXPECT_TRUE(ctx, sink.commit(dec4.safe_torque_nm, dec4.fault_mask));
  INTEROP_EXPECT_EQ(ctx, sink.committed_count(), 2U);
  INTEROP_EXPECT_NEAR(ctx, sink.last_safe_torque_nm(), 18.5, 1e-9);

  return ctx.exit_code();
}
