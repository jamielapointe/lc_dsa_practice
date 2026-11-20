/// @file valid_parentheses_test.cpp
/// @brief Comprehensive 4-tier Google Test suite for LeetCode #20 (Valid
/// Parentheses).
///
/// Tiers:
/// - Tier 1: Canonical LeetCode examples and non-owning std::string_view
///   subviews.
/// - Tier 2: Boundary, edge, structural mismatch, and invalid-character
///   precondition cases.
/// - Tier 3: Algebraic Dyck invariants and differential pair-reduction oracle.
/// - Tier 4: Max-constraint, large linear-time stress workloads, and OJ
///   simulation loop.

#include "leet_code/valid_parentheses.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view kAllBrackets = "()[]{}";
constexpr std::array<char, 3uz> kOpenBrackets = {'(', '[', '{'};
constexpr std::array<char, 3uz> kCloseBrackets = {')', ']', '}'};

/// @brief Independent iterative pair-reduction oracle for small strings.
///
/// Repeatedly erases adjacent matching pairs (`"()"`, `"[]"`, `"{}"`) until
/// a fixed point is reached, returning `true` iff the resulting string is
/// empty. Not a stack implementation; intended only for small test inputs.
[[nodiscard]] auto reduce_valid_oracle(std::string_view input) -> bool {
  constexpr std::array<std::string_view, 3uz> kPairs = {"()", "[]", "{}"};
  std::string reduced(input);
  bool changed = true;
  while (changed) {
    changed = false;
    for (const std::string_view pair : kPairs) {
      const size_t pos = reduced.find(pair);
      if (pos != std::string::npos) {
        reduced.erase(pos, 2uz);
        changed = true;
      }
    }
  }
  return reduced.empty();
}

/// @brief Generates a valid Dyck-3 string with `pair_count` bracket pairs.
[[nodiscard]] auto generate_valid_dyck(std::mt19937_64& rng, size_t pair_count)
    -> std::string {
  std::string out;
  out.reserve(pair_count * 2uz);
  std::vector<char> pending_closes;
  pending_closes.reserve(pair_count);

  std::uniform_int_distribution<size_t> bracket_dist(0uz, 2uz);
  std::uniform_int_distribution<int> action_dist(0, 1);

  size_t remaining_opens = pair_count;
  while (remaining_opens > 0uz || !pending_closes.empty()) {
    const bool can_open = remaining_opens > 0uz;
    const bool can_close = !pending_closes.empty();
    const bool choose_open = can_open && (!can_close || action_dist(rng) == 0);
    if (choose_open) {
      const size_t idx = bracket_dist(rng);
      out.push_back(kOpenBrackets[idx]);
      pending_closes.push_back(kCloseBrackets[idx]);
      --remaining_opens;
    } else {
      out.push_back(pending_closes.back());
      pending_closes.pop_back();
    }
  }
  return out;
}

/// @brief Builds a deep-nested valid bracket sequence of `2 * half_depth`
/// chars.
[[nodiscard]] auto make_deep_nested_valid(size_t half_depth) -> std::string {
  std::string out;
  out.reserve(half_depth * 2uz);
  for (size_t i = 0uz; i < half_depth; ++i) {
    out.push_back(kOpenBrackets[i % kOpenBrackets.size()]);
  }
  for (size_t i = half_depth; i > 0uz; --i) {
    out.push_back(kCloseBrackets[(i - 1uz) % kCloseBrackets.size()]);
  }
  return out;
}

void ExpectBoolResult(std::string_view input, bool expected) {
  const auto res = leet_code::valid_parentheses(input);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, expected);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(input)), expected);
}

void ExpectInvalidCharResult(std::string_view input) {
  const auto res = leet_code::valid_parentheses(input);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), leet_code::ValidParenthesesError::kInvalidCharacter);

  leet_code::Solution solution;
  EXPECT_FALSE(solution.isValid(std::string(input)));
}

// ============================================================================
// TIER 1: CANONICAL FEATURE COVERAGE
// ============================================================================

TEST(ValidParenthesesTest, Example1Canonical) {
  constexpr std::string_view kInput = "()";
  const auto res = leet_code::valid_parentheses(kInput);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, true);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(kInput)), true);
}

TEST(ValidParenthesesTest, Example2Canonical) {
  constexpr std::string_view kInput = "()[]{}";
  const auto res = leet_code::valid_parentheses(kInput);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, true);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(kInput)), true);
}

TEST(ValidParenthesesTest, Example3Canonical) {
  constexpr std::string_view kInput = "(]";
  const auto res = leet_code::valid_parentheses(kInput);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, false);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(kInput)), false);
}

TEST(ValidParenthesesTest, Example4Canonical) {
  constexpr std::string_view kInput = "([])";
  const auto res = leet_code::valid_parentheses(kInput);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, true);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(kInput)), true);
}

TEST(ValidParenthesesTest, Example5Canonical) {
  constexpr std::string_view kInput = "([)]";
  const auto res = leet_code::valid_parentheses(kInput);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, false);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(kInput)), false);
}

TEST(ValidParenthesesTest, NonOwningStringViewSlice) {
  const std::string_view subview =
      std::string_view("xx([])yy").substr(2uz, 4uz);
  const auto res = leet_code::valid_parentheses(subview);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(*res, true);

  leet_code::Solution solution;
  EXPECT_EQ(solution.isValid(std::string(subview)), true);
}

// ============================================================================
// TIER 2: BOUNDARY, EDGE & PRECONDITION CASES
// ============================================================================

TEST(ValidParenthesesTest, EmptyStringIsVacuouslyValid) {
  ExpectBoolResult("", true);
}

TEST(ValidParenthesesTest, SingleBracketsAreInvalid) {
  constexpr std::array<std::string_view, 6uz> kSingles = {"(", ")", "[",
                                                          "]", "{", "}"};
  for (const std::string_view input : kSingles) {
    SCOPED_TRACE(input);
    ExpectBoolResult(input, false);
  }
}

TEST(ValidParenthesesTest, OddLengthStringsAreInvalid) {
  constexpr std::array<std::string_view, 5uz> kOddCases = {
      "(()", "())", "()(", "()[]{", "[({})",
  };
  for (const std::string_view input : kOddCases) {
    SCOPED_TRACE(input);
    ExpectBoolResult(input, false);
  }
}

TEST(ValidParenthesesTest, ReversedAndClosingBeforeOpening) {
  constexpr std::array<std::string_view, 5uz> kReversedCases = {
      ")(", "][", "}{", ")()(", ")[]{}",
  };
  for (const std::string_view input : kReversedCases) {
    SCOPED_TRACE(input);
    ExpectBoolResult(input, false);
  }
}

TEST(ValidParenthesesTest, AllOpeningAndAllClosingAndTrailingClose) {
  constexpr std::array<std::string_view, 7uz> kUnbalancedCases = {
      "((((", "(([{{", "([{", "))))", "]]}}", "()))", "()[]}",
  };
  for (const std::string_view input : kUnbalancedCases) {
    SCOPED_TRACE(input);
    ExpectBoolResult(input, false);
  }
}

TEST(ValidParenthesesTest, AllNineOpenClosePairs) {
  struct PairCase {
    std::string_view input;
    bool expected = false;
  };
  constexpr std::array<PairCase, 9uz> kPairCases = {{
      {.input = "()", .expected = true},
      {.input = "(]", .expected = false},
      {.input = "(}", .expected = false},
      {.input = "[)", .expected = false},
      {.input = "[]", .expected = true},
      {.input = "[}", .expected = false},
      {.input = "{)", .expected = false},
      {.input = "{]", .expected = false},
      {.input = "{}", .expected = true},
  }};
  for (const auto& test_case : kPairCases) {
    SCOPED_TRACE(test_case.input);
    ExpectBoolResult(test_case.input, test_case.expected);
  }
}

TEST(ValidParenthesesTest, CrossedAndInterleavedTraps) {
  constexpr std::array<std::string_view, 5uz> kCrossedCases = {
      "([)]", "({[}])", "[({]})", "({)}", "[(])",
  };
  for (const std::string_view input : kCrossedCases) {
    SCOPED_TRACE(input);
    ExpectBoolResult(input, false);
  }
}

TEST(ValidParenthesesTest, DeepNestingSiblingsAndLateOrMiddleMismatches) {
  struct StructuralCase {
    std::string_view input;
    bool expected = false;
  };
  constexpr std::array<StructuralCase, 6uz> kCases = {{
      {.input = "(((((())))))", .expected = true},
      {.input = "({[({[()]})]})", .expected = true},
      {.input = "(()[]){[()()]()}", .expected = true},
      {.input = "({[()]})[", .expected = false},
      {.input = "({[()]})]", .expected = false},
      {.input = "(([}])", .expected = false},
  }};
  for (const auto& test_case : kCases) {
    SCOPED_TRACE(test_case.input);
    ExpectBoolResult(test_case.input, test_case.expected);
  }
}

TEST(ValidParenthesesTest, RejectsInvalidCharacters) {
  const std::vector<std::string_view> invalid_inputs = {
      "a",    " ",    "(a)",
      "( )",  "()x",  "([z])",
      "{\t}", "(\n)", std::string_view("()\0", 3uz),
      "（)",  "🚀",
  };
  for (size_t i = 0uz; i < invalid_inputs.size(); ++i) {
    SCOPED_TRACE(i);
    ExpectInvalidCharResult(invalid_inputs[i]);
  }
}

// ============================================================================
// TIER 3: ALGEBRAIC INVARIANTS & DIFFERENTIAL REFERENCE ORACLE
// ============================================================================

TEST(ValidParenthesesTest, RandomDyckAlgebraicInvariantsAndSolutionParity) {
  constexpr auto kSeed = uint64_t{0x2026111900000020};
  constexpr size_t kSampleCount = 150uz;
  constexpr std::array<std::pair<char, char>, 3uz> kWrappers = {{
      {'(', ')'},
      {'[', ']'},
      {'{', '}'},
  }};

  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<size_t> pair_count_dist(1uz, 7uz);
  std::uniform_int_distribution<size_t> concat_pair_dist(0uz, 6uz);
  leet_code::Solution solution;

  for (size_t trial = 0uz; trial < kSampleCount; ++trial) {
    SCOPED_TRACE(trial);
    const std::string s = generate_valid_dyck(rng, pair_count_dist(rng));
    ASSERT_TRUE(reduce_valid_oracle(s));

    const auto res = leet_code::valid_parentheses(s);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(*res);

    EXPECT_EQ(solution.isValid(s), *res);

    for (const auto& [open_ch, close_ch] : kWrappers) {
      std::string wrapped;
      wrapped.reserve(s.size() + 2uz);
      wrapped.push_back(open_ch);
      wrapped.append(s);
      wrapped.push_back(close_ch);
      ASSERT_TRUE(reduce_valid_oracle(wrapped));
      const auto wrapped_res = leet_code::valid_parentheses(wrapped);
      ASSERT_TRUE(wrapped_res.has_value());
      EXPECT_TRUE(*wrapped_res);
    }

    const std::string t = generate_valid_dyck(rng, concat_pair_dist(rng));
    std::string concat;
    concat.reserve(s.size() + t.size());
    concat.append(s);
    concat.append(t);
    ASSERT_TRUE(reduce_valid_oracle(concat));
    const auto concat_res = leet_code::valid_parentheses(concat);
    ASSERT_TRUE(concat_res.has_value());
    EXPECT_TRUE(*concat_res);

    for (size_t pos = 0uz; pos < s.size(); ++pos) {
      std::string deleted = s;
      deleted.erase(pos, 1uz);
      ASSERT_FALSE(reduce_valid_oracle(deleted));
      const auto deleted_res = leet_code::valid_parentheses(deleted);
      ASSERT_TRUE(deleted_res.has_value());
      EXPECT_FALSE(*deleted_res);

      for (const char replacement : kAllBrackets) {
        if (replacement == s[pos]) {
          continue;
        }
        std::string corrupted = s;
        corrupted[pos] = replacement;
        ASSERT_FALSE(reduce_valid_oracle(corrupted));
        const auto corrupted_res = leet_code::valid_parentheses(corrupted);
        ASSERT_TRUE(corrupted_res.has_value());
        EXPECT_FALSE(*corrupted_res);
      }
    }
  }
}

TEST(ValidParenthesesTest, RandomStringsMatchPairReductionOracle) {
  constexpr auto kSeed = uint64_t{0x5EED002020260002};
  constexpr size_t kTrialCount = 200uz;

  std::mt19937_64 rng(kSeed);
  std::uniform_int_distribution<size_t> len_dist(0uz, 24uz);
  std::uniform_int_distribution<size_t> char_dist(0uz,
                                                  kAllBrackets.size() - 1uz);
  leet_code::Solution solution;

  for (size_t trial = 0uz; trial < kTrialCount; ++trial) {
    SCOPED_TRACE(trial);
    const size_t len = len_dist(rng);
    std::string candidate;
    candidate.reserve(len);
    for (size_t i = 0uz; i < len; ++i) {
      candidate.push_back(kAllBrackets[char_dist(rng)]);
    }

    const bool expected = reduce_valid_oracle(candidate);
    const auto res = leet_code::valid_parentheses(candidate);
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(*res, expected);

    std::string candidate_copy = candidate;
    EXPECT_EQ(solution.isValid(candidate_copy), expected);
    EXPECT_EQ(candidate_copy, candidate);
  }
}

// ============================================================================
// TIER 4: STRESS WORKLOADS & ONLINE JUDGE SIMULATION
// ============================================================================

TEST(ValidParenthesesTest, MaxConstraint10kInputs) {
  constexpr size_t kHalfDepth = 5'000uz;

  const std::string deep_valid = make_deep_nested_valid(kHalfDepth);
  ASSERT_EQ(deep_valid.size(), 10'000uz);
  {
    const auto res = leet_code::valid_parentheses(deep_valid);
    ASSERT_TRUE(res.has_value());
    EXPECT_TRUE(*res);
  }

  std::string last_mismatch = deep_valid;
  last_mismatch.back() = (last_mismatch.back() == ')') ? ']' : ')';
  {
    const auto res = leet_code::valid_parentheses(last_mismatch);
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(*res);
  }

  std::string all_opening;
  all_opening.reserve(10'000uz);
  for (size_t i = 0uz; i < 10'000uz; ++i) {
    all_opening.push_back(kOpenBrackets[i % kOpenBrackets.size()]);
  }
  {
    const auto res = leet_code::valid_parentheses(all_opening);
    ASSERT_TRUE(res.has_value());
    EXPECT_FALSE(*res);
  }
}

TEST(ValidParenthesesTest, LargeLinear100kFlatRepeatedWorkload) {
  constexpr size_t kRepeatCount = 16'667uz;
  std::string flat_valid;
  flat_valid.reserve(kRepeatCount * kAllBrackets.size());
  for (size_t i = 0uz; i < kRepeatCount; ++i) {
    flat_valid.append(kAllBrackets);
  }
  ASSERT_EQ(flat_valid.size(), 100'002uz);

  const auto start = std::chrono::steady_clock::now();
  const auto res = leet_code::valid_parentheses(flat_valid);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(*res);
}

TEST(ValidParenthesesTest, LargeLinear100kDeepNestedWorkload) {
  constexpr size_t kHalfDepth = 50'000uz;
  const std::string deep_valid = make_deep_nested_valid(kHalfDepth);
  ASSERT_EQ(deep_valid.size(), 100'000uz);

  const auto start = std::chrono::steady_clock::now();
  const auto res = leet_code::valid_parentheses(deep_valid);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(*res);
}

struct TestCase {
  std::string input;
  bool expected = false;
};

TEST(ValidParenthesesTest, LeetCodeOnlineJudgeSimulationHarness) {
  const std::vector<TestCase> test_cases = {
      {.input = "", .expected = true},
      {.input = "()", .expected = true},
      {.input = "()[]{}", .expected = true},
      {.input = "(]", .expected = false},
      {.input = "([])", .expected = true},
      {.input = "([)]", .expected = false},
      {.input = "(((((())))))", .expected = true},
      {.input = "({[({[()]})]})", .expected = true},
      {.input = "(()[]){[()()]()}", .expected = true},
      {.input = "([{", .expected = false},
      {.input = ")(", .expected = false},
      {.input = "(a)", .expected = false},
      {.input = std::string("()\0", 3uz), .expected = false},
  };

  leet_code::Solution solution;
  for (size_t i = 0uz; i < test_cases.size(); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(solution.isValid(test_cases[i].input), test_cases[i].expected);
  }
}

}  // namespace
