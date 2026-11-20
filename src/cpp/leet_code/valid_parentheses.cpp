#include "leet_code/valid_parentheses.hpp"

#include <expected>
#include <stack>
#include <string>
#include <string_view>

namespace leet_code {

[[nodiscard]] static auto check_if_matching_bracket(char open, char close)
    -> bool {
  switch (open) {
    case '(':
      return close == ')';
    case '[':
      return close == ']';
    case '{':
      return close == '}';
    default:
      return false;
  }
}

auto valid_parentheses(std::string_view s)
    -> std::expected<bool, ValidParenthesesError> {
  std::stack<char> bracket_stack;
  for (auto const& c : s) {
    switch (c) {
      case '(':
      case '[':
      case '{':
        bracket_stack.push(c);
        break;
      case ')':
      case ']':
      case '}':
        if (bracket_stack.empty()) {
          return false;
        }
        if (!check_if_matching_bracket(bracket_stack.top(), c)) {
          return false;
        }
        bracket_stack.pop();
        break;
      default:
        return std::unexpected{ValidParenthesesError::kInvalidCharacter};
    }
  }
  return bracket_stack.empty();
}

// NOLINTBEGIN(readability-convert-member-functions-to-static): LeetCode API.
// NOLINTBEGIN(performance-unnecessary-value-param): LeetCode API.
auto Solution::isValid(std::string s) -> bool {
  return valid_parentheses(s).value_or(false);
}
// NOLINTEND(performance-unnecessary-value-param)
// NOLINTEND(readability-convert-member-functions-to-static)

}  // namespace leet_code
