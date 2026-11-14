//! Rust 2024 showcase executable exercising all 5 C++23 libraries via Google Crubit.

use core::pin::Pin;
use std::process::ExitCode;

use cc_ownership_raii::interop::crubit::LeaseGuard;
use cc_primitives_enums::interop::crubit::FlightPhase;
use cc_primitives_enums::interop::crubit::Waypoint3d;
use cc_primitives_enums::interop::crubit::advance_phase;
use cc_primitives_enums::interop::crubit::euclidean_distance;
use cc_references_views::interop::crubit::SignalWindow;
use cc_references_views::interop::crubit::scale_in_place;
use cc_references_views::interop::crubit::select_dominant_window;
use cc_telemetry_bridge::interop::crubit::ActuatorSink;
use cc_telemetry_bridge::interop::crubit::compute_frame_crc32;
use cc_telemetry_bridge::interop::crubit::make_verified_frame;
use cc_traits_errors::interop::crubit::CalibrationCode;
use cc_traits_errors::interop::crubit::calibrate_pd_controller;
use cref::CRef;
use ctor::CtorNew;
use rs_telemetry_validator::AUTH_RESET_TOKEN;
use rs_telemetry_validator::SafetyInterceptor;
use rs_telemetry_validator::SafetyState;

#[allow(unsafe_code)]
fn read_peak_value(window: &SignalWindow) -> f64 {
    let peak_ref = window.peak_ref();
    // SAFETY: `peak_ref` is lifetime-bound to `window`, which is immutable for this scope.
    let pinned: Pin<&f64> = unsafe { CRef::unchanging(peak_ref) };
    *pinned
}

#[allow(unsafe_code)]
fn read_dominant_peak(lhs: &SignalWindow, rhs: &SignalWindow) -> (f64, i32) {
    let dom_ref = select_dominant_window(lhs, rhs);
    // SAFETY: `dom_ref` is lifetime-bound to `lhs` and `rhs`, neither of which is mutated here.
    let dom: Pin<&SignalWindow> = unsafe { CRef::unchanging(dom_ref) };
    let peak = read_peak_value(&dom);
    let count = dom.count();
    (peak, count)
}

#[allow(clippy::useless_conversion)]
fn c_ulonglong_to_u64(val: ffi_11::c_ulonglong) -> u64 {
    u64::from(val)
}

fn run_primitives_enums() -> Result<(), &'static str> {
    let origin = Waypoint3d {
        x_m: 0.0,
        y_m: 0.0,
        altitude_m: 100.0,
    };
    let target = Waypoint3d {
        x_m: 3.0,
        y_m: 4.0,
        altitude_m: 112.0,
    };
    let dist_m = euclidean_distance(origin, target);
    if (dist_m - 13.0).abs() > 1e-9 {
        return Err("unexpected euclidean_distance");
    }

    let phase = advance_phase(FlightPhase::kPreflight, 50.0, 3.0);
    if phase != FlightPhase::kClimb {
        return Err("unexpected FlightPhase transition");
    }

    println!("[CRUBIT_RUST_DEMO] module=primitives_enums status=ok dist_m={dist_m:.2} phase=kClimb");
    Ok(())
}

fn run_references_views() -> Result<(), &'static str> {
    let mut win_a = SignalWindow::from(1.0);
    win_a.push_sample(7.5);
    let mut win_b = SignalWindow::from(4.0);
    win_b.push_sample(3.0);

    let (peak, count) = read_dominant_peak(&win_a, &win_b);
    if (peak - 7.5).abs() > 1e-9 || count != 2 {
        return Err("unexpected dominant SignalWindow state");
    }

    let mut scalar = 4.0_f64;
    scale_in_place(&mut scalar, 2.5);
    if (scalar - 10.0).abs() > 1e-9 {
        return Err("unexpected scale_in_place result");
    }

    println!("[CRUBIT_RUST_DEMO] module=references_views status=ok peak={peak:.2} count={count} scaled={scalar:.2}");
    Ok(())
}

fn run_ownership_raii() -> Result<(), &'static str> {
    let baseline = LeaseGuard::active_lease_count();
    let (lease_id, epochs, peak_active) = {
        let mut guard = ctor::emplace!(LeaseGuard::ctor_new(ffi_11::new_c_int(42)));
        let id = guard.lease_id();
        let updated = guard.as_mut().renew(ffi_11::new_c_int(3));
        let active_now = LeaseGuard::active_lease_count();
        (id, updated, active_now)
    };
    let after_drop = LeaseGuard::active_lease_count();

    if lease_id != 42 || epochs != 4 || peak_active != baseline + 1 || after_drop != baseline {
        return Err("unexpected LeaseGuard RAII lifecycle");
    }

    println!(
        "[CRUBIT_RUST_DEMO] module=ownership_raii status=ok lease_id={lease_id} epochs={epochs} active_after_drop={after_drop}"
    );
    Ok(())
}

fn run_traits_errors() -> Result<(), &'static str> {
    let ok_outcome = calibrate_pd_controller(5.0, 0.707);
    if ok_outcome.code != CalibrationCode::kOk {
        return Err("expected kOk calibration outcome");
    }
    let doubled = ok_outcome.gains.scaled(2.0);
    let expected = ok_outcome.gains.scaled(2.0);
    if doubled != expected {
        return Err("expected GainVector2d PartialEq to hold");
    }

    let invalid = calibrate_pd_controller(-1.0, 0.5);
    if invalid.code != CalibrationCode::kOutOfRange {
        return Err("expected kOutOfRange calibration outcome");
    }

    println!(
        "[CRUBIT_RUST_DEMO] module=traits_errors status=ok kp={:.2} kd={:.2}",
        ok_outcome.gains.kp, ok_outcome.gains.kd
    );
    Ok(())
}

fn run_telemetry_capstone() -> Result<(), &'static str> {
    let mut interceptor = SafetyInterceptor::new_armed();
    let mut sink = ActuatorSink::default();

    let frame1 = make_verified_frame(ffi_11::new_c_uint(1), ffi_11::new_c_ulonglong(10_000_000), 32.5, 1.8);
    let crc1_ok = compute_frame_crc32(
        frame1.sequence_id,
        frame1.timestamp_ns,
        frame1.torque_cmd_nm,
        frame1.velocity_rad_s,
    ) == frame1.crc32;
    let dec1 = interceptor.validate_command(
        c_ulonglong_to_u64(frame1.timestamp_ns),
        11_000_000,
        frame1.torque_cmd_nm,
        crc1_ok,
    );
    if dec1.state != SafetyState::Active || !sink.commit(dec1.safe_torque_nm, ffi_11::new_c_uint(dec1.fault_mask)) {
        return Err("nominal capstone frame rejected");
    }

    // Inject over-torque fault and verify both sides latch until authenticated reset.
    let frame2 = make_verified_frame(ffi_11::new_c_uint(2), ffi_11::new_c_ulonglong(12_000_000), 110.0, 2.5);
    let dec2 = interceptor.validate_command(
        c_ulonglong_to_u64(frame2.timestamp_ns),
        13_000_000,
        frame2.torque_cmd_nm,
        true,
    );
    if dec2.state != SafetyState::FaultEstop
        || sink.commit(dec2.safe_torque_nm, ffi_11::new_c_uint(dec2.fault_mask))
        || sink.latched_fault_mask() == 0
    {
        return Err("fault latch failed");
    }

    if !interceptor.reset_authenticated(AUTH_RESET_TOKEN) {
        return Err("authenticated reset failed");
    }
    sink.clear_latched_fault();

    let frame3 = make_verified_frame(ffi_11::new_c_uint(3), ffi_11::new_c_ulonglong(14_000_000), 24.0, 1.1);
    let dec3 = interceptor.validate_command(
        c_ulonglong_to_u64(frame3.timestamp_ns),
        15_000_000,
        frame3.torque_cmd_nm,
        true,
    );
    if !sink.commit(dec3.safe_torque_nm, ffi_11::new_c_uint(dec3.fault_mask)) {
        return Err("post-reset frame rejected");
    }

    let committed = sink.committed_count();
    let last_torque = sink.last_safe_torque_nm();
    println!(
        "[CRUBIT_RUST_DEMO] module=telemetry_capstone status=ok committed={committed} last_torque_nm={last_torque:.2}"
    );
    Ok(())
}

fn main() -> ExitCode {
    let steps: [fn() -> Result<(), &'static str>; 5] = [
        run_primitives_enums,
        run_references_views,
        run_ownership_raii,
        run_traits_errors,
        run_telemetry_capstone,
    ];
    for step in steps {
        if let Err(err) = step() {
            eprintln!("[CRUBIT_RUST_DEMO] status=error reason={err}");
            return ExitCode::FAILURE;
        }
    }
    ExitCode::SUCCESS
}
