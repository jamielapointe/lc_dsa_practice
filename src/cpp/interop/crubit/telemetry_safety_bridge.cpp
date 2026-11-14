#include "interop/crubit/telemetry_safety_bridge.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace interop::crubit {

namespace {

constexpr uint32_t kCrc32Poly = 0xEDB88320U;
constexpr uint32_t kNonFiniteFaultBit = 0x80000000U;

auto crc32_update_byte(uint32_t crc, uint8_t byte) noexcept -> uint32_t {
  crc ^= static_cast<uint32_t>(byte);
  for (int bit = 0; bit < 8; ++bit) {
    const uint32_t mask = (crc & 1U) != 0U ? 0xFFFFFFFFU : 0U;
    crc = (crc >> 1U) ^ (kCrc32Poly & mask);
  }
  return crc;
}

template <size_t ByteCount>
void append_le_bytes(uint64_t value, std::array<uint8_t, 28>& out,
                     size_t offset) noexcept {
  for (size_t idx = 0; idx < ByteCount; ++idx) {
    out.at(offset + idx) = static_cast<uint8_t>((value >> (8U * idx)) & 0xFFU);
  }
}

}  // namespace

auto compute_frame_crc32(uint32_t sequence_id, uint64_t timestamp_ns,
                         double torque_cmd_nm, double velocity_rad_s) noexcept
    -> uint32_t {
  std::array<uint8_t, 28> payload{};
  append_le_bytes<4>(static_cast<uint64_t>(sequence_id), payload, 0);
  append_le_bytes<8>(timestamp_ns, payload, 4);
  append_le_bytes<8>(std::bit_cast<uint64_t>(torque_cmd_nm), payload, 12);
  append_le_bytes<8>(std::bit_cast<uint64_t>(velocity_rad_s), payload, 20);

  uint32_t crc = 0xFFFFFFFFU;
  for (const uint8_t byte : payload) {
    crc = crc32_update_byte(crc, byte);
  }
  return crc ^ 0xFFFFFFFFU;
}

auto make_verified_frame(uint32_t sequence_id, uint64_t timestamp_ns,
                         double torque_cmd_nm, double velocity_rad_s) noexcept
    -> SensorFrame {
  return SensorFrame{
      .sequence_id = sequence_id,
      .timestamp_ns = timestamp_ns,
      .torque_cmd_nm = torque_cmd_nm,
      .velocity_rad_s = velocity_rad_s,
      .crc32 = compute_frame_crc32(sequence_id, timestamp_ns, torque_cmd_nm,
                                   velocity_rad_s),
  };
}

auto ActuatorSink::committed_count() const noexcept -> uint32_t {
  return committed_count_;
}

auto ActuatorSink::latched_fault_mask() const noexcept -> uint32_t {
  return latched_fault_mask_;
}

auto ActuatorSink::last_safe_torque_nm() const noexcept -> double {
  return last_safe_torque_nm_;
}

auto ActuatorSink::commit(double safe_torque_nm, uint32_t fault_mask) noexcept
    -> bool {
  if (fault_mask != 0U) {
    latched_fault_mask_ |= fault_mask;
    last_safe_torque_nm_ = 0.0;
    return false;
  }
  if (latched_fault_mask_ != 0U) {
    last_safe_torque_nm_ = 0.0;
    return false;
  }
  if (!std::isfinite(safe_torque_nm)) {
    latched_fault_mask_ |= kNonFiniteFaultBit;
    last_safe_torque_nm_ = 0.0;
    return false;
  }
  last_safe_torque_nm_ = safe_torque_nm;
  ++committed_count_;
  return true;
}

auto ActuatorSink::clear_latched_fault() noexcept -> void {
  latched_fault_mask_ = 0U;
}

}  // namespace interop::crubit
