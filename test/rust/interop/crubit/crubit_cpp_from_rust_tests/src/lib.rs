//! Integration test crate verifying all 5 C++23 Crubit libraries from Rust 2024.

use core::pin::Pin;

use cc_references_views::interop::crubit::{SignalWindow, select_dominant_window};
use cref::CRef;

/// Reads the current peak sample from a C++23 [`SignalWindow`] via `CRef::unchanging`.
#[must_use]
#[allow(unsafe_code)]
pub fn read_window_peak(window: &SignalWindow) -> f64 {
    let peak_ref = window.peak_ref();
    // SAFETY:
    // 1. `peak_ref` is a valid non-null reference inside `window` bound to `window`'s borrow lifetime.
    // 2. `window` is borrowed immutably (`&SignalWindow`) for the entire scope of `pinned`,
    //    guaranteeing the C++ pointee is not mutated while `Pin<&f64>` is live.
    let pinned: Pin<&f64> = unsafe { CRef::unchanging(peak_ref) };
    *pinned
}

/// Selects the dominant [`SignalWindow`] and returns its peak sample and count.
#[must_use]
#[allow(unsafe_code)]
pub fn dominant_window_summary(lhs: &SignalWindow, rhs: &SignalWindow) -> (f64, i32) {
    let dom_ref = select_dominant_window(lhs, rhs);
    // SAFETY:
    // 1. `dom_ref` aliases either `lhs` or `rhs` and is lifetime-bound to both shared borrows.
    // 2. Neither `lhs` nor `rhs` is mutated while `dom` is live.
    let dom: Pin<&SignalWindow> = unsafe { CRef::unchanging(dom_ref) };
    (read_window_peak(&dom), dom.count())
}
