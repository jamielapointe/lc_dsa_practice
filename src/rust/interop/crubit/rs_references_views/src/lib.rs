//! Crubit interop Module 2: slices (`&[T]`, `&mut [T]`), `&str` views, and borrowed `self` cursors.

/// Computes the root-mean-square (RMS) of `samples`.
///
/// Returns `0.0` if `samples` is empty.
#[must_use]
pub fn rms_slice(samples: &[f64]) -> f64 {
    if samples.is_empty() {
        return 0.0;
    }
    let sum_sq: f64 = samples.iter().map(|&x| x * x).sum();
    #[allow(clippy::cast_precision_loss)]
    let count = samples.len() as f64;
    (sum_sq / count).sqrt()
}

/// Normalizes `samples` in place to unit RMS scaled by `gain`.
///
/// Leaves `samples` untouched if `samples` is empty, has zero RMS, or `gain` is non-finite.
pub fn normalize_slice_in_place(samples: &mut [f64], gain: f64) {
    if !gain.is_finite() {
        return;
    }
    let rms = rms_slice(samples);
    if rms == 0.0 || !rms.is_finite() {
        return;
    }
    let scale = gain / rms;
    for sample in samples {
        *sample *= scale;
    }
}

/// Parses a sensor tag of the form `"TAG-<digits>"` into a non-negative `i32`.
///
/// Returns `-1` if `tag` does not match `"TAG-<digits>"` or overflows `i32`.
#[must_use]
pub fn parse_sensor_tag_id(tag: &str) -> i32 {
    let Some(digits) = tag.strip_prefix("TAG-") else {
        return -1;
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return -1;
    }
    digits.parse::<i32>().unwrap_or(-1)
}

/// Fixed-capacity modular ring cursor demonstrating `&self` and `&mut self` borrows across Crubit.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RingCursor {
    index: usize,
    capacity: usize,
}

impl RingCursor {
    /// Creates a new [`RingCursor`] starting at index `0` with modular `capacity`.
    #[must_use]
    pub const fn create(capacity: usize) -> Self {
        Self { index: 0, capacity }
    }

    /// Advances the cursor by `steps` modulo `capacity` and returns the new index.
    ///
    /// Returns `0` when `capacity == 0`.
    pub const fn advance(&mut self, steps: usize) -> usize {
        if self.capacity == 0 {
            self.index = 0;
            return 0;
        }
        let cur = self.index % self.capacity;
        let step_mod = steps % self.capacity;
        let rem = self.capacity - cur;
        self.index = if step_mod >= rem {
            step_mod - rem
        } else {
            cur + step_mod
        };
        self.index
    }

    /// Returns the current cursor index.
    #[must_use]
    pub const fn index(&self) -> usize {
        self.index
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rms_and_normalize_roundtrip() {
        assert!((rms_slice(&[]) - 0.0).abs() < 1e-12);
        let mut data = [3.0, -3.0, 3.0, -3.0];
        assert!((rms_slice(&data) - 3.0).abs() < 1e-12);
        normalize_slice_in_place(&mut data, 6.0);
        assert!((rms_slice(&data) - 6.0).abs() < 1e-12);
    }

    #[test]
    fn parses_sensor_tags() {
        assert_eq!(parse_sensor_tag_id("TAG-0"), 0);
        assert_eq!(parse_sensor_tag_id("TAG-42"), 42);
        assert_eq!(parse_sensor_tag_id("TAG-"), -1);
        assert_eq!(parse_sensor_tag_id("TAG--5"), -1);
        assert_eq!(parse_sensor_tag_id("TAG-+5"), -1);
        assert_eq!(parse_sensor_tag_id("ID-42"), -1);
        assert_eq!(parse_sensor_tag_id("TAG-99999999999999"), -1);
    }

    #[test]
    fn ring_cursor_wraps_without_overflow() {
        let mut zero_cap = RingCursor::create(0);
        assert_eq!(zero_cap.advance(5), 0);
        assert_eq!(zero_cap.index(), 0);

        let mut cursor = RingCursor::create(8);
        assert_eq!(cursor.index(), 0);
        assert_eq!(cursor.advance(5), 5);
        assert_eq!(cursor.advance(6), 3);
        assert_eq!(cursor.advance(usize::MAX), (3 + usize::MAX % 8) % 8);
    }
}
