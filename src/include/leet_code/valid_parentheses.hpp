/// @file valid_parentheses.hpp
/// @brief 20. Valid Parentheses (Easy)
///
/// https://leetcode.com/problems/valid-parentheses/
///
/// Given a string `s` containing just the characters `'('`, `')'`, `'{'`,
/// `'}'`, `'['` and `']'`, determine if the input string is valid.
///
/// An input string is valid if:
/// 1. Open brackets must be closed by the same type of brackets.
/// 2. Open brackets must be closed in the correct order.
/// 3. Every close bracket has a corresponding open bracket of the same type.
///
/// **Example 1:**
///
/// Input: `s = "()"`
/// Output: `true`
///
/// **Example 2:**
///
/// Input: `s = "()[]{}"`
/// Output: `true`
///
/// **Example 3:**
///
/// Input: `s = "(]"`
/// Output: `false`
///
/// **Example 4:**
///
/// Input: `s = "([])"`
/// Output: `true`
///
/// **Example 5:**
///
/// Input: `s = "([)]"`
/// Output: `false`
///
/// **Constraints:**
///
/// * `1 <= s.length <= 10^4`
/// * `s` consists of parentheses only `'()[]{}'`.
///
/// **Target Complexity:**
/// * Time: `O(n)`
/// * Space: `O(n)`

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace leet_code {

/// @brief Error codes returned by `valid_parentheses`.
enum class ValidParenthesesError : uint8_t {
  /// Input contains a character outside `'()[]{}'`.
  kInvalidCharacter,
  /// Function stub has not yet been implemented.
  kNotImplemented,
};

/// @brief Determines whether a bracket string is balanced and properly nested.
///
/// Valid alphabet consists strictly of `'('`, `')'`, `'{'`, `'}'`, `'['`, and
/// `']'`.
///
/// Preconditions and contract:
/// - An empty string `""` is vacuously valid and returns `true`.
/// - Any character outside `'()[]{}'` returns
///   `std::unexpected(ValidParenthesesError::kInvalidCharacter)`.
///
/// @param s Non-owning view of the input bracket string.
/// @return `true` if `s` is a valid bracket sequence (including `""`), `false`
///         if brackets are unbalanced or mismatched, or a
///         `ValidParenthesesError` on invalid characters or unimplemented stub.
[[nodiscard]] auto valid_parentheses(std::string_view s)
    -> std::expected<bool, ValidParenthesesError>;

/// @brief Official LeetCode #20 Solution API wrapper.
class Solution {
 public:
  /// @brief Checks whether the bracket sequence `s` is valid.
  ///
  /// Delegates to `leet_code::valid_parentheses(s)` and returns `false` if an
  /// error occurs (such as a character outside `'()[]{}'`). Does not throw
  /// exceptions. An empty string `""` is vacuously valid and returns `true`.
  ///
  /// @param s Input string of bracket characters.
  /// @return `true` if `s` is a valid bracket sequence (including `""`),
  ///         `false` otherwise.
  [[nodiscard]] auto isValid(std::string s) -> bool;
};

}  // namespace leet_code
