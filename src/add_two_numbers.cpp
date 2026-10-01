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

inline void sum_carry(int& sum, int& carry) {
  carry = sum / 10;
  sum %= 10;
}

inline void sum_carry_store_result(int& sum, int& carry, ListNode*& result) {
  sum_carry(sum, carry);
  result->next = new ListNode(sum);
  result = result->next;
}

[[nodiscard]] auto add_two_numbers(const ListNode* const list1,
                                   const ListNode* const list2) -> ListNode* {
  auto* result = new ListNode(0);
  auto* result_start = result;
  int carry = 0;
  const ListNode* left = list1;
  const ListNode* right = list2;
  int base_sum = left->val + right->val + carry;
  sum_carry(base_sum, carry);
  result->val = base_sum;
  left = left->next;
  right = right->next;
  while (left != nullptr && right != nullptr) {
    int sum = left->val + right->val + carry;
    sum_carry_store_result(sum, carry, result);
    left = left->next;
    right = right->next;
  }
  while (left != nullptr) {
    int sum = left->val + carry;
    sum_carry_store_result(sum, carry, result);
    left = left->next;
  }
  while (right != nullptr) {
    int sum = right->val + carry;
    sum_carry_store_result(sum, carry, result);
    right = right->next;
  }
  if (carry > 0) {
    result->next = new ListNode(carry);
  }
  return result_start;
}

[[nodiscard]] auto Solution::addTwoNumbers(ListNode* list1, ListNode* list2)
    -> ListNode* {  // NOLINT
  return add_two_numbers(list1, list2);
}

}  // namespace lc_dsa
