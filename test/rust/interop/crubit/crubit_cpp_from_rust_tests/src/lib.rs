//! Integration test crate verifying all 5 C++23 Crubit libraries from Rust 2024.

use core::pin::Pin;

use cc_references_views::interop::crubit::SignalWindow;
use cc_references_views::interop::crubit::select_dominant_window;
use cref::CRef;

/// Reads the current peak sample from a C++23 [`SignalWindow`] via `CRef::unchanging`.
#[must_use]
#[allow(unsafe_code)]
pub fn read_window_peak(window: &SignalWindow) -> f64 {
    let peak_ref = window.peak_ref();
    // SAFETY: `peak_ref` is lifetime-bound to `window`, which cannot be mutated during this borrow.
    let pinned: Pin<&f64> = unsafe { CRef::unchanging(peak_ref) };
    *pinned
}

/// Selects the dominant [`SignalWindow`] and returns its peak sample and count.
#[must_use]
#[allow(unsafe_code)]
pub fn dominant_window_summary(lhs: &SignalWindow, rhs: &SignalWindow) -> (f64, i32) {
    let dom_ref = select_dominant_window(lhs, rhs);
    // SAFETY: `dom_ref` is lifetime-bound to `lhs` and `rhs`, neither of which is mutated here.
    let dom: Pin<&SignalWindow> = unsafe { CRef::unchanging(dom_ref) };
    (read_window_peak(&dom), dom.count())
}
