//! 4-Tier integration test suite for LeetCode #228: Summary Ranges.

use std::time::{Duration, Instant};

use leet_code::summary_ranges::{Solution, SummaryRangesError, summary_ranges};

// ===========================================================================
// Tier 1: Canonical Examples
// ===========================================================================

#[test]
fn test_canonical_example_1() {
    let nums = [0, 1, 2, 4, 5, 7];
    let expected = vec!["0->2".to_string(), "4->5".to_string(), "7".to_string()];

    assert_eq!(summary_ranges(&nums), Ok(expected.clone()));
    assert_eq!(Solution::summary_ranges(nums.to_vec()), expected);
}

#[test]
fn test_canonical_example_2() {
    let nums = [0, 2, 3, 4, 6, 8, 9];
    let expected = vec![
        "0".to_string(),
        "2->4".to_string(),
        "6".to_string(),
        "8->9".to_string(),
    ];

    assert_eq!(summary_ranges(&nums), Ok(expected.clone()));
    assert_eq!(Solution::summary_ranges(nums.to_vec()), expected);
}

// ===========================================================================
// Tier 2: Boundary, Overflow Hazards & Preconditions
// ===========================================================================

#[test]
fn test_empty_input() {
    let nums: [i32; 0] = [];
    assert_eq!(summary_ranges(&nums), Ok(Vec::new()));
    assert_eq!(Solution::summary_ranges(Vec::new()), Vec::<String>::new());
}

#[test]
fn test_singleton_inputs() {
    assert_eq!(summary_ranges(&[0]), Ok(vec!["0".to_string()]));
    assert_eq!(summary_ranges(&[-42]), Ok(vec!["-42".to_string()]));
    assert_eq!(
        summary_ranges(&[i32::MIN]),
        Ok(vec!["-2147483648".to_string()])
    );
    assert_eq!(
        summary_ranges(&[i32::MAX]),
        Ok(vec!["2147483647".to_string()])
    );
    assert_eq!(Solution::summary_ranges(vec![0]), vec!["0".to_string()]);
    assert_eq!(
        Solution::summary_ranges(vec![i32::MIN]),
        vec!["-2147483648".to_string()]
    );
}

#[test]
fn test_full_contiguous_and_all_isolated_and_zero_crossing() {
    assert_eq!(
        summary_ranges(&[-3, -2, -1, 0, 1, 2, 3]),
        Ok(vec!["-3->3".to_string()])
    );
    assert_eq!(
        summary_ranges(&[-10, -5, 0, 5, 10]),
        Ok(vec![
            "-10".to_string(),
            "-5".to_string(),
            "0".to_string(),
            "5".to_string(),
            "10".to_string(),
        ])
    );
    assert_eq!(
        summary_ranges(&[-3, -2, 0, 1, 2, 10]),
        Ok(vec![
            "-3->-2".to_string(),
            "0->2".to_string(),
            "10".to_string(),
        ])
    );
}

#[test]
fn test_32bit_overflow_and_extremes() {
    assert_eq!(
        summary_ranges(&[i32::MIN, i32::MAX]),
        Ok(vec!["-2147483648".to_string(), "2147483647".to_string(),])
    );
    assert_eq!(
        summary_ranges(&[i32::MIN, 0]),
        Ok(vec!["-2147483648".to_string(), "0".to_string()])
    );
    assert_eq!(
        summary_ranges(&[-1, i32::MAX]),
        Ok(vec!["-1".to_string(), "2147483647".to_string()])
    );
    assert_eq!(
        summary_ranges(&[i32::MIN, i32::MIN + 1, 0, i32::MAX - 1, i32::MAX]),
        Ok(vec![
            "-2147483648->-2147483647".to_string(),
            "0".to_string(),
            "2147483646->2147483647".to_string(),
        ])
    );
    assert_eq!(
        Solution::summary_ranges(vec![i32::MIN, i32::MAX]),
        vec!["-2147483648".to_string(), "2147483647".to_string()]
    );
}

#[test]
fn test_precondition_not_strictly_ascending_errors() {
    let invalid_cases: [&[i32]; 8] = [
        &[1, 2, 2, 3],
        &[0, 0],
        &[i32::MIN, i32::MIN],
        &[i32::MAX, i32::MAX],
        &[3, 1, 2],
        &[2, 1],
        &[-1, -2],
        // Adversarial wrap-around trap: i32::MIN.wrapping_sub(i32::MAX) == 1
        &[i32::MAX, i32::MIN],
    ];

    for case in invalid_cases {
        assert_eq!(
            summary_ranges(case),
            Err(SummaryRangesError::NotStrictlyAscending),
            "expected NotStrictlyAscending for {case:?}"
        );
        assert_eq!(
            Solution::summary_ranges(case.to_vec()),
            Vec::<String>::new(),
            "expected empty Vec from Solution wrapper for invalid {case:?}"
        );
    }
}

// ===========================================================================
// Tier 3: Invariants & Properties
// ===========================================================================

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

    fn next_range_i64(&mut self, low: i64, high_inclusive: i64) -> i64 {
        let span = u64::try_from(high_inclusive - low + 1).unwrap_or(1);
        let offset = i64::try_from(self.next_u64() % span).unwrap_or(0);
        low + offset
    }
}

fn parse_range(token: &str) -> Result<(i32, i32), String> {
    if let Some((left, right)) = token.split_once("->") {
        let start = left
            .parse::<i32>()
            .map_err(|err| format!("invalid start `{left}` in `{token}`: {err}"))?;
        let end = right
            .parse::<i32>()
            .map_err(|err| format!("invalid end `{right}` in `{token}`: {err}"))?;
        if start >= end {
            return Err(format!("expected start < end for arrow range `{token}`"));
        }
        if format!("{start}->{end}") != token {
            return Err(format!("non-canonical range formatting `{token}`"));
        }
        Ok((start, end))
    } else {
        let val = token
            .parse::<i32>()
            .map_err(|err| format!("invalid singleton `{token}`: {err}"))?;
        if val.to_string() != token {
            return Err(format!("non-canonical singleton formatting `{token}`"));
        }
        Ok((val, val))
    }
}

fn verify_partition_invariants(nums: &[i32], ranges: &[String]) {
    let mut reconstructed = Vec::with_capacity(nums.len());
    let mut prev_end: Option<i32> = None;

    for token in ranges {
        let parsed = parse_range(token);
        assert!(
            parsed.is_ok(),
            "failed to parse range token `{token}`: {parsed:?}"
        );
        let Ok((start, end)) = parsed else {
            return;
        };

        assert!(start <= end, "range start {start} > end {end}");
        if let Some(prev) = prev_end {
            assert!(
                i64::from(prev) + 1 < i64::from(start),
                "ranges [{prev}] and [{start}, {end}] could have been coalesced or overlap"
            );
        }

        let span = i64::from(end) - i64::from(start) + 1;
        let max_len = i64::try_from(nums.len()).unwrap_or(i64::MAX);
        assert!(
            span <= max_len,
            "range [{start}, {end}] length {span} exceeds input length {}",
            nums.len()
        );

        for val in start..=end {
            reconstructed.push(val);
        }
        prev_end = Some(end);
    }

    assert_eq!(
        reconstructed, nums,
        "reconstructed numbers did not match input"
    );
}

#[test]
fn test_property_partition_invariants_and_wrapper_parity() {
    let mut rng = XorShift64::new(0x5eed_0228_cafe_babe);
    let base_anchors: [i64; 5] = [i64::from(i32::MIN), -100, -10, 0, i64::from(i32::MAX) - 100];

    for trial in 0_usize..200 {
        let len = if trial <= 20 {
            trial
        } else {
            usize::try_from(rng.next_range_i64(0, 20)).unwrap_or(0)
        };
        let mut current = base_anchors[trial % 5] + rng.next_range_i64(0, 20);
        let mut nums = Vec::with_capacity(len);

        for _ in 0..len {
            let Ok(val) = i32::try_from(current) else {
                break;
            };
            nums.push(val);
            current += rng.next_range_i64(1, 4);
        }

        let res = summary_ranges(&nums);
        assert!(
            res.is_ok(),
            "expected Ok for strictly ascending input {nums:?}, got {res:?}"
        );
        let Ok(ranges) = res else {
            return;
        };

        verify_partition_invariants(&nums, &ranges);
        assert_eq!(summary_ranges(&nums).as_ref(), Ok(&ranges));
        assert_eq!(Solution::summary_ranges(nums.clone()), ranges);
    }
}

// ===========================================================================
// Tier 4: Stress / High-Constraint Tests
// ===========================================================================

#[test]
fn test_stress_100k_disjoint_two_element_ranges() {
    const PAIRS: i32 = 50_000;
    let mut nums = Vec::with_capacity(100_000);
    let mut expected = Vec::with_capacity(50_000);

    for k in 0..PAIRS {
        let a = 3 * k;
        let b = a + 1;
        nums.push(a);
        nums.push(b);
        expected.push(format!("{a}->{b}"));
    }

    let start = Instant::now();
    let res = summary_ranges(&nums);
    let elapsed = start.elapsed();

    assert!(
        res.is_ok(),
        "expected Ok(50_000 ranges) for disjoint 100k workload, got {res:?}"
    );
    let Ok(ranges) = res else {
        return;
    };
    assert_eq!(ranges, expected);
    assert!(
        elapsed < Duration::from_millis(500),
        "disjoint 100k workload took {elapsed:?}, exceeding 500ms budget"
    );
}

#[test]
fn test_stress_100k_contiguous_elements() {
    let nums: Vec<i32> = (-50_000..50_000).collect();

    let start = Instant::now();
    let res = summary_ranges(&nums);
    let elapsed = start.elapsed();

    assert_eq!(res, Ok(vec!["-50000->49999".to_string()]));
    assert!(
        elapsed < Duration::from_millis(500),
        "contiguous 100k workload took {elapsed:?}, exceeding 500ms budget"
    );
}
