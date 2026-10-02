/**
 * @file add_two_numbers.cpp
 * @brief Barebones stub implementation of Add Two Numbers algorithm.
 *
 * Designed to compile cleanly under strict compiler flags (-Wall, -Wextra,
 * -Wpedantic, -Wconversion, -Werror) while returning a dummy value (nullptr) to
 * establish the initial Test-Driven Development (TDD) Red state.
 */

#include "lc_dsa/add_two_numbers.hpp"

#include "lc_dsa/list_node.hpp"

namespace lc_dsa {

[[nodiscard]] auto add_two_numbers(const ListNode* const list1,
                                   const ListNode* const list2) -> ListNode* {
  static_cast<void>(list1);
  static_cast<void>(list2);
  return nullptr;
}

[[nodiscard]] auto Solution::addTwoNumbers(ListNode* list1, ListNode* list2)
    -> ListNode* {  // NOLINT
  return add_two_numbers(list1, list2);
}

}  // namespace lc_dsa
