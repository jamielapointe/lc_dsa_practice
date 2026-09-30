/**
 * @file list_node_test.cpp
 * @brief Comprehensive 4-Tier Google Test suite for ListNode and
 * ScopedLinkedList.
 *
 * Conforms to PROJECT.md and TEST_INFRA standards:
 * - Tier 1: Canonical Feature Coverage (Struct layout, constexpr constructors,
 * conversions)
 * - Tier 2: Boundary, Corner & Edge Cases (Empty lists, single elements,
 * extreme numbers)
 * - Tier 3: Memory Lifecycle, Move Semantics & Invariants (Move semantics,
 * reset, release)
 * - Tier 4: Large-Scale Workloads & Sanitizer Stress Testing (Deep lists, churn
 * loops)
 */

#include "lc_dsa/list_node.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

// ============================================================================
// TIER 1: CANONICAL FEATURE COVERAGE
// ============================================================================

TEST(ListNodeTest, DefaultConstructorInitializesZeroAndNull) {
  constexpr lc_dsa::ListNode node{};
  static_assert(node.val == 0);
  static_assert(node.next == nullptr);
  EXPECT_EQ(node.val, 0);
  EXPECT_EQ(node.next, nullptr);
}

TEST(ListNodeTest, SingleValueConstructorInitializesValAndNullNext) {
  constexpr lc_dsa::ListNode node{42};
  static_assert(node.val == 42);
  static_assert(node.next == nullptr);
  EXPECT_EQ(node.val, 42);
  EXPECT_EQ(node.next, nullptr);
}

TEST(ListNodeTest, TwoArgumentConstructorLinksNodes) {
  lc_dsa::ListNode next_node{99};
  lc_dsa::ListNode node{42, &next_node};
  EXPECT_EQ(node.val, 42);
  ASSERT_NE(node.next, nullptr);
  EXPECT_EQ(node.next->val, 99);
  EXPECT_EQ(node.next->next, nullptr);
}

TEST(LinkedListHelpersTest, SingleElementRoundTrip) {
  const std::vector<int> values{42};
  lc_dsa::ListNode* head = lc_dsa::create_linked_list(values);
  ASSERT_NE(head, nullptr);
  EXPECT_EQ(head->val, 42);
  EXPECT_EQ(head->next, nullptr);

  const auto round_trip = lc_dsa::linked_list_to_vector(head);
  EXPECT_EQ(round_trip, values);
  lc_dsa::free_linked_list(head);
}

TEST(LinkedListHelpersTest, MultiElementRoundTrip) {
  const std::vector<int> values{1, 2, 3, 4, 5};
  lc_dsa::ListNode* head = lc_dsa::create_linked_list(values);
  ASSERT_NE(head, nullptr);

  // Verify explicit pointer linkage
  lc_dsa::ListNode* current = head;
  for (const int expected_val : values) {
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->val, expected_val);
    current = current->next;
  }
  EXPECT_EQ(current, nullptr);

  const auto round_trip = lc_dsa::linked_list_to_vector(head);
  EXPECT_EQ(round_trip, values);
  lc_dsa::free_linked_list(head);
}

TEST(LinkedListHelpersTest, InitializerListOverload) {
  lc_dsa::ListNode* head = lc_dsa::create_linked_list({10, 20, 30});
  ASSERT_NE(head, nullptr);
  const auto round_trip = lc_dsa::linked_list_to_vector(head);
  const std::vector<int> expected{10, 20, 30};
  EXPECT_EQ(round_trip, expected);
  lc_dsa::free_linked_list(head);
}

TEST(ScopedLinkedListTest, ConstructFromRawPointer) {
  // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
  auto* raw = new lc_dsa::ListNode(100);
  {
    const lc_dsa::ScopedLinkedList list(raw);
    EXPECT_EQ(list.get(), raw);
    EXPECT_TRUE(list);
    EXPECT_EQ(list->val, 100);
    EXPECT_EQ((*list).val, 100);
    EXPECT_EQ(list.to_vector(), (std::vector<int>{100}));
  }  // Cleanly deallocated on scope exit
}

TEST(ScopedLinkedListTest, ConstructFromInitializerList) {
  {
    const lc_dsa::ScopedLinkedList list{10, 20, 30, 40};
    EXPECT_TRUE(list);
    EXPECT_EQ(list.to_vector(), (std::vector<int>{10, 20, 30, 40}));
  }
}

TEST(ScopedLinkedListTest, ConstructFromSpan) {
  const std::vector<int> values{5, 10, 15};
  {
    const lc_dsa::ScopedLinkedList list(std::span<const int>{values});
    EXPECT_TRUE(list);
    EXPECT_EQ(list.to_vector(), values);
  }
}

// ============================================================================
// TIER 2: BOUNDARY, CORNER & EDGE CASES
// ============================================================================

TEST(LinkedListHelpersTest, EmptySpanCreatesNullptr) {
  const std::vector<int> empty_vals{};
  lc_dsa::ListNode* head = lc_dsa::create_linked_list(empty_vals);
  EXPECT_EQ(head, nullptr);
  EXPECT_TRUE(lc_dsa::linked_list_to_vector(head).empty());
  lc_dsa::free_linked_list(head);  // Safe no-op on nullptr
}

TEST(LinkedListHelpersTest, NonOwningSpanCompatibility) {
  const std::array<int, 4> array_input{7, 8, 9, 10};
  lc_dsa::ListNode* head = lc_dsa::create_linked_list(array_input);
  ASSERT_NE(head, nullptr);
  EXPECT_EQ(lc_dsa::linked_list_to_vector(head),
            (std::vector<int>{7, 8, 9, 10}));
  lc_dsa::free_linked_list(head);

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  int c_array[] = {100, 200};
  lc_dsa::ListNode* head2 = lc_dsa::create_linked_list(c_array);
  ASSERT_NE(head2, nullptr);
  EXPECT_EQ(lc_dsa::linked_list_to_vector(head2), (std::vector<int>{100, 200}));
  lc_dsa::free_linked_list(head2);
}

TEST(LinkedListHelpersTest, ExtremeIntegerValues) {
  const std::vector<int> extremes = {
      std::numeric_limits<int>::min(), -1, 0, 1,
      std::numeric_limits<int>::max(),
  };
  lc_dsa::ListNode* head = lc_dsa::create_linked_list(extremes);
  ASSERT_NE(head, nullptr);
  EXPECT_EQ(lc_dsa::linked_list_to_vector(head), extremes);
  lc_dsa::free_linked_list(head);
}

TEST(ScopedLinkedListTest, DefaultConstructedIsEmpty) {
  const lc_dsa::ScopedLinkedList list;
  EXPECT_EQ(list.get(), nullptr);
  EXPECT_FALSE(list);
  EXPECT_TRUE(list.to_vector().empty());
}

TEST(ScopedLinkedListTest, EmptyInitializerListIsEmpty) {
  const lc_dsa::ScopedLinkedList list{};
  EXPECT_FALSE(list);
  EXPECT_EQ(list.get(), nullptr);
  EXPECT_TRUE(list.to_vector().empty());
}

TEST(ScopedLinkedListTest, SingleElementLifecycle) {
  lc_dsa::ScopedLinkedList list{42};
  ASSERT_TRUE(list);
  EXPECT_EQ(list->val, 42);
  EXPECT_EQ(list->next, nullptr);
  EXPECT_EQ(list.to_vector(), (std::vector<int>{42}));
}

// ============================================================================
// TIER 3: MEMORY LIFECYCLE, MOVE SEMANTICS & INVARIANTS
// ============================================================================

TEST(ScopedLinkedListTest, ReleaseTransfersOwnershipToCaller) {
  lc_dsa::ListNode* extracted = nullptr;
  {
    lc_dsa::ScopedLinkedList list{1, 2, 3};
    ASSERT_TRUE(list);
    extracted = list.release();
    EXPECT_EQ(list.get(), nullptr);
    EXPECT_FALSE(list);
  }  // Destructor does not free extracted

  ASSERT_NE(extracted, nullptr);
  EXPECT_EQ(lc_dsa::linked_list_to_vector(extracted),
            (std::vector<int>{1, 2, 3}));
  lc_dsa::free_linked_list(extracted);
}

TEST(ScopedLinkedListTest, ResetToNullDeallocates) {
  lc_dsa::ScopedLinkedList list{1, 2, 3};
  ASSERT_TRUE(list);
  list.reset();  // Frees nodes
  EXPECT_EQ(list.get(), nullptr);
  EXPECT_FALSE(list);
  EXPECT_TRUE(list.to_vector().empty());
}

TEST(ScopedLinkedListTest, ResetToNewListDeallocatesOldAndAdoptsNew) {
  lc_dsa::ScopedLinkedList list{1, 2, 3};
  ASSERT_TRUE(list);
  auto* new_nodes = lc_dsa::create_linked_list({4, 5, 6, 7});
  list.reset(new_nodes);  // Frees {1, 2, 3} and adopts new_nodes
  EXPECT_EQ(list.get(), new_nodes);
  EXPECT_EQ(list.to_vector(), (std::vector<int>{4, 5, 6, 7}));
}

TEST(ScopedLinkedListTest, ResetSelfIsSafeNoOp) {
  lc_dsa::ScopedLinkedList list{42, 43};
  ASSERT_TRUE(list);
  auto* original_head = list.get();
  list.reset(list.get());  // Must be safe no-op
  EXPECT_EQ(list.get(), original_head);
  EXPECT_EQ(list.to_vector(), (std::vector<int>{42, 43}));
}

TEST(ScopedLinkedListTest, MoveConstructorTransfersOwnership) {
  lc_dsa::ScopedLinkedList source{1, 2, 3, 4, 5};
  const lc_dsa::ListNode* original_head = source.get();
  ASSERT_NE(original_head, nullptr);

  lc_dsa::ScopedLinkedList destination(std::move(source));
  EXPECT_EQ(destination.get(), original_head);
  EXPECT_TRUE(destination);
  EXPECT_EQ(destination.to_vector(), (std::vector<int>{1, 2, 3, 4, 5}));

  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_EQ(source.get(), nullptr);
  EXPECT_FALSE(source);
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

TEST(ScopedLinkedListTest, MoveAssignmentFreesTargetAndAdoptsSource) {
  lc_dsa::ScopedLinkedList target{10, 20};
  lc_dsa::ScopedLinkedList source{30, 40, 50};
  const lc_dsa::ListNode* source_head = source.get();

  target = std::move(source);
  EXPECT_EQ(target.get(), source_head);
  EXPECT_EQ(target.to_vector(), (std::vector<int>{30, 40, 50}));

  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_EQ(source.get(), nullptr);
  EXPECT_FALSE(source);
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

TEST(ScopedLinkedListTest, SelfMoveAssignmentSafety) {
  lc_dsa::ScopedLinkedList list{1, 2, 3};
  const lc_dsa::ListNode* head_before = list.get();
  ASSERT_NE(head_before, nullptr);

  // Use pointer aliasing to avoid -Wself-move compiler diagnostic
  auto* alias = &list;
  *alias = std::move(list);

  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  EXPECT_EQ(list.get(), head_before);
  EXPECT_TRUE(list);
  EXPECT_EQ(list.to_vector(), (std::vector<int>{1, 2, 3}));
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}

// ============================================================================
// TIER 4: LARGE-SCALE WORKLOADS & SANITIZER STRESS TESTING
// ============================================================================

TEST(LinkedListStressTest, DeepListIterativeDestructionAvoidsStackOverflow) {
  // Test with 50,000 nodes. Recursive destruction would overflow stack.
  constexpr std::size_t kLargeSize = 50'000;
  std::vector<int> large_vec(kLargeSize, 1);

  {
    const lc_dsa::ScopedLinkedList deep_list(large_vec);
    ASSERT_TRUE(deep_list);
    EXPECT_EQ(deep_list->val, 1);
  }  // Destruction must execute with O(1) stack frames.
}

TEST(LinkedListStressTest, ReallocationAndChurnLoopUnderASan) {
  // Repeatedly allocate, move, and reset in a tight loop to stress ASan
  // quarantine & leak detection
  for (int iter = 0; iter < 500; ++iter) {
    lc_dsa::ScopedLinkedList list_a{iter, iter + 1, iter + 2};
    lc_dsa::ScopedLinkedList list_b;
    list_b = std::move(list_a);
    list_b.reset(lc_dsa::create_linked_list({iter + 3, iter + 4}));
    EXPECT_EQ(list_b.to_vector().size(), 2U);
  }
}

}  // namespace
