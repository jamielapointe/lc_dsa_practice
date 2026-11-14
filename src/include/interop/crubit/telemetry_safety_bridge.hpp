#pragma once

/// @file telemetry_safety_bridge.hpp
/// @brief Capstone C++23 sensor frame builder, IEEE 802.3 CRC-32, and latching
/// actuator sink.

namespace interop::crubit {

/// @brief Telemetry command frame transferred between C++23 and Rust 2024.
struct SensorFrame {
  unsigned int sequence_id = 0U;
  // Built-in unsigned long long avoids <cstdint>/cc_std dependencies in Crubit
  // CMake mode.
  unsigned long long timestamp_ns = 0ULL;  // NOLINT(google-runtime-int)
  double torque_cmd_nm = 0.0;
  double velocity_rad_s = 0.0;
  unsigned int crc32 = 0U;
};

/// @brief Computes IEEE 802.3 CRC-32 over the serialized little-endian frame
/// payload.
[[nodiscard]] auto compute_frame_crc32(
    unsigned int sequence_id,
    // Built-in type required for Crubit CMake mode.
    unsigned long long timestamp_ns,  // NOLINT(google-runtime-int)
    double torque_cmd_nm, double velocity_rad_s) noexcept -> unsigned int;

/// @brief Constructs a `SensorFrame` with `crc32` populated by
/// `compute_frame_crc32`.
[[nodiscard]] auto make_verified_frame(
    unsigned int sequence_id,
    // Built-in type required for Crubit CMake mode.
    unsigned long long timestamp_ns,  // NOLINT(google-runtime-int)
    double torque_cmd_nm, double velocity_rad_s) noexcept -> SensorFrame;

/// @brief Deterministic actuator sink that latches faults and zeroes torque
/// until cleared.
class ActuatorSink {
 public:
  ActuatorSink() noexcept = default;

  /// @brief Returns the number of fault-free frames committed to the actuator.
  [[nodiscard]] auto committed_count() const noexcept -> unsigned int;

  /// @brief Returns the cumulative latched fault bitmask (0 when healthy).
  [[nodiscard]] auto latched_fault_mask() const noexcept -> unsigned int;

  /// @brief Returns the last committed safe torque in N*m (0.0 on fault or
  /// latch).
  [[nodiscard]] auto last_safe_torque_nm() const noexcept -> double;

  /// @brief Commits a validated torque command if `fault_mask == 0` and no
  /// fault is latched. Returns `true` if committed, `false` if
  /// rejected/latched.
  auto commit(double safe_torque_nm, unsigned int fault_mask) noexcept -> bool;

  /// @brief Clears the latched fault mask after authenticated supervisor reset.
  auto clear_latched_fault() noexcept -> void;

 private:
  unsigned int committed_count_ = 0U;
  unsigned int latched_fault_mask_ = 0U;
  double last_safe_torque_nm_ = 0.0;
};

}  // namespace interop::crubit
