#include <cstddef>
#include <cstdint>
#include <print>

#include "crubit/rs_ownership_raii.h"
#include "interop/crubit/ownership_and_raii.hpp"
#include "support/rs_std/str_ref.h"

auto main() -> int {
  size_t peak_buffers = 0uz;
  size_t bytes_appended = 0uz;
  uint32_t checksum = 0U;

  {
    interop::crubit::LeaseGuard cpp_lease(42);
    auto rust_packet = rs_ownership_raii::TelemetryPacketBuffer::create(
        rs::StrRef{"telemetry_demo"}, 32);
    bytes_appended = rust_packet.append_byte(0xABU);
    bytes_appended = rust_packet.append_byte(0x0FU);
    checksum = rust_packet.checksum();
    peak_buffers = rs_ownership_raii::live_packet_buffers();
    if (interop::crubit::LeaseGuard::active_lease_count() != 1 ||
        peak_buffers != 1uz) {
      return 1;
    }
  }

  const auto remaining_leases =
      interop::crubit::LeaseGuard::active_lease_count();
  const auto remaining_buffers = rs_ownership_raii::live_packet_buffers();
  if (remaining_leases != 0 || remaining_buffers != 0uz) {
    return 1;
  }

  std::println(
      "[CRUBIT_CPP_DEMO] module=ownership_raii status=ok peak_buffers={} "
      "bytes={} checksum={} leaked={}",
      peak_buffers, bytes_appended, checksum, remaining_buffers);
  return 0;
}
