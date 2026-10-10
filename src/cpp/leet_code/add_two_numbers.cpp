/**
 * @file add_two_numbers.cpp
 * @brief Barebones stub implementation of Add Two Numbers algorithm.
 *
 * Designed to compile cleanly under strict compiler flags (-Wall, -Wextra,
 * -Wpedantic, -Wconversion, -Werror) while returning a dummy value (nullptr) to
 * establish the initial Test-Driven Development (TDD) Red state.
 */

#include "leet_code/add_two_numbers.hpp"

#include "leet_code/list_node.hpp"

namespace leet_code {

[[nodiscard]] auto add_two_numbers(const ListNode* list1, const ListNode* list2)
    -> ListNode* {
  ListNode dummy{0};
  ListNode* tail = &dummy;
  int carry = 0;

  while (list1 != nullptr || list2 != nullptr || carry != 0) {
    int sum = carry;
    if (list1 != nullptr) {
      sum += list1->val;
      list1 = list1->next;
    }
    if (list2 != nullptr) {
      sum += list2->val;
      list2 = list2->next;
    }
    carry = sum / 10;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    tail->next = new ListNode(sum % 10);
    tail = tail->next;
  }

  return dummy.next;
}

[[nodiscard]] auto Solution::addTwoNumbers(ListNode* list1, ListNode* list2)
    -> ListNode* {  // NOLINT
  return add_two_numbers(list1, list2);
}

}  // namespace leet_code
