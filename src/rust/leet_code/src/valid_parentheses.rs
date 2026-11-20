//! LeetCode #20: Valid Parentheses (Easy).
//!
//! URL: <https://leetcode.com/problems/valid-parentheses/>
//!
//! # Problem Description
//!
//! Given a string `s` containing just the characters `'('`, `')'`, `'{'`, `'}'`,
//! `'['` and `']'`, determine if the input string is valid.
//!
//! An input string is valid if:
//!
//! 1. Open brackets must be closed by the same type of brackets.
//! 2. Open brackets must be closed in the correct order.
//! 3. Every close bracket has a corresponding open bracket of the same type.
//!
//! # Examples
//!
//! 1. `s = "()"` -> `true`
//! 2. `s = "()[]{}"` -> `true`
//! 3. `s = "(]"` -> `false`
//! 4. `s = "([])"` -> `true`
//! 5. `s = "([)]"` -> `false`
//!
//! # Constraints
//!
//! - `1 <= s.length <= 10^4`
//! - `s` consists of parentheses only `'()[]{}'`.
//!
//! # Complexity Target
//!
//! - Time: `O(n)` single linear pass over `s`
//! - Space: `O(n)` worst-case stack storage for opening brackets
//!
//! # Preconditions
//!
//! - Valid alphabet: ONLY `'('`, `')'`, `'{'`, `'}'`, `'['`, `']'`.
//! - An empty string `""` is treated as vacuously valid and returns `Ok(true)`
//!   on [`valid_parentheses`] (and `true` on [`Solution::is_valid`]).
//! - Any character outside `'()[]{}'` causes [`valid_parentheses`] to return
//!   `Err(ValidParenthesesError::InvalidCharacter)`, while
//!   [`Solution::is_valid`] maps errors to `false`.

/// Errors that may occur during the evaluation of [`valid_parentheses`].
#[must_use]
#[non_exhaustive]
#[derive(Clone, Debug, Eq, PartialEq, thiserror::Error)]
pub enum ValidParenthesesError {
    /// Input string contains a character outside `'()[]{}'`.
    #[error("input string must contain only bracket characters '()[]{{}}'")]
    InvalidCharacter,
    /// Function stub has not yet been implemented.
    #[error("valid_parentheses is not yet implemented")]
    NotImplemented,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum BracketKind {
    Paren,
    Brace,
    Bracket,
}

#[must_use]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum Bracket {
    Open(BracketKind),
    Close(BracketKind),
}

impl TryFrom<char> for Bracket {
    type Error = ValidParenthesesError;

    fn try_from(value: char) -> Result<Self, Self::Error> {
        match value {
            '(' => Ok(Self::Open(BracketKind::Paren)),
            ')' => Ok(Self::Close(BracketKind::Paren)),
            '{' => Ok(Self::Open(BracketKind::Brace)),
            '}' => Ok(Self::Close(BracketKind::Brace)),
            '[' => Ok(Self::Open(BracketKind::Bracket)),
            ']' => Ok(Self::Close(BracketKind::Bracket)),
            _ => Err(ValidParenthesesError::InvalidCharacter),
        }
    }
}

/// Determines whether the bracket sequence `s` is properly balanced and nested.
///
/// An empty string `""` is vacuously valid and evaluates to `Ok(true)`.
///
/// # Errors
///
/// Returns [`ValidParenthesesError::InvalidCharacter`] if `s` contains any
/// character outside `'()[]{}'`.
/// Returns [`ValidParenthesesError::NotImplemented`] while this function
/// remains a stub.
///
/// # Examples
///
/// ```no_run
/// # fn main() -> Result<(), Box<dyn std::error::Error>> {
/// use leet_code::valid_parentheses::valid_parentheses;
///
/// assert!(valid_parentheses("()[]{}")?);
/// assert!(!valid_parentheses("([)]")?);
/// # Ok(())
/// # }
/// ```
pub fn valid_parentheses(s: &str) -> Result<bool, ValidParenthesesError> {
    let mut bracket_stack = Vec::new();

    for char in s.chars() {
        match Bracket::try_from(char)? {
            Bracket::Open(kind) => bracket_stack.push(kind),
            Bracket::Close(kind) => {
                if bracket_stack.pop() != Some(kind) {
                    return Ok(false);
                }
            }
        }
    }

    Ok(bracket_stack.is_empty())
}

/// LeetCode compatibility wrapper matching the official method signature.
pub struct Solution;

impl Solution {
    /// LeetCode submission entrypoint.
    ///
    /// Delegates to [`valid_parentheses`] via `.unwrap_or(false)`. An empty
    /// string `""` returns `true`.
    ///
    /// # Panics
    ///
    /// This function never panics; invalid inputs return `false`.
    #[must_use]
    #[expect(
        clippy::needless_pass_by_value,
        reason = "Matches official LeetCode String signature"
    )]
    pub fn is_valid(s: String) -> bool {
        valid_parentheses(&s).unwrap_or(false)
    }
}
