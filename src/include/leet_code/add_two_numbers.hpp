/**
 * @file add_two_numbers.hpp
 * @brief Modern C++23 interface and LeetCode wrapper for Add Two Numbers.
 *
 * LeetCode #2: Add Two Numbers
 * Difficulty: Medium
 * URL: https://leetcode.com/problems/add-two-numbers/
 *
 * Problem Statement:
 * You are given two non-empty linked lists representing two non-negative
 * integers. The digits are stored in reverse order, and each of their nodes
 * contains a single digit. Add the two numbers and return the sum as a linked
 * list. You may assume the two numbers do not contain any leading zero, except
 * the number 0 itself.
 *
 * Constraints:
 * - The number of nodes in each linked list is in the range [1, 100].
 * - 0 <= Node.val <= 9
 * - It is guaranteed that the list represents a number that does not have
 * leading zeros (except for the number 0 itself).
 *
 * Complexity:
 * - Time: O(max(N, M)) where N and M are lengths of l1 and l2.
 * - Space: O(max(N, M)) for the newly created output linked list.
 */

#pragma once

#include "leet_code/list_node.hpp"

namespace leet_code {

/**
 * @brief Adds two numbers represented by linked lists with digits in reverse
 * order.
 *
 * Traverses both linked lists, sums corresponding digits along with any carry
 * from the previous addition, and creates a new result linked list.
 *
 * @param l1 Pointer to the head of the first non-empty linked list.
 * @param l2 Pointer to the head of the second non-empty linked list.
 * @return ListNode* Pointer to the head of the resulting sum linked list.
 * @note The caller assumes ownership of the returned linked list. In test code,
 *       wrap the returned pointer in `leet_code::ScopedLinkedList` to ensure
 * automatic iterative deallocation and prevent AddressSanitizer memory leaks.
 */
[[nodiscard]] auto add_two_numbers(const ListNode* list1, const ListNode* list2)
    -> ListNode*;

/**
 * @brief LeetCode official Solution class compatibility wrapper.
 *
 * Adapts add_two_numbers to conform to the standard LeetCode class method
 * signature.
 */
class Solution {
 public:
  /**
   * @brief LeetCode standard method signature for problem #2.
   *
   * @param l1 Pointer to head of first linked list.
   * @param l2 Pointer to head of second linked list.
   * @return ListNode* Pointer to head of sum linked list.
   */
  [[nodiscard]] static auto addTwoNumbers(ListNode* list1, ListNode* list2)
      -> ListNode*;  // NOLINT
};

}  // namespace leet_code
