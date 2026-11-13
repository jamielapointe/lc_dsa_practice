#include <cstdint>
#include <iostream>

#include "crubit/rs_telemetry_validator.h"
#include "interop/crubit/telemetry_safety_bridge.hpp"

auto main() -> int {
  constexpr std::uint64_t kAuthToken = 0xA5A55A5ACAFEBABEULL;
  auto interceptor = rs_telemetry_validator::SafetyInterceptor::new_armed();
  interop::crubit::ActuatorSink sink;

  const auto frame1 =
      interop::crubit::make_verified_frame(1U, 10'000'000ULL, 32.5, 1.8);
  const bool crc1_ok =
      interop::crubit::compute_frame_crc32(
          frame1.sequence_id, frame1.timestamp_ns, frame1.torque_cmd_nm,
          frame1.velocity_rad_s) == frame1.crc32;
  const auto dec1 = interceptor.validate_command(
      frame1.timestamp_ns, 11'000'000ULL, frame1.torque_cmd_nm, crc1_ok);
  if (!sink.commit(dec1.safe_torque_nm, dec1.fault_mask)) {
    return 1;
  }

  // Inject over-torque fault and verify latch + authenticated reset recovery.
  const auto frame2 =
      interop::crubit::make_verified_frame(2U, 12'000'000ULL, 110.0, 2.5);
  const auto dec2 = interceptor.validate_command(
      frame2.timestamp_ns, 13'000'000ULL, frame2.torque_cmd_nm, true);
  if (sink.commit(dec2.safe_torque_nm, dec2.fault_mask) ||
      sink.latched_fault_mask() == 0U) {
    return 1;
  }

  if (!interceptor.reset_authenticated(kAuthToken)) {
    return 1;
  }
  sink.clear_latched_fault();

  const auto frame3 =
      interop::crubit::make_verified_frame(3U, 14'000'000ULL, 24.0, 1.1);
  const auto dec3 = interceptor.validate_command(
      frame3.timestamp_ns, 15'000'000ULL, frame3.torque_cmd_nm, true);
  if (!sink.commit(dec3.safe_torque_nm, dec3.fault_mask)) {
    return 1;
  }

  std::cout
      << "[CRUBIT_CPP_DEMO] module=telemetry_capstone status=ok committed="
      << sink.committed_count()
      << " last_torque_nm=" << sink.last_safe_torque_nm() << '\n';
  return 0;
}
