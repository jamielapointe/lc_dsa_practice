//! 4-Tier integration test suite for LeetCode #20: Valid Parentheses.

use std::time::{Duration, Instant};

use leet_code::valid_parentheses::{Solution, ValidParenthesesError, valid_parentheses};

// ===========================================================================
// Tier 1: Canonical Examples
// ===========================================================================

#[test]
fn test_canonical_examples() {
    let cases = [
        ("()", true),
        ("()[]{}", true),
        ("(]", false),
        ("([])", true),
        ("([)]", false),
    ];

    for (input, expected) in cases {
        assert_eq!(
            valid_parentheses(input),
            Ok(expected),
            "valid_parentheses failed for canonical case {input:?}"
        );
        assert_eq!(
            Solution::is_valid(input.to_owned()),
            expected,
            "Solution::is_valid failed for canonical case {input:?}"
        );
    }

    let borrowed_slice = &"xx([])yy"[2..6];
    assert_eq!(valid_parentheses(borrowed_slice), Ok(true));
    assert!(Solution::is_valid(borrowed_slice.to_owned()));
}

// ===========================================================================
// Tier 2: Boundary, Edge Cases & Preconditions
// ===========================================================================

#[test]
fn test_empty_and_single_brackets() {
    assert_eq!(valid_parentheses(""), Ok(true));
    assert!(Solution::is_valid(String::new()));

    for single in ["(", ")", "[", "]", "{", "}"] {
        assert_eq!(
            valid_parentheses(single),
            Ok(false),
            "expected false for single bracket {single:?}"
        );
        assert!(
            !Solution::is_valid(single.to_owned()),
            "expected false from Solution::is_valid for {single:?}"
        );
    }
}

#[test]
fn test_odd_reversed_all_open_and_all_close() {
    let odd_lengths = ["(()", "())", "()(", "()[]{", "[({})"];
    let reversed = [")(", "][", "}{", ")()(", ")[]{}"];
    let all_open = ["((((", "(([{{", "([{"];
    let all_close = ["))))", "]]}}", "()))", "()[]}"];

    for case in odd_lengths
        .into_iter()
        .chain(reversed)
        .chain(all_open)
        .chain(all_close)
    {
        assert_eq!(
            valid_parentheses(case),
            Ok(false),
            "expected false for {case:?}"
        );
        assert!(
            !Solution::is_valid(case.to_owned()),
            "expected false from Solution::is_valid for {case:?}"
        );
    }
}

#[test]
fn test_all_nine_open_close_pairs() {
    let pairs = [
        ("()", true),
        ("(]", false),
        ("(}", false),
        ("[)", false),
        ("[]", true),
        ("[}", false),
        ("{)", false),
        ("{]", false),
        ("{}", true),
    ];

    for (pair, expected) in pairs {
        assert_eq!(
            valid_parentheses(pair),
            Ok(expected),
            "unexpected result for pair {pair:?}"
        );
        assert_eq!(
            Solution::is_valid(pair.to_owned()),
            expected,
            "unexpected Solution::is_valid result for pair {pair:?}"
        );
    }
}

#[test]
fn test_crossed_deep_sibling_and_targeted_mismatches() {
    let cases = [
        // Crossed / interleaved traps
        ("([)]", false),
        ("({[}])", false),
        ("[({]})", false),
        ("({)}", false),
        ("[(])", false),
        // Deep single-type, deep mixed, and sibling subtrees
        ("(((((())))))", true),
        ("({[({[()]})]})", true),
        ("(()[]){[()()]()}", true),
        // Mismatch at very end and in middle
        ("({[()]})[", false),
        ("({[()]})]", false),
        ("(([}])", false),
    ];

    for (case, expected) in cases {
        assert_eq!(
            valid_parentheses(case),
            Ok(expected),
            "unexpected result for structural case {case:?}"
        );
        assert_eq!(
            Solution::is_valid(case.to_owned()),
            expected,
            "unexpected Solution::is_valid result for structural case {case:?}"
        );
    }
}

#[test]
fn test_precondition_invalid_characters() {
    // No bracket mismatch occurs prior to the invalid character in any case.
    let invalid_cases = [
        "a", " ", "(a)", "( )", "()x", "([z])", "{\t}", "(\n)", "()\0", "（)", "🚀",
    ];

    for input in invalid_cases {
        assert_eq!(
            valid_parentheses(input),
            Err(ValidParenthesesError::InvalidCharacter),
            "expected InvalidCharacter for {input:?}"
        );
        assert!(
            !Solution::is_valid(input.to_owned()),
            "expected false from Solution::is_valid for invalid {input:?}"
        );
    }
}

// ===========================================================================
// Tier 3: Invariants & Differential Oracle
// ===========================================================================

const OPEN_BRACKETS: [u8; 3] = *b"([{";
const ALL_BRACKETS: [u8; 6] = *b"()[]{}";

struct XorShift64 {
    state: u64,
}

impl XorShift64 {
    const fn new(seed: u64) -> Self {
        Self {
            state: if seed == 0 {
                0x9e37_79b9_7f4a_7c15
            } else {
                seed
            },
        }
    }

    const fn next_u64(&mut self) -> u64 {
        let mut x = self.state;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.state = x;
        x
    }

    fn next_usize(&mut self, upper_exclusive: usize) -> usize {
        let span = u64::try_from(upper_exclusive).unwrap_or(1).max(1);
        usize::try_from(self.next_u64() % span).unwrap_or(0)
    }
}

const fn matching_close(open: u8) -> u8 {
    match open {
        b'(' => b')',
        b'[' => b']',
        _ => b'}',
    }
}

fn generate_valid_dyck(rng: &mut XorShift64, pairs: usize) -> String {
    let mut out = Vec::with_capacity(pairs * 2);
    let mut stack = Vec::with_capacity(pairs);
    let mut opened = 0_usize;

    while opened < pairs || !stack.is_empty() {
        let can_open = opened < pairs;
        let can_close = !stack.is_empty();
        let should_open = match (can_open, can_close) {
            (true, false) => true,
            (false, true) => false,
            (true, true) => rng.next_u64().is_multiple_of(2),
            (false, false) => break,
        };

        if should_open {
            let open = OPEN_BRACKETS[rng.next_usize(OPEN_BRACKETS.len())];
            out.push(open);
            stack.push(matching_close(open));
            opened += 1;
        } else if let Some(close) = stack.pop() {
            out.push(close);
        }
    }

    String::from_utf8(out).unwrap_or_default()
}

fn assert_single_deletions_invalid(dyck: &str) {
    let bytes = dyck.as_bytes();
    for idx in 0..bytes.len() {
        let mut deleted = Vec::with_capacity(bytes.len() - 1);
        deleted.extend_from_slice(&bytes[..idx]);
        deleted.extend_from_slice(&bytes[idx + 1..]);
        if let Ok(mutated) = String::from_utf8(deleted) {
            assert_eq!(
                valid_parentheses(&mutated),
                Ok(false),
                "deleting index {idx} from {dyck:?} yielded {mutated:?}, expected false"
            );
            assert!(
                !Solution::is_valid(mutated),
                "Solution::is_valid should reject single deletion of {dyck:?}"
            );
        }
    }
}

fn assert_single_corruptions_invalid(dyck: &str) {
    let bytes = dyck.as_bytes();
    for idx in 0..bytes.len() {
        for &replacement in &ALL_BRACKETS {
            if replacement == bytes[idx] {
                continue;
            }
            let mut corrupted = bytes.to_vec();
            corrupted[idx] = replacement;
            if let Ok(mutated) = String::from_utf8(corrupted) {
                assert_eq!(
                    valid_parentheses(&mutated),
                    Ok(false),
                    "corrupting index {idx} in {dyck:?} yielded {mutated:?}, expected false"
                );
                assert!(
                    !Solution::is_valid(mutated),
                    "Solution::is_valid should reject single corruption of {dyck:?}"
                );
            }
        }
    }
}

#[test]
fn test_property_dyck_invariants() {
    let mut rng = XorShift64::new(0x5eed_0020_cafe_babe);

    for trial in 0_usize..150 {
        let pairs = if trial == 0 { 0 } else { 1 + rng.next_usize(8) };
        let dyck = generate_valid_dyck(&mut rng, pairs);

        assert_eq!(valid_parentheses(&dyck), Ok(true));
        assert!(Solution::is_valid(dyck.clone()));

        for (open, close) in [('(', ')'), ('[', ']'), ('{', '}')] {
            let wrapped = format!("{open}{dyck}{close}");
            assert_eq!(valid_parentheses(&wrapped), Ok(true));
            assert!(Solution::is_valid(wrapped));
        }

        let partner_pairs = rng.next_usize(6);
        let partner = generate_valid_dyck(&mut rng, partner_pairs);
        let concatenated = format!("{dyck}{partner}");
        assert_eq!(valid_parentheses(&concatenated), Ok(true));
        assert!(Solution::is_valid(concatenated));

        assert_single_deletions_invalid(&dyck);
        assert_single_corruptions_invalid(&dyck);
    }
}

fn reference_is_valid_reduction(s: &str) -> bool {
    let mut reduced = s.to_owned();
    loop {
        let next = reduced
            .replace("()", "")
            .replace("[]", "")
            .replace("{}", "");
        if next.len() == reduced.len() {
            return next.is_empty();
        }
        reduced = next;
    }
}

#[test]
fn test_differential_against_pair_reduction_oracle() {
    let mut rng = XorShift64::new(0x0020_dead_beef_1234);

    for trial in 0_usize..200 {
        let len = if trial <= 24 {
            trial
        } else {
            rng.next_usize(25)
        };
        let mut bytes = Vec::with_capacity(len);
        for _ in 0..len {
            let byte = if trial.is_multiple_of(3) {
                if rng.next_u64().is_multiple_of(2) {
                    b'('
                } else {
                    b')'
                }
            } else {
                ALL_BRACKETS[rng.next_usize(ALL_BRACKETS.len())]
            };
            bytes.push(byte);
        }

        let candidate = String::from_utf8(bytes).unwrap_or_default();
        let expected = reference_is_valid_reduction(&candidate);

        assert_eq!(
            valid_parentheses(&candidate),
            Ok(expected),
            "oracle mismatch for {candidate:?}"
        );
        assert_eq!(
            Solution::is_valid(candidate),
            expected,
            "Solution::is_valid parity mismatch"
        );
    }
}

// ===========================================================================
// Tier 4: Stress & OJ Simulation
// ===========================================================================

fn build_deep_nested_brackets(pairs: usize) -> String {
    let mut out = Vec::with_capacity(pairs * 2);
    for idx in 0..pairs {
        out.push(OPEN_BRACKETS[idx % OPEN_BRACKETS.len()]);
    }
    for idx in (0..pairs).rev() {
        out.push(matching_close(OPEN_BRACKETS[idx % OPEN_BRACKETS.len()]));
    }
    String::from_utf8(out).unwrap_or_default()
}

#[test]
fn test_stress_max_constraint_10k() {
    let valid_10k = build_deep_nested_brackets(5_000);
    assert_eq!(valid_10k.len(), 10_000);
    assert_eq!(valid_parentheses(&valid_10k), Ok(true));
    assert!(Solution::is_valid(valid_10k.clone()));

    let mut mismatch_bytes = valid_10k.into_bytes();
    if let Some(last) = mismatch_bytes.last_mut() {
        *last = if *last == b')' { b']' } else { b')' };
    }
    let mismatch_10k = String::from_utf8(mismatch_bytes).unwrap_or_default();
    assert_eq!(mismatch_10k.len(), 10_000);
    assert_eq!(valid_parentheses(&mismatch_10k), Ok(false));
    assert!(!Solution::is_valid(mismatch_10k));

    let all_open_10k = "(".repeat(10_000);
    assert_eq!(valid_parentheses(&all_open_10k), Ok(false));
    assert!(!Solution::is_valid(all_open_10k));
}

#[test]
fn test_stress_large_scale_100k() {
    let flat_100k = "()[]{}".repeat(16_667);
    assert_eq!(flat_100k.len(), 100_002);

    let start_flat = Instant::now();
    let flat_res = valid_parentheses(&flat_100k);
    let elapsed_flat = start_flat.elapsed();
    assert_eq!(flat_res, Ok(true));
    assert!(
        elapsed_flat < Duration::from_millis(500),
        "flat 100k workload took {elapsed_flat:?}, exceeding 500ms budget"
    );

    let deep_100k = build_deep_nested_brackets(50_000);
    assert_eq!(deep_100k.len(), 100_000);

    let start_deep = Instant::now();
    let deep_res = valid_parentheses(&deep_100k);
    let elapsed_deep = start_deep.elapsed();
    assert_eq!(deep_res, Ok(true));
    assert!(
        elapsed_deep < Duration::from_millis(500),
        "deep 100k workload took {elapsed_deep:?}, exceeding 500ms budget"
    );
}

#[test]
fn test_oj_simulation_table() {
    let oj_cases = [
        ("", true),
        ("()", true),
        ("()[]{}", true),
        ("(]", false),
        ("([])", true),
        ("([)]", false),
        ("{[]}", true),
        ("(", false),
        ("]", false),
        ("((({{{[[[]]]}}})))", true),
        ("((({{{[[[]]]}}})", false),
        ("abc", false),
        ("(a)", false),
    ];

    for (input, expected) in oj_cases {
        assert_eq!(
            Solution::is_valid(input.to_owned()),
            expected,
            "OJ simulation failed for {input:?}"
        );
    }
}
