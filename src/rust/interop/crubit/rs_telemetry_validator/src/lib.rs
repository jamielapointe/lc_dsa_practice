//! Crubit interop Module 5: zero-allocation deterministic safety interceptor for telemetry actuation.

/// Maximum allowed command age in nanoseconds (5 ms watchdog deadline).
pub const MAX_WATCHDOG_AGE_NS: u64 = 5_000_000;
/// Maximum allowed absolute actuation torque in Newton-meters.
pub const MAX_TORQUE_NM: f64 = 80.0;
/// Authenticated manual reset token required to clear a latched `FaultEstop`.
pub const AUTH_RESET_TOKEN: u64 = 0xa5a5_5a5a_cafe_babe;

/// Fault bit: watchdog age exceeded 5 ms or `timestamp_ns > now_ns`.
pub const FAULT_WATCHDOG_EXPIRED: u32 = 1 << 0;
/// Fault bit: frame timestamp regressed below the last accepted timestamp.
pub const FAULT_NON_MONOTONIC_TIMESTAMP: u32 = 1 << 1;
/// Fault bit: torque command is non-finite or exceeds `80.0` N*m.
pub const FAULT_TORQUE_OUT_OF_RANGE: u32 = 1 << 2;
/// Fault bit: frame CRC check failed.
pub const FAULT_CRC_MISMATCH: u32 = 1 << 3;

/// Deterministic safety state machine states.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SafetyState {
    /// Initial uninitialized state.
    Init,
    /// Armed and ready to accept valid actuation frames.
    Armed,
    /// Actively forwarding validated actuation torques.
    Active,
    /// Fault latched; all torque outputs are forced to `0.0` until authenticated reset.
    FaultEstop,
}

/// Output of validating a single telemetry actuation frame.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ValidationDecision {
    /// Resulting interceptor state after processing the frame.
    pub state: SafetyState,
    /// Validated torque command in N*m (`0.0` whenever faulted or latched).
    pub safe_torque_nm: f64,
    /// Bitmask of active/latched faults (`0` when healthy).
    pub fault_mask: u32,
}

/// Zero-allocation safety command interceptor with fault latching and authenticated reset.
#[derive(Debug)]
pub struct SafetyInterceptor {
    state: SafetyState,
    last_timestamp_ns: u64,
    latched_fault_mask: u32,
}

impl SafetyInterceptor {
    /// Constructs a new [`SafetyInterceptor`] in the [`SafetyState::Armed`] state.
    #[must_use]
    pub const fn new_armed() -> Self {
        Self {
            state: SafetyState::Armed,
            last_timestamp_ns: 0,
            latched_fault_mask: 0,
        }
    }

    /// Validates an incoming actuation command against watchdog, monotonicity, torque, and CRC invariants.
    ///
    /// Any fault zeroes `safe_torque_nm` and latches [`SafetyState::FaultEstop`] until
    /// [`Self::reset_authenticated`] succeeds.
    pub fn validate_command(
        &mut self,
        timestamp_ns: u64,
        now_ns: u64,
        torque_nm: f64,
        crc_valid: bool,
    ) -> ValidationDecision {
        let mut faults = 0_u32;

        if now_ns < timestamp_ns || (now_ns - timestamp_ns) > MAX_WATCHDOG_AGE_NS {
            faults |= FAULT_WATCHDOG_EXPIRED;
        }
        if timestamp_ns < self.last_timestamp_ns {
            faults |= FAULT_NON_MONOTONIC_TIMESTAMP;
        }
        if !torque_nm.is_finite() || torque_nm.abs() > MAX_TORQUE_NM {
            faults |= FAULT_TORQUE_OUT_OF_RANGE;
        }
        if !crc_valid {
            faults |= FAULT_CRC_MISMATCH;
        }

        if self.state == SafetyState::FaultEstop || faults != 0 {
            self.state = SafetyState::FaultEstop;
            self.latched_fault_mask |= faults;
            return ValidationDecision {
                state: SafetyState::FaultEstop,
                safe_torque_nm: 0.0,
                fault_mask: self.latched_fault_mask,
            };
        }

        self.state = SafetyState::Active;
        self.last_timestamp_ns = timestamp_ns;
        ValidationDecision {
            state: SafetyState::Active,
            safe_torque_nm: torque_nm,
            fault_mask: 0,
        }
    }

    /// Clears a latched fault state iff `auth_token` matches the authenticated reset token.
    pub const fn reset_authenticated(&mut self, auth_token: u64) -> bool {
        if auth_token == AUTH_RESET_TOKEN {
            self.state = SafetyState::Armed;
            self.last_timestamp_ns = 0;
            self.latched_fault_mask = 0;
            true
        } else {
            false
        }
    }

    /// Returns the current [`SafetyState`].
    #[must_use]
    pub const fn state(&self) -> SafetyState {
        self.state
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validates_nominal_and_latches_faults_until_authenticated_reset() {
        let mut interceptor = SafetyInterceptor::new_armed();
        assert_eq!(interceptor.state(), SafetyState::Armed);

        let ok = interceptor.validate_command(1_000_000, 2_000_000, 24.5, true);
        assert_eq!(ok.state, SafetyState::Active);
        assert!((ok.safe_torque_nm - 24.5).abs() < 1e-12);
        assert_eq!(ok.fault_mask, 0);

        let fault = interceptor.validate_command(2_000_000, 2_500_000, 95.0, false);
        assert_eq!(fault.state, SafetyState::FaultEstop);
        assert!((fault.safe_torque_nm - 0.0).abs() < 1e-12);
        assert_eq!(fault.fault_mask, FAULT_TORQUE_OUT_OF_RANGE | FAULT_CRC_MISMATCH);

        // Remains latched even for valid frames.
        let latched = interceptor.validate_command(3_000_000, 3_500_000, 10.0, true);
        assert_eq!(latched.state, SafetyState::FaultEstop);
        assert!((latched.safe_torque_nm - 0.0).abs() < 1e-12);

        // Wrong token fails; right token resets to Armed.
        assert!(!interceptor.reset_authenticated(0xdead_beef));
        assert_eq!(interceptor.state(), SafetyState::FaultEstop);
        assert!(interceptor.reset_authenticated(AUTH_RESET_TOKEN));
        assert_eq!(interceptor.state(), SafetyState::Armed);

        let recovered = interceptor.validate_command(4_000_000, 4_500_000, -15.0, true);
        assert_eq!(recovered.state, SafetyState::Active);
        assert!((recovered.safe_torque_nm - (-15.0)).abs() < 1e-12);
    }
}
