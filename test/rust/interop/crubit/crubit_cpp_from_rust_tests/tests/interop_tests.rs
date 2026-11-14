//! Integration tests calling all 5 C++23 libraries from Rust 2024 via Google Crubit.

use core::sync::atomic::AtomicBool;
use core::sync::atomic::Ordering;

use cc_ownership_raii::interop::crubit::LeaseGuard;
use cc_primitives_enums::interop::crubit::FlightPhase;
use cc_primitives_enums::interop::crubit::Waypoint3d;
use cc_primitives_enums::interop::crubit::advance_phase;
use cc_primitives_enums::interop::crubit::euclidean_distance;
use cc_references_views::interop::crubit::SignalWindow;
use cc_references_views::interop::crubit::scale_in_place;
use cc_telemetry_bridge::interop::crubit::ActuatorSink;
use cc_telemetry_bridge::interop::crubit::compute_frame_crc32;
use cc_telemetry_bridge::interop::crubit::make_verified_frame;
use cc_traits_errors::interop::crubit::CalibrationCode;
use cc_traits_errors::interop::crubit::GainVector2d;
use cc_traits_errors::interop::crubit::calibrate_pd_controller;
use crubit_cpp_from_rust_tests::dominant_window_summary;
use crubit_cpp_from_rust_tests::read_window_peak;
use ctor::Assign;
use ctor::CtorNew;
use rs_telemetry_validator::AUTH_RESET_TOKEN;
use rs_telemetry_validator::FAULT_CRC_MISMATCH;
use rs_telemetry_validator::FAULT_TORQUE_OUT_OF_RANGE;
use rs_telemetry_validator::FAULT_WATCHDOG_EXPIRED;
use rs_telemetry_validator::SafetyInterceptor;
use rs_telemetry_validator::SafetyState;

#[test]
fn primitives_and_enums_roundtrip() {
    let origin = Waypoint3d::default();
    let target = Waypoint3d {
        x_m: 3.0,
        y_m: 4.0,
        altitude_m: 12.0,
    };
    assert!((euclidean_distance(origin, target) - 13.0).abs() < 1e-12);

    let climb = advance_phase(FlightPhase::kPreflight, 25.0, 2.0);
    assert_eq!(climb, FlightPhase::kClimb);
    let cruise = advance_phase(climb, 3200.0, 0.2);
    assert_eq!(cruise, FlightPhase::kCruise);
    let approach = advance_phase(cruise, 1500.0, -2.0);
    assert_eq!(approach, FlightPhase::kApproach);
    let landed = advance_phase(approach, 0.5, -0.1);
    assert_eq!(landed, FlightPhase::kLanded);
}

#[test]
fn references_and_views_lifetime_bound_queries() {
    let mut win_a = SignalWindow::from(2.0);
    for sample in [5.0, 12.5, 4.0, 6.0, 7.0, 8.0, 9.0, 3.0] {
        win_a.push_sample(sample);
    }
    // Capacity is 8: 2.0 was shifted out, 12.5 remains peak.
    assert_eq!(win_a.count(), 8);
    assert!((read_window_peak(&win_a) - 12.5).abs() < 1e-12);

    let mut win_b = SignalWindow::from(10.0);
    win_b.push_sample(20.0);
    let (dom_peak, dom_count) = dominant_window_summary(&win_a, &win_b);
    assert!((dom_peak - 20.0).abs() < 1e-12);
    assert_eq!(dom_count, 2);

    let mut scalar = -6.0_f64;
    scale_in_place(&mut scalar, 1.5);
    assert!((scalar - (-9.0)).abs() < 1e-12);
}

#[test]
fn ownership_and_raii_drop_and_move_semantics() {
    // Serialize LeaseGuard counter assertions across parallel test threads.
    static LOCK: AtomicBool = AtomicBool::new(false);
    while LOCK
        .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
        .is_err()
    {
        core::hint::spin_loop();
    }

    let baseline = LeaseGuard::active_lease_count();
    {
        let mut first = ctor::emplace!(LeaseGuard::ctor_new(ffi_11::new_c_int(101)));
        assert_eq!(LeaseGuard::active_lease_count(), baseline + 1);
        assert_eq!(first.lease_id(), 101);
        assert_eq!(first.as_mut().renew(ffi_11::new_c_int(4)), 5);
        assert_eq!(first.epochs_remaining(), 5);

        let mut moved = ctor::emplace!(LeaseGuard::ctor_new(ctor::mov!(first.as_mut())));
        // Move leaves `first` inactive and transfers ownership without changing active count.
        assert_eq!(LeaseGuard::active_lease_count(), baseline + 1);
        assert_eq!(first.lease_id(), -1);
        assert_eq!(first.epochs_remaining(), 0);
        assert_eq!(moved.lease_id(), 101);
        assert_eq!(moved.epochs_remaining(), 5);

        let mut dst = ctor::emplace!(LeaseGuard::ctor_new(ffi_11::new_c_int(202)));
        assert_eq!(LeaseGuard::active_lease_count(), baseline + 2);
        dst.as_mut().assign(ctor::mov!(moved.as_mut()));
        // Overwriting `dst` releases lease 202 immediately.
        assert_eq!(LeaseGuard::active_lease_count(), baseline + 1);
        assert_eq!(dst.lease_id(), 101);
    }
    assert_eq!(LeaseGuard::active_lease_count(), baseline);
    LOCK.store(false, Ordering::Release);
}

#[test]
fn traits_and_errors_equality_and_calibration() {
    let ok = calibrate_pd_controller(4.0, 0.8);
    assert_eq!(ok.code, CalibrationCode::kOk);
    assert!(ok.gains.kp > 0.0);
    assert!(ok.gains.kd > 0.0);

    let scaled_a = ok.gains.scaled(0.5);
    let scaled_b = GainVector2d {
        kp: ok.gains.kp * 0.5,
        kd: ok.gains.kd * 0.5,
    };
    assert!(scaled_a == scaled_b);

    let singular = calibrate_pd_controller(0.0, 0.8);
    assert_eq!(singular.code, CalibrationCode::kSingular);
    assert!(singular.gains == GainVector2d::default());

    let out_of_range = calibrate_pd_controller(2500.0, 0.8);
    assert_eq!(out_of_range.code, CalibrationCode::kOutOfRange);
}

#[test]
fn telemetry_capstone_fault_latch_and_recovery() {
    let mut interceptor = SafetyInterceptor::new_armed();
    let mut sink = ActuatorSink::default();

    let frame1 = make_verified_frame(1, 10_000_000, 18.5, 0.9);
    let crc1_ok = compute_frame_crc32(
        frame1.sequence_id,
        frame1.timestamp_ns,
        frame1.torque_cmd_nm,
        frame1.velocity_rad_s,
    ) == frame1.crc32;
    assert!(crc1_ok);

    let dec1 = interceptor.validate_command(
        frame1.timestamp_ns,
        12_000_000,
        frame1.torque_cmd_nm,
        crc1_ok,
    );
    assert_eq!(dec1.state, SafetyState::Active);
    assert!(sink.commit(dec1.safe_torque_nm, dec1.fault_mask));
    assert_eq!(sink.committed_count(), 1);

    // Trigger combined watchdog expiry, over-torque, and CRC mismatch.
    let frame2 = make_verified_frame(2, 11_000_000, 120.0, 1.2);
    let dec2 = interceptor.validate_command(
        frame2.timestamp_ns,
        20_000_000,
        frame2.torque_cmd_nm,
        false,
    );
    assert_eq!(dec2.state, SafetyState::FaultEstop);
    assert!((dec2.safe_torque_nm - 0.0).abs() < 1e-12);
    assert_eq!(
        dec2.fault_mask,
        FAULT_WATCHDOG_EXPIRED | FAULT_TORQUE_OUT_OF_RANGE | FAULT_CRC_MISMATCH
    );
    assert!(!sink.commit(dec2.safe_torque_nm, dec2.fault_mask));
    assert_eq!(sink.latched_fault_mask(), dec2.fault_mask);

    // Wrong auth token must keep latch engaged.
    assert!(!interceptor.reset_authenticated(0x1234_5678));
    assert_eq!(interceptor.state(), SafetyState::FaultEstop);

    // Authenticated reset clears both Rust interceptor and C++ actuator sink.
    assert!(interceptor.reset_authenticated(AUTH_RESET_TOKEN));
    sink.clear_latched_fault();
    assert_eq!(interceptor.state(), SafetyState::Armed);
    assert_eq!(sink.latched_fault_mask(), 0);

    let frame3 = make_verified_frame(3, 25_000_000, -12.5, -0.4);
    let dec3 = interceptor.validate_command(
        frame3.timestamp_ns,
        26_000_000,
        frame3.torque_cmd_nm,
        true,
    );
    assert_eq!(dec3.state, SafetyState::Active);
    assert!(sink.commit(dec3.safe_torque_nm, dec3.fault_mask));
    assert_eq!(sink.committed_count(), 2);
    assert!((sink.last_safe_torque_nm() - (-12.5)).abs() < 1e-12);
}
