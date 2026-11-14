//! Crubit interop Module 1: Rust 2024 primitives, `Copy` structs, and payload enums.

/// One-axis kinematic state sample exported to C++23 by value.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct KinematicSample {
    /// Longitudinal position in meters.
    pub position_m: f64,
    /// Velocity in meters per second.
    pub velocity_mps: f64,
    /// Constant acceleration over the step in meters per second squared.
    pub acceleration_mps2: f64,
}

/// Discriminated control command with per-variant payload exported via Crubit.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum ControlCommand {
    /// Hold a target altitude in meters.
    HoldAltitude(f64),
    /// Climb at `target_mps` subject to `max_g` acceleration limit.
    ClimbRate {
        /// Desired vertical climb rate in meters per second.
        target_mps: f64,
        /// Maximum allowed vertical acceleration in g.
        max_g: f64,
    },
    /// Abort maneuver with a fault/reason code.
    Abort {
        /// Subsystem abort reason code.
        code: i32,
    },
}

/// Integrates a [`KinematicSample`] forward by `dt_sec` under constant acceleration.
///
/// Uses exact constant-acceleration kinematics:
/// `v' = v + a * dt` and `p' = p + v * dt + 0.5 * a * dt^2`.
#[must_use]
pub fn integrate_kinematics(sample: KinematicSample, dt_sec: f64) -> KinematicSample {
    let velocity_mps = sample.acceleration_mps2.mul_add(dt_sec, sample.velocity_mps);
    let position_m = (0.5 * sample.acceleration_mps2 * dt_sec)
        .mul_add(dt_sec, sample.velocity_mps.mul_add(dt_sec, sample.position_m));
    KinematicSample {
        position_m,
        velocity_mps,
        acceleration_mps2: sample.acceleration_mps2,
    }
}

/// Constructs a [`ControlCommand::ClimbRate`] command.
#[must_use]
pub const fn make_climb_command(target_mps: f64, max_g: f64) -> ControlCommand {
    ControlCommand::ClimbRate { target_mps, max_g }
}

/// Evaluates the target altitude in meters for `cmd` given `current_altitude_m`.
///
/// - `HoldAltitude(alt)` returns `alt`.
/// - `ClimbRate { target_mps, max_g }` returns `current_altitude_m + target_mps * max_g.max(0.0)`.
/// - `Abort { .. }` holds safely at `current_altitude_m`.
#[must_use]
pub const fn evaluate_command_target(cmd: &ControlCommand, current_altitude_m: f64) -> f64 {
    match *cmd {
        ControlCommand::HoldAltitude(altitude_m) => altitude_m,
        ControlCommand::ClimbRate { target_mps, max_g } => target_mps.mul_add(max_g.max(0.0), current_altitude_m),
        ControlCommand::Abort { code: _ } => current_altitude_m,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn integrates_constant_acceleration() {
        let initial = KinematicSample {
            position_m: 10.0,
            velocity_mps: 4.0,
            acceleration_mps2: 2.0,
        };
        let next = integrate_kinematics(initial, 3.0);
        assert!((next.position_m - 31.0).abs() < 1e-12);
        assert!((next.velocity_mps - 10.0).abs() < 1e-12);
        assert!((next.acceleration_mps2 - 2.0).abs() < 1e-12);
    }

    #[test]
    fn evaluates_all_command_variants() {
        let hold = ControlCommand::HoldAltitude(150.0);
        assert!((evaluate_command_target(&hold, 100.0) - 150.0).abs() < 1e-12);

        let climb = make_climb_command(5.0, 1.5);
        assert!((evaluate_command_target(&climb, 100.0) - 107.5).abs() < 1e-12);

        let abort = ControlCommand::Abort { code: 42 };
        assert!((evaluate_command_target(&abort, 100.0) - 100.0).abs() < 1e-12);
    }
}
