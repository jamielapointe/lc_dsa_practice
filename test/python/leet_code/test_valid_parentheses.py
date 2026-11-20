"""Tests for ``leet_code.valid_parentheses`` (4-tier layout)."""

import time

import pytest
from hypothesis import given
from hypothesis import strategies as st

from leet_code.valid_parentheses import Solution, valid_parentheses


_BRACKETS = "()[]{}"
_BRACKET_PAIRS = (("(", ")"), ("[", "]"), ("{", "}"))
_INVALID_CHAR_PATTERN = r"(?i)invalid|bracket|character|parenthes"


# ---------------------------------------------------------------------------
# Tier 1: Canonical feature coverage (official LeetCode examples)
# ---------------------------------------------------------------------------


@pytest.mark.unit
@pytest.mark.parametrize(
    ("s", "expected"),
    [
        ("()", True),
        ("()[]{}", True),
        ("(]", False),
        ("([])", True),
        ("([)]", False),
    ],
)
def test_canonical_examples(s: str, expected: bool) -> None:
    """Official LeetCode examples return the expected validity."""
    assert valid_parentheses(s) is expected


@pytest.mark.unit
@pytest.mark.parametrize(
    ("s", "expected"),
    [
        ("()", True),
        ("()[]{}", True),
        ("(]", False),
        ("([])", True),
        ("([)]", False),
    ],
)
def test_solution_wrapper_canonical_examples(s: str, expected: bool) -> None:
    """The LeetCode wrapper returns the expected validity for official examples."""
    assert Solution().isValid(s) is expected


# ---------------------------------------------------------------------------
# Tier 2: Boundary, corner, edge, and precondition cases
# ---------------------------------------------------------------------------


@pytest.mark.unit
def test_empty_string_is_vacuously_valid() -> None:
    """An empty string is vacuously valid on both interfaces."""
    assert valid_parentheses("") is True
    assert Solution().isValid("") is True


@pytest.mark.unit
@pytest.mark.parametrize("s", ["(", ")", "[", "]", "{", "}"])
def test_single_brackets_are_invalid(s: str) -> None:
    """Every single-character bracket string is invalid on both interfaces."""
    assert valid_parentheses(s) is False
    assert Solution().isValid(s) is False


@pytest.mark.unit
@pytest.mark.parametrize("s", ["(()", "())", "()(", "()[]{", "[({})"])
def test_odd_length_strings_are_invalid(s: str) -> None:
    """Odd-length bracket strings cannot balance and return False."""
    assert valid_parentheses(s) is False
    assert Solution().isValid(s) is False


@pytest.mark.unit
@pytest.mark.parametrize("s", [")(", "][", "}{", ")()(", ")[]{}"])
def test_closing_before_opening_strings_are_invalid(s: str) -> None:
    """Strings starting with or reversing closing brackets return False."""
    assert valid_parentheses(s) is False
    assert Solution().isValid(s) is False


@pytest.mark.unit
@pytest.mark.parametrize(
    "s",
    ["((((", "(([{{", "([{", "))))", "]]}}", "()))", "()[]}"],
)
def test_all_opening_and_trailing_close_strings_are_invalid(s: str) -> None:
    """Unclosed opening runs and trailing unmatched closing brackets return False."""
    assert valid_parentheses(s) is False
    assert Solution().isValid(s) is False


@pytest.mark.unit
@pytest.mark.parametrize(
    ("s", "expected"),
    [
        ("()", True),
        ("(]", False),
        ("(}", False),
        ("[)", False),
        ("[]", True),
        ("[}", False),
        ("{)", False),
        ("{]", False),
        ("{}", True),
    ],
)
def test_all_open_close_pairs(s: str, expected: bool) -> None:
    """All nine two-character open-close combinations evaluate accurately."""
    assert valid_parentheses(s) is expected
    assert Solution().isValid(s) is expected


@pytest.mark.unit
@pytest.mark.parametrize("s", ["([)]", "({[}])", "[({]})", "({)}", "[(])"])
def test_crossed_interleaved_traps_are_invalid(s: str) -> None:
    """Crossed and interleaved bracket orders return False."""
    assert valid_parentheses(s) is False
    assert Solution().isValid(s) is False


@pytest.mark.unit
@pytest.mark.parametrize(
    ("s", "expected"),
    [
        ("(((((())))))", True),
        ("({[({[()]})]})", True),
        ("(()[]){[()()]()}", True),
        ("({[()]})[", False),
        ("({[()]})]", False),
        ("(([}])", False),
    ],
)
def test_deep_nested_siblings_and_late_or_middle_mismatches(
    s: str,
    expected: bool,
) -> None:
    """Deep nesting, sibling subtrees, and late/middle mismatches evaluate cleanly."""
    assert valid_parentheses(s) is expected
    assert Solution().isValid(s) is expected


@pytest.mark.unit
@pytest.mark.parametrize(
    "invalid_s",
    [
        "a",
        " ",
        "(a)",
        "( )",
        "()x",
        "([z])",
        "{\t}",
        "(\n)",
        "()\x00",
        "\uff08)",
        "🚀",
    ],
)
def test_invalid_characters_raise_value_error(invalid_s: str) -> None:
    """Characters outside '()[]{}' with no prior mismatch raise ValueError."""
    with pytest.raises(ValueError, match=_INVALID_CHAR_PATTERN):
        valid_parentheses(invalid_s)
    with pytest.raises(ValueError, match=_INVALID_CHAR_PATTERN):
        Solution().isValid(invalid_s)


# ---------------------------------------------------------------------------
# Tier 3: Invariants (Hypothesis property-based tests & differential oracle)
# ---------------------------------------------------------------------------


def _valid_dyck_strings() -> st.SearchStrategy[str]:
    """Builds a bounded recursive Hypothesis strategy for valid bracket strings.

    Returns:
        A Hypothesis search strategy generating balanced strings over ``"()[]{}"``.
    """
    base = st.sampled_from(["", "()", "[]", "{}"])

    def _extend(children: st.SearchStrategy[str]) -> st.SearchStrategy[str]:
        wrapped = st.tuples(st.sampled_from(_BRACKET_PAIRS), children).map(
            lambda pair: f"{pair[0][0]}{pair[1]}{pair[0][1]}"
        )
        concatenated = st.tuples(children, children).map(
            lambda pair: f"{pair[0]}{pair[1]}"
        )
        return st.one_of(wrapped, concatenated)

    return st.recursive(base, _extend, max_leaves=15)


def _oracle_valid_parentheses(s: str) -> bool:
    """Computes bracket validity via iterative innermost-pair reduction.

    Args:
        s: Input string consisting exclusively of characters in ``"()[]{}"``.

    Returns:
        ``True`` if ``s`` reduces completely to ``""``, ``False`` otherwise.
    """
    cur = s
    while "()" in cur or "[]" in cur or "{}" in cur:
        cur = cur.replace("()", "").replace("[]", "").replace("{}", "")
    return not cur


@pytest.mark.unit
@given(s=_valid_dyck_strings(), other=_valid_dyck_strings())
def test_hypothesis_valid_dyck_closure_invariants(s: str, other: str) -> None:
    """Valid Dyck strings pass both APIs and remain valid under wrap and concat."""
    assert valid_parentheses(s) is True
    assert Solution().isValid(s) is True

    for open_b, close_b in _BRACKET_PAIRS:
        wrapped = f"{open_b}{s}{close_b}"
        assert valid_parentheses(wrapped) is True
        assert Solution().isValid(wrapped) is True

    concatenated = f"{s}{other}"
    assert valid_parentheses(concatenated) is True
    assert Solution().isValid(concatenated) is True


@pytest.mark.unit
@given(s=_valid_dyck_strings())
def test_hypothesis_single_deletion_or_corruption_invalidates(s: str) -> None:
    """Deleting or corrupting any single bracket in a valid Dyck string yields False."""
    assert valid_parentheses(s) is True
    assert Solution().isValid(s) is True

    for idx, original in enumerate(s):
        deleted = f"{s[:idx]}{s[idx + 1 :]}"
        assert valid_parentheses(deleted) is False
        assert Solution().isValid(deleted) is False

        for replacement in _BRACKETS:
            if replacement != original:
                corrupted = f"{s[:idx]}{replacement}{s[idx + 1 :]}"
                assert valid_parentheses(corrupted) is False
                assert Solution().isValid(corrupted) is False


@pytest.mark.unit
@given(s=st.text(alphabet=_BRACKETS, max_size=30))
def test_hypothesis_matches_pair_reduction_oracle(s: str) -> None:
    """Differential check against iterative pair reduction and Solution parity."""
    expected = _oracle_valid_parentheses(s)
    assert valid_parentheses(s) is expected
    assert Solution().isValid(s) is expected


@pytest.mark.unit
@given(
    prefix=_valid_dyck_strings(),
    bad_char=st.characters().filter(lambda c: c not in _BRACKETS),
    suffix=st.text(max_size=10),
)
def test_hypothesis_non_bracket_after_valid_prefix_raises_value_error(
    prefix: str,
    bad_char: str,
    suffix: str,
) -> None:
    """Any non-bracket character after a valid prefix raises ValueError on both APIs."""
    candidate = f"{prefix}{bad_char}{suffix}"
    with pytest.raises(ValueError, match=_INVALID_CHAR_PATTERN):
        valid_parentheses(candidate)
    with pytest.raises(ValueError, match=_INVALID_CHAR_PATTERN):
        Solution().isValid(candidate)


# ---------------------------------------------------------------------------
# Tier 4: Stress workloads and online-judge simulation
# ---------------------------------------------------------------------------


@pytest.mark.stress
def test_max_leetcode_constraint_workloads_run_in_linear_time() -> None:
    """Length-10,000 valid, late-mismatch, and all-opening workloads stay fast."""
    deep_valid = "(" * 5_000 + ")" * 5_000
    last_mismatch = "(" * 5_000 + ")" * 4_999 + "]"
    all_opening = "(" * 10_000

    for candidate, expected in (
        (deep_valid, True),
        (last_mismatch, False),
        (all_opening, False),
    ):
        start = time.perf_counter()
        result = valid_parentheses(candidate)
        elapsed = time.perf_counter() - start
        assert result is expected
        assert elapsed < 0.5
        assert Solution().isValid(candidate) is expected


@pytest.mark.stress
def test_large_flat_and_deep_nesting_avoid_recursion_and_quadratic_blowup() -> None:
    """Length-100,000+ flat and deep workloads finish iteratively without blowup."""
    flat_repeated = "()[]{}" * 16_667
    deep_nested = "(" * 50_000 + ")" * 50_000

    for candidate in (flat_repeated, deep_nested):
        start = time.perf_counter()
        result = valid_parentheses(candidate)
        elapsed = time.perf_counter() - start
        assert result is True
        assert elapsed < 0.5
        assert Solution().isValid(candidate) is True


@pytest.mark.unit
def test_oj_simulation_table() -> None:
    """LeetCode wrapper evaluates a representative table of online-judge inputs."""
    oj_cases = [
        ("", True),
        ("()", True),
        ("()[]{}", True),
        ("(]", False),
        ("([])", True),
        ("([)]", False),
        ("{[]}", True),
        ("(", False),
        ("]", False),
        (")(", False),
        ("([{", False),
        ("(((((())))))", True),
        ("((({{{[[[]]]}}})))", True),
        ("((({{{[[[]]]}}})", False),
        ("({[({[()]})]})", True),
        ("(()[]){[()()]()}", True),
    ]

    for case, expected in oj_cases:
        assert Solution().isValid(case) is expected
