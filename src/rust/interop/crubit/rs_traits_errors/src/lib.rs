//! Crubit interop Module 4: standard traits (`Display`, `Default`, `PartialEq`) and `Option`/`Result` errors.

use core::fmt;

const NOMINAL_GAINS: [f64; 8] = [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0];

/// Identifier pairing a PID controller ID with a gain profile slot.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct PidScheduleId {
    /// Controller identifier.
    pub controller_id: u32,
    /// Gain schedule slot index.
    pub profile_slot: u16,
}

impl fmt::Display for PidScheduleId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}", self.controller_id, self.profile_slot)
    }
}

/// Errors produced when parsing a [`PidScheduleId`] from text.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ParseScheduleError {
    /// Input string was empty.
    EmptyInput,
    /// Input lacked a valid `<controller_id>:<profile_slot>` numeric separator structure.
    MalformedSeparator,
    /// Parsed integer value exceeded `u32`/`u16` range.
    OutOfBounds,
}

impl fmt::Display for ParseScheduleError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::EmptyInput => f.write_str("empty schedule input"),
            Self::MalformedSeparator => f.write_str("malformed controller:slot separator"),
            Self::OutOfBounds => f.write_str("schedule identifier component out of bounds"),
        }
    }
}

impl core::error::Error for ParseScheduleError {}

/// Looks up the nominal proportional gain for profile slots `0..=7`.
#[must_use]
pub fn lookup_nominal_gain(slot: u16) -> Option<f64> {
    NOMINAL_GAINS.get(usize::from(slot)).copied()
}

/// Parses a `"<controller_id>:<profile_slot>"` string into a [`PidScheduleId`].
///
/// # Errors
///
/// - [`ParseScheduleError::EmptyInput`] when `text` is empty.
/// - [`ParseScheduleError::MalformedSeparator`] when `text` does not have exactly two ASCII-digit fields around `:`.
/// - [`ParseScheduleError::OutOfBounds`] when either integer overflows `u32` or `u16`.
pub fn parse_schedule_id(text: &str) -> Result<PidScheduleId, ParseScheduleError> {
    if text.is_empty() {
        return Err(ParseScheduleError::EmptyInput);
    }
    let Some((ctrl_str, slot_str)) = text.split_once(':') else {
        return Err(ParseScheduleError::MalformedSeparator);
    };
    if ctrl_str.is_empty()
        || slot_str.is_empty()
        || !ctrl_str.bytes().all(|b| b.is_ascii_digit())
        || !slot_str.bytes().all(|b| b.is_ascii_digit())
    {
        return Err(ParseScheduleError::MalformedSeparator);
    }
    let Ok(controller_id) = ctrl_str.parse::<u32>() else {
        return Err(ParseScheduleError::OutOfBounds);
    };
    let Ok(profile_slot) = slot_str.parse::<u16>() else {
        return Err(ParseScheduleError::OutOfBounds);
    };
    Ok(PidScheduleId {
        controller_id,
        profile_slot,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn gain_lookup_covers_valid_and_invalid_slots() {
        assert_eq!(lookup_nominal_gain(0), Some(0.5));
        assert_eq!(lookup_nominal_gain(7), Some(5.0));
        assert_eq!(lookup_nominal_gain(8), None);
    }

    #[test]
    fn parse_and_display_roundtrip() {
        let parsed = parse_schedule_id("42:3");
        let expected = PidScheduleId {
            controller_id: 42,
            profile_slot: 3,
        };
        assert_eq!(parsed, Ok(expected));
        assert_eq!(expected.to_string(), "42:3");

        assert_eq!(parse_schedule_id(""), Err(ParseScheduleError::EmptyInput));
        assert_eq!(parse_schedule_id("42"), Err(ParseScheduleError::MalformedSeparator));
        assert_eq!(parse_schedule_id("42:1:2"), Err(ParseScheduleError::MalformedSeparator));
        assert_eq!(parse_schedule_id("abc:1"), Err(ParseScheduleError::MalformedSeparator));
        assert_eq!(parse_schedule_id("42:70000"), Err(ParseScheduleError::OutOfBounds));
    }
}
