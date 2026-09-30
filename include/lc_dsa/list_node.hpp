/**
 * @file list_node.hpp
 * @brief Canonical LeetCode singly-linked list node and memory-safe RAII
 * utilities.
 *
 * Provides the standard LeetCode ListNode structure and ScopedLinkedList RAII
 * wrapper ensuring zero memory leaks under AddressSanitizer and preventing call
 * stack overflow via non-recursive, iterative deallocation.
 */

#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace lc_dsa {

/**
 * @brief Singly-linked list node compatible with LeetCode canonical definition.
 */
struct ListNode {
  int val{0};
  ListNode* next{nullptr};

  constexpr ListNode() noexcept = default;
  constexpr explicit ListNode(int value) noexcept : val(value) {}
  constexpr ListNode(int value, ListNode* next_node) noexcept
      : val(value), next(next_node) {}
};

/**
 * @brief Iteratively frees all nodes in a singly-linked list to prevent stack
 * overflow.
 *
 * Traverses iteratively to guarantee O(1) auxiliary stack space, ensuring
 * safety for arbitrary list lengths (e.g. 50,000+ nodes) under
 * AddressSanitizer.
 *
 * @param head Pointer to the head of the linked list. May be nullptr.
 * @note Operates in O(n) time and O(1) auxiliary space without recursion.
 * Assumes acyclic list.
 */
inline void free_linked_list(ListNode* head) noexcept {
  while (head != nullptr) {
    ListNode* next_node = head->next;
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    delete head;
    head = next_node;
  }
}

/**
 * @brief Constructs a singly-linked list from a contiguous span of integers.
 *
 * Provides strong exception safety: if an allocation fails midway
 * (std::bad_alloc), all previously allocated nodes are cleanly freed before
 * propagating the exception.
 *
 * @param values Non-owning view of contiguous integers to populate nodes.
 * @return ListNode* Head of the newly allocated linked list, or nullptr if
 * values is empty.
 * @throws std::bad_alloc If heap allocation fails.
 */
[[nodiscard]] inline auto create_linked_list(std::span<const int> values)
    -> ListNode* {
  if (values.empty()) {
    return nullptr;
  }

  ListNode dummy{0};
  ListNode* tail = &dummy;

  try {
    for (const int value : values) {
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      tail->next = new ListNode(value);
      tail = tail->next;
    }
  } catch (...) {
    free_linked_list(dummy.next);
    throw;
  }

  return dummy.next;
}

/**
 * @brief Constructs a singly-linked list from an initializer list of integers.
 *
 * Overload enabling ergonomic syntax for tests and benchmarks:
 * create_linked_list({2, 4, 3}).
 *
 * @param values Initializer list of integer values.
 * @return ListNode* Head of the newly allocated linked list, or nullptr if
 * empty.
 * @throws std::bad_alloc If heap allocation fails.
 */
[[nodiscard]] inline auto create_linked_list(std::initializer_list<int> values)
    -> ListNode* {
  return create_linked_list(
      std::span<const int>(values.begin(), values.size()));
}

/**
 * @brief Converts a singly-linked list into a std::vector of integers.
 *
 * @param head Pointer to the head of the linked list. May be nullptr.
 * @return std::vector<int> Elements of the linked list in traversal order.
 */
[[nodiscard]] inline auto linked_list_to_vector(const ListNode* head)
    -> std::vector<int> {
  std::vector<int> result;
  for (const ListNode* current = head; current != nullptr;
       current = current->next) {
    result.push_back(current->val);
  }
  return result;
}

/**
 * @brief Move-only RAII owning container for managing heap-allocated LeetCode
 * linked lists.
 *
 * Automatically frees the managed linked list upon destruction using
 * free_linked_list, preventing memory leaks under AddressSanitizer. Follows the
 * Rule of 5 (move-only semantics).
 */
class ScopedLinkedList {
 public:
  constexpr ScopedLinkedList() noexcept = default;

  explicit ScopedLinkedList(ListNode* head) noexcept : head_(head) {}

  explicit ScopedLinkedList(std::span<const int> values)
      : head_(create_linked_list(values)) {}

  ScopedLinkedList(std::initializer_list<int> values)
      : head_(create_linked_list(values)) {}

  ~ScopedLinkedList() noexcept { free_linked_list(head_); }

  ScopedLinkedList(const ScopedLinkedList&) = delete;
  auto operator=(const ScopedLinkedList&) -> ScopedLinkedList& = delete;

  ScopedLinkedList(ScopedLinkedList&& other) noexcept
      : head_(other.release()) {}

  auto operator=(ScopedLinkedList&& other) noexcept -> ScopedLinkedList& {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  [[nodiscard]] auto get() const noexcept -> ListNode* { return head_; }

  [[nodiscard]] auto release() noexcept -> ListNode* {
    return std::exchange(head_, nullptr);
  }

  void reset(ListNode* new_head = nullptr) noexcept {
    if (head_ == new_head) {
      return;
    }
    ListNode* old_head = std::exchange(head_, new_head);
    free_linked_list(old_head);
  }

  [[nodiscard]] explicit operator bool() const noexcept {
    return head_ != nullptr;
  }

  [[nodiscard]] auto operator->() const noexcept -> ListNode* { return head_; }

  [[nodiscard]] auto operator*() const noexcept -> ListNode& { return *head_; }

  [[nodiscard]] auto to_vector() const -> std::vector<int> {
    return linked_list_to_vector(head_);
  }

 private:
  ListNode* head_{nullptr};
};

}  // namespace lc_dsa
