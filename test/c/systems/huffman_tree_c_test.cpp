/**
 * @file huffman_tree_c_test.cpp
 * @brief Google Test suite for ISO C23 huffman_tree component.
 * Traceability: HUFF-REQ-001 through HUFF-REQ-008, HUFF-REQ-010, HUFF-REQ-011,
 *               HUFF-REQ-013 through HUFF-REQ-017.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
#include <systems/huffman_tree.h>
}

#include "systems_test/allocation_counter.hpp"

namespace {

// Helper to follow a bit path (MSB-first, length `bit_len` in `code`) from root
[[nodiscard]] auto follow_path(const struct huffman_node* const root,
                               const uint32_t code,
                               const uint8_t bit_len) noexcept -> const
    struct huffman_node* {
  const struct huffman_node* curr = root;
  for (uint8_t i = 0U; i < bit_len && curr != nullptr; ++i) {
    const uint32_t bit = (code >> (bit_len - 1U - i)) & 1U;
    curr = (bit == 0U) ? curr->left : curr->right;
  }
  return curr;
}

// ============================================================================
// Tier 1: Canonical Functional Tests (HUFF-REQ-001, 002, 003, 004, 017)
// ============================================================================

TEST(HuffmanTreeCTest, CanonicalExamplePromptTreeStructure) {
  RecordProperty("requirements", "HUFF-REQ-001,HUFF-REQ-002,HUFF-REQ-017");

  // Example from prompt: {'b': 2, 'e': 1, 'z': 2} (unsorted input order)
  const std::array<struct huffman_symbol_length, 3> entries = {{
      {.symbol = static_cast<uint8_t>('b'), .length = 2U},
      {.symbol = static_cast<uint8_t>('e'), .length = 1U},
      {.symbol = static_cast<uint8_t>('z'), .length = 2U},
  }};

  struct huffman_tree tree{};
  const enum huffman_error err =
      huffman_tree_build(&tree, entries.data(), entries.size());
  ASSERT_EQ(err, HUFFMAN_OK) << huffman_error_to_string(err);

  EXPECT_EQ(tree.leaf_count, 3U);
  EXPECT_EQ(tree.node_count, 5U);
  ASSERT_NE(tree.root, nullptr);
  EXPECT_FALSE(huffman_node_is_leaf(tree.root));

  // 'e' = 0 (left child of root)
  ASSERT_NE(tree.root->left, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(tree.root->left));
  EXPECT_EQ(tree.root->left->symbol, static_cast<uint8_t>('e'));
  EXPECT_EQ(tree.root->left->left, nullptr);
  EXPECT_EQ(tree.root->left->right, nullptr);

  // Internal node at '1' (right child of root)
  ASSERT_NE(tree.root->right, nullptr);
  EXPECT_FALSE(huffman_node_is_leaf(tree.root->right));

  // 'b' = 10
  ASSERT_NE(tree.root->right->left, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(tree.root->right->left));
  EXPECT_EQ(tree.root->right->left->symbol, static_cast<uint8_t>('b'));
  EXPECT_EQ(tree.root->right->left->left, nullptr);
  EXPECT_EQ(tree.root->right->left->right, nullptr);

  // 'z' = 11
  ASSERT_NE(tree.root->right->right, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(tree.root->right->right));
  EXPECT_EQ(tree.root->right->right->symbol, static_cast<uint8_t>('z'));
  EXPECT_EQ(tree.root->right->right->left, nullptr);
  EXPECT_EQ(tree.root->right->right->right, nullptr);
}

TEST(HuffmanTreeCTest, CanonicalOrderingSortsByLengthThenByByteValue) {
  RecordProperty("requirements", "HUFF-REQ-001,HUFF-REQ-002");

  // Unsorted input: {'d': 3, 'a': 2, 'c': 3, 'b': 1}
  // Canonical sorted order:
  //   'b' (len 1) -> 0
  //   'a' (len 2) -> 10
  //   'c' (len 3) -> 110
  //   'd' (len 3) -> 111
  const std::array<struct huffman_symbol_length, 4> entries = {{
      {.symbol = static_cast<uint8_t>('d'), .length = 3U},
      {.symbol = static_cast<uint8_t>('a'), .length = 2U},
      {.symbol = static_cast<uint8_t>('c'), .length = 3U},
      {.symbol = static_cast<uint8_t>('b'), .length = 1U},
  }};

  struct huffman_tree tree{};
  const enum huffman_error err =
      huffman_tree_build(&tree, entries.data(), entries.size());
  ASSERT_EQ(err, HUFFMAN_OK) << huffman_error_to_string(err);
  ASSERT_NE(tree.root, nullptr);

  const struct huffman_node* node_b = follow_path(tree.root, 0b0U, 1U);
  ASSERT_NE(node_b, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(node_b));
  EXPECT_EQ(node_b->symbol, static_cast<uint8_t>('b'));

  const struct huffman_node* node_a = follow_path(tree.root, 0b10U, 2U);
  ASSERT_NE(node_a, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(node_a));
  EXPECT_EQ(node_a->symbol, static_cast<uint8_t>('a'));

  const struct huffman_node* node_c = follow_path(tree.root, 0b110U, 3U);
  ASSERT_NE(node_c, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(node_c));
  EXPECT_EQ(node_c->symbol, static_cast<uint8_t>('c'));

  const struct huffman_node* node_d = follow_path(tree.root, 0b111U, 3U);
  ASSERT_NE(node_d, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(node_d));
  EXPECT_EQ(node_d->symbol, static_cast<uint8_t>('d'));
}

TEST(HuffmanTreeCTest, LeftAlignedNonFullTreeConstruction) {
  RecordProperty("requirements", "HUFF-REQ-001,HUFF-REQ-003");

  // Incomplete encoding: {'a': 1, 'b': 3} (Kraft sum = 1/2 + 1/8 = 5/8 < 1)
  // Left-aligned placement:
  //   'a' (len 1) -> 0
  //   'b' (len 3) -> 100 (since 0 is full, go right to 1, then left to 10,
  //                       then left to 100)
  const std::array<struct huffman_symbol_length, 2> entries = {{
      {.symbol = static_cast<uint8_t>('b'), .length = 3U},
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
  }};

  struct huffman_tree tree{};
  const enum huffman_error err =
      huffman_tree_build(&tree, entries.data(), entries.size());
  ASSERT_EQ(err, HUFFMAN_OK) << huffman_error_to_string(err);
  ASSERT_NE(tree.root, nullptr);

  const struct huffman_node* node_a = follow_path(tree.root, 0b0U, 1U);
  ASSERT_NE(node_a, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(node_a));
  EXPECT_EQ(node_a->symbol, static_cast<uint8_t>('a'));

  const struct huffman_node* node_b = follow_path(tree.root, 0b100U, 3U);
  ASSERT_NE(node_b, nullptr);
  EXPECT_TRUE(huffman_node_is_leaf(node_b));
  EXPECT_EQ(node_b->symbol, static_cast<uint8_t>('b'));

  // Unused right branches along the rightmost spine must be nullptr
  ASSERT_NE(tree.root->right, nullptr);
  EXPECT_EQ(tree.root->right->right, nullptr);
  ASSERT_NE(tree.root->right->left, nullptr);
  EXPECT_EQ(tree.root->right->left->right, nullptr);
}

TEST(HuffmanTreeCTest, NodeIsFullFlagAndFunctionVerifySubtrees) {
  RecordProperty("requirements", "HUFF-REQ-004,HUFF-REQ-007");

  // 1. Full tree: {'b': 2, 'e': 1, 'z': 2}
  const std::array<struct huffman_symbol_length, 3> full_entries = {{
      {.symbol = static_cast<uint8_t>('e'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 2U},
      {.symbol = static_cast<uint8_t>('z'), .length = 2U},
  }};
  struct huffman_tree full_tree{};
  ASSERT_EQ(
      huffman_tree_build(&full_tree, full_entries.data(), full_entries.size()),
      HUFFMAN_OK);
  ASSERT_NE(full_tree.root, nullptr);
  EXPECT_TRUE(full_tree.root->is_full);
  ASSERT_NE(full_tree.root->left, nullptr);
  EXPECT_TRUE(full_tree.root->left->is_full);
  ASSERT_NE(full_tree.root->right, nullptr);
  EXPECT_TRUE(full_tree.root->right->is_full);

  bool is_full = false;
  ASSERT_EQ(huffman_node_is_full(full_tree.root, &is_full), HUFFMAN_OK);
  EXPECT_TRUE(is_full);
  ASSERT_EQ(huffman_node_is_full(full_tree.root->right, &is_full), HUFFMAN_OK);
  EXPECT_TRUE(is_full);

  // 2. Non-full tree: {'e': 1, 'b': 2}
  //    root->left ('e') is full, root->right->left ('b') is full,
  //    but root->right and root are NOT full.
  const std::array<struct huffman_symbol_length, 2> partial_entries = {{
      {.symbol = static_cast<uint8_t>('e'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 2U},
  }};
  struct huffman_tree partial_tree{};
  ASSERT_EQ(huffman_tree_build(&partial_tree, partial_entries.data(),
                               partial_entries.size()),
            HUFFMAN_OK);
  ASSERT_NE(partial_tree.root, nullptr);
  EXPECT_FALSE(partial_tree.root->is_full);
  ASSERT_NE(partial_tree.root->left, nullptr);
  EXPECT_TRUE(partial_tree.root->left->is_full);
  ASSERT_NE(partial_tree.root->right, nullptr);
  EXPECT_FALSE(partial_tree.root->right->is_full);
  ASSERT_NE(partial_tree.root->right->left, nullptr);
  EXPECT_TRUE(partial_tree.root->right->left->is_full);

  ASSERT_EQ(huffman_node_is_full(partial_tree.root, &is_full), HUFFMAN_OK);
  EXPECT_FALSE(is_full);
  ASSERT_EQ(huffman_node_is_full(partial_tree.root->left, &is_full),
            HUFFMAN_ERR_INVALID_NODE);
  EXPECT_FALSE(is_full);
  ASSERT_EQ(huffman_node_is_full(partial_tree.root->right, &is_full),
            HUFFMAN_OK);
  EXPECT_FALSE(is_full);
}

// ============================================================================
// Tier 2: Task 2 Optimized Build, Legality, Fullness & Decoding
//         (HUFF-REQ-005, 006, 007, 008)
// ============================================================================

TEST(HuffmanTreeCTest, OptimizedBuildMatchesCanonicalBuildOnLargeAndDeepTrees) {
  RecordProperty("requirements", "HUFF-REQ-002,HUFF-REQ-003,HUFF-REQ-005");

  // 1. Mixed depth non-full encoding in unsorted order
  const std::array<struct huffman_symbol_length, 5> entries = {{
      {.symbol = static_cast<uint8_t>('z'), .length = 4U},
      {.symbol = static_cast<uint8_t>('c'), .length = 3U},
      {.symbol = static_cast<uint8_t>('e'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 3U},
      {.symbol = static_cast<uint8_t>('a'), .length = 3U},
  }};

  struct huffman_tree tree_ref{};
  ASSERT_EQ(huffman_tree_build(&tree_ref, entries.data(), entries.size()),
            HUFFMAN_OK);

  struct huffman_tree tree_opt{};
  const enum huffman_error err =
      huffman_tree_build_optimized(&tree_opt, entries.data(), entries.size());
  ASSERT_EQ(err, HUFFMAN_OK) << huffman_error_to_string(err);
  ASSERT_NE(tree_opt.root, nullptr);
  ASSERT_EQ(tree_opt.node_count, tree_ref.node_count);
  ASSERT_EQ(tree_opt.leaf_count, tree_ref.leaf_count);
  for (size_t i = 0U; i < tree_opt.node_count; ++i) {
    EXPECT_EQ(tree_opt.nodes[i].symbol, tree_ref.nodes[i].symbol);
    EXPECT_EQ(tree_opt.nodes[i].has_symbol, tree_ref.nodes[i].has_symbol);
    EXPECT_EQ(tree_opt.nodes[i].is_full, tree_ref.nodes[i].is_full);
  }

  bool is_full = true;
  ASSERT_EQ(huffman_node_is_full(tree_opt.root, &is_full), HUFFMAN_OK);
  EXPECT_FALSE(is_full);

  // Verify expected canonical paths:
  // 'e' -> 0 (len 1)
  // 'a' -> 100 (len 3)
  // 'b' -> 101 (len 3)
  // 'c' -> 110 (len 3)
  // 'z' -> 1110 (len 4)
  const struct huffman_node* n_e = follow_path(tree_opt.root, 0b0U, 1U);
  const struct huffman_node* n_a = follow_path(tree_opt.root, 0b100U, 3U);
  const struct huffman_node* n_b = follow_path(tree_opt.root, 0b101U, 3U);
  const struct huffman_node* n_c = follow_path(tree_opt.root, 0b110U, 3U);
  const struct huffman_node* n_z = follow_path(tree_opt.root, 0b1110U, 4U);
  ASSERT_NE(n_e, nullptr);
  ASSERT_NE(n_a, nullptr);
  ASSERT_NE(n_b, nullptr);
  ASSERT_NE(n_c, nullptr);
  ASSERT_NE(n_z, nullptr);
  EXPECT_EQ(n_e->symbol, static_cast<uint8_t>('e'));
  EXPECT_EQ(n_a->symbol, static_cast<uint8_t>('a'));
  EXPECT_EQ(n_b->symbol, static_cast<uint8_t>('b'));
  EXPECT_EQ(n_c->symbol, static_cast<uint8_t>('c'));
  EXPECT_EQ(n_z->symbol, static_cast<uint8_t>('z'));

  // 2. Full 256-symbol alphabet (reverse-sorted input to exercise sorting +
  //    linear spine build)
  std::array<struct huffman_symbol_length, 256> all_bytes{};
  for (size_t i = 0U; i < 256U; ++i) {
    all_bytes[i] = {.symbol = static_cast<uint8_t>(255U - i), .length = 8U};
  }
  struct huffman_tree full_256_tree{};
  ASSERT_EQ(huffman_tree_build_optimized(&full_256_tree, all_bytes.data(),
                                         all_bytes.size()),
            HUFFMAN_OK);
  EXPECT_EQ(full_256_tree.leaf_count, 256U);
  EXPECT_EQ(full_256_tree.node_count, 511U);
  ASSERT_EQ(huffman_node_is_full(full_256_tree.root, &is_full), HUFFMAN_OK);
  EXPECT_TRUE(is_full);

  for (uint32_t sym = 0U; sym < 256U; ++sym) {
    const struct huffman_node* leaf = follow_path(full_256_tree.root, sym, 8U);
    ASSERT_NE(leaf, nullptr);
    EXPECT_TRUE(huffman_node_is_leaf(leaf));
    EXPECT_EQ(leaf->symbol, static_cast<uint8_t>(sym));
  }
}

TEST(HuffmanTreeCTest, InfeasibleEncodingsRejectedWithIllegalEncodingError) {
  RecordProperty("requirements", "HUFF-REQ-006");

  struct huffman_tree tree{};

  // 1. User's example: {'a': 1, 'b': 1, 'c': 2}
  //    Infeasible since the tree is already full after adding two length-1
  //    paths (Kraft sum = 1/2 + 1/2 + 1/4 = 1.25 > 1).
  const std::array<struct huffman_symbol_length, 3> infeasible = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 1U},
      {.symbol = static_cast<uint8_t>('c'), .length = 2U},
  }};
  EXPECT_EQ(huffman_tree_build(&tree, infeasible.data(), infeasible.size()),
            HUFFMAN_ERR_ILLEGAL_ENCODING);
  EXPECT_EQ(
      huffman_tree_build_optimized(&tree, infeasible.data(), infeasible.size()),
      HUFFMAN_ERR_ILLEGAL_ENCODING);

  // 2. Over-subscribed by a single 2^-32 unit at max depth 32:
  //    {'a': 1, 'b': 1, 'c': 32}
  const std::array<struct huffman_symbol_length, 3> infeasible_deep = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 1U},
      {.symbol = static_cast<uint8_t>('c'), .length = 32U},
  }};
  EXPECT_EQ(
      huffman_tree_build(&tree, infeasible_deep.data(), infeasible_deep.size()),
      HUFFMAN_ERR_ILLEGAL_ENCODING);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, infeasible_deep.data(),
                                         infeasible_deep.size()),
            HUFFMAN_ERR_ILLEGAL_ENCODING);

  // 3. Legal deep tree at max depth 32: {'a': 1, 'b': 32, 'c': 32}
  const std::array<struct huffman_symbol_length, 3> legal_deep = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 32U},
      {.symbol = static_cast<uint8_t>('c'), .length = 32U},
  }};
  EXPECT_EQ(huffman_tree_build(&tree, legal_deep.data(), legal_deep.size()),
            HUFFMAN_OK);
  EXPECT_EQ(
      huffman_tree_build_optimized(&tree, legal_deep.data(), legal_deep.size()),
      HUFFMAN_OK);
}

TEST(HuffmanTreeCTest, BitstreamDecodeEmitsSymbolsAndRejectsInvalidStreams) {
  RecordProperty("requirements", "HUFF-REQ-008");

  // {'b': 2, 'e': 1, 'z': 2} -> 'e' = 0, 'b' = 10, 'z' = 11
  const std::array<struct huffman_symbol_length, 3> entries = {{
      {.symbol = static_cast<uint8_t>('b'), .length = 2U},
      {.symbol = static_cast<uint8_t>('e'), .length = 1U},
      {.symbol = static_cast<uint8_t>('z'), .length = 2U},
  }};
  struct huffman_tree tree{};
  ASSERT_EQ(huffman_tree_build(&tree, entries.data(), entries.size()),
            HUFFMAN_OK);

  // Encode "ebze" -> bits: 0, 10, 11, 0 -> 0b0101'1000 = 0x58 (6 bits)
  const std::array<uint8_t, 1> bitstream = {0x58U};
  std::array<uint8_t, 8> out_bytes{};
  size_t written = 0U;
  ASSERT_EQ(huffman_tree_decode(&tree, bitstream.data(), 6U, out_bytes.data(),
                                out_bytes.size(), &written),
            HUFFMAN_OK);
  ASSERT_EQ(written, 4U);
  EXPECT_EQ(out_bytes[0], static_cast<uint8_t>('e'));
  EXPECT_EQ(out_bytes[1], static_cast<uint8_t>('b'));
  EXPECT_EQ(out_bytes[2], static_cast<uint8_t>('z'));
  EXPECT_EQ(out_bytes[3], static_cast<uint8_t>('e'));

  // Zero-bit decode succeeds and writes 0 bytes
  written = 99U;
  EXPECT_EQ(
      huffman_tree_decode(&tree, nullptr, 0U, out_bytes.data(), 8U, &written),
      HUFFMAN_OK);
  EXPECT_EQ(written, 0U);

  // Buffer too small (out_capacity = 3 for 4 decoded symbols)
  EXPECT_EQ(huffman_tree_decode(&tree, bitstream.data(), 6U, out_bytes.data(),
                                3U, &written),
            HUFFMAN_ERR_BUFFER_TOO_SMALL);

  // Truncated bitstream ending at internal node '1' (bit_count = 1 on 0x80)
  const std::array<uint8_t, 1> truncated = {0x80U};
  EXPECT_EQ(huffman_tree_decode(&tree, truncated.data(), 1U, out_bytes.data(),
                                out_bytes.size(), &written),
            HUFFMAN_ERR_INVALID_BITSTREAM);

  // Non-full tree {'a': 1}: bit '1' walks into missing right branch
  const std::array<struct huffman_symbol_length, 1> single_entry = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
  }};
  struct huffman_tree single_tree{};
  ASSERT_EQ(huffman_tree_build(&single_tree, single_entry.data(),
                               single_entry.size()),
            HUFFMAN_OK);
  EXPECT_EQ(huffman_tree_decode(&single_tree, truncated.data(), 1U,
                                out_bytes.data(), out_bytes.size(), &written),
            HUFFMAN_ERR_INVALID_BITSTREAM);
}

// ============================================================================
// Tier 3: Boundary & Error Precedence (HUFF-REQ-007, 013, 014, 015, 016)
// ============================================================================

TEST(HuffmanTreeCTest, ValidationPrecedenceAndCleanErrorState) {
  RecordProperty("requirements", "HUFF-REQ-007,HUFF-REQ-013,HUFF-REQ-014");
  RecordProperty("requirements", "HUFF-REQ-015,HUFF-REQ-016");

  struct huffman_tree tree{};
  const std::array<struct huffman_symbol_length, 2> valid_entries = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 1U},
  }};
  bool is_full = false;
  size_t written = 0U;
  std::array<uint8_t, 4> buf{};

  // 1. Null pointers
  EXPECT_EQ(huffman_tree_build(nullptr, valid_entries.data(), 2U),
            HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_tree_build(&tree, nullptr, 2U), HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_tree_build_optimized(nullptr, valid_entries.data(), 2U),
            HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, nullptr, 2U),
            HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_node_is_full(nullptr, &is_full), HUFFMAN_ERR_NULL_PTR);
  struct huffman_node dummy_leaf = {
      .parent = nullptr,
      .left = nullptr,
      .right = nullptr,
      .symbol = static_cast<uint8_t>('a'),
      .has_symbol = true,
      .is_full = true,
  };
  EXPECT_EQ(huffman_node_is_full(&dummy_leaf, nullptr), HUFFMAN_ERR_NULL_PTR);

  // Degenerate internal node (`!huffman_node_is_leaf(node) && left == nullptr`)
  // rejected
  struct huffman_node invalid_internal = {
      .parent = nullptr,
      .left = nullptr,
      .right = nullptr,
      .symbol = 0U,
      .has_symbol = false,
      .is_full = false,
  };
  EXPECT_EQ(huffman_node_is_full(&invalid_internal, &is_full),
            HUFFMAN_ERR_INVALID_NODE);

  EXPECT_EQ(
      huffman_tree_decode(nullptr, buf.data(), 1U, buf.data(), 4U, &written),
      HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_tree_decode(&tree, nullptr, 1U, buf.data(), 4U, &written),
            HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_tree_decode(&tree, buf.data(), 1U, nullptr, 4U, &written),
            HUFFMAN_ERR_NULL_PTR);
  EXPECT_EQ(huffman_tree_decode(&tree, buf.data(), 1U, buf.data(), 4U, nullptr),
            HUFFMAN_ERR_NULL_PTR);

  // 2. Empty input (count == 0)
  EXPECT_EQ(huffman_tree_build(&tree, valid_entries.data(), 0U),
            HUFFMAN_ERR_EMPTY_INPUT);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, valid_entries.data(), 0U),
            HUFFMAN_ERR_EMPTY_INPUT);

  // 3. Too many symbols (count > 256)
  EXPECT_EQ(huffman_tree_build(&tree, valid_entries.data(), 257U),
            HUFFMAN_ERR_TOO_MANY_SYMBOLS);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, valid_entries.data(), 257U),
            HUFFMAN_ERR_TOO_MANY_SYMBOLS);

  // 4. Invalid length (0 or > 32) takes precedence over duplicate / illegal
  const std::array<struct huffman_symbol_length, 2> zero_len_dup = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 0U},
      {.symbol = static_cast<uint8_t>('a'), .length = 0U},
  }};
  EXPECT_EQ(huffman_tree_build(&tree, zero_len_dup.data(), zero_len_dup.size()),
            HUFFMAN_ERR_INVALID_LENGTH);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, zero_len_dup.data(),
                                         zero_len_dup.size()),
            HUFFMAN_ERR_INVALID_LENGTH);

  const std::array<struct huffman_symbol_length, 1> excessive_len = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 33U},
  }};
  EXPECT_EQ(
      huffman_tree_build(&tree, excessive_len.data(), excessive_len.size()),
      HUFFMAN_ERR_INVALID_LENGTH);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, excessive_len.data(),
                                         excessive_len.size()),
            HUFFMAN_ERR_INVALID_LENGTH);

  // 5. Duplicate symbol takes precedence over illegal encoding
  const std::array<struct huffman_symbol_length, 3> dup_infeasible = {{
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
      {.symbol = static_cast<uint8_t>('a'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 1U},
  }};
  EXPECT_EQ(
      huffman_tree_build(&tree, dup_infeasible.data(), dup_infeasible.size()),
      HUFFMAN_ERR_DUPLICATE_SYMBOL);
  EXPECT_EQ(huffman_tree_build_optimized(&tree, dup_infeasible.data(),
                                         dup_infeasible.size()),
            HUFFMAN_ERR_DUPLICATE_SYMBOL);
  EXPECT_EQ(tree.root, nullptr);
  EXPECT_EQ(tree.node_count, 0U);
  EXPECT_EQ(tree.leaf_count, 0U);

  // Verify error strings are non-empty
  EXPECT_STREQ(huffman_error_to_string(HUFFMAN_OK), "ok");
  EXPECT_NE(huffman_error_to_string(HUFFMAN_ERR_ILLEGAL_ENCODING), nullptr);
}

// ============================================================================
// Tier 4: Resource / Zero Heap Allocation (HUFF-REQ-011)
// ============================================================================

TEST(HuffmanTreeCTest, ZeroHeapAllocationInBuildAndDecode) {
  RecordProperty("requirements", "HUFF-REQ-011");

  const std::array<struct huffman_symbol_length, 3> entries = {{
      {.symbol = static_cast<uint8_t>('e'), .length = 1U},
      {.symbol = static_cast<uint8_t>('b'), .length = 2U},
      {.symbol = static_cast<uint8_t>('z'), .length = 2U},
  }};
  const std::array<uint8_t, 1> bitstream = {0x58U};
  std::array<uint8_t, 4> decoded{};
  size_t written = 0U;
  bool is_full = false;
  struct huffman_tree tree{};

  enum huffman_error build_err = HUFFMAN_ERR_NOT_IMPLEMENTED;
  enum huffman_error opt_err = HUFFMAN_ERR_NOT_IMPLEMENTED;
  enum huffman_error full_err = HUFFMAN_ERR_NOT_IMPLEMENTED;
  enum huffman_error decode_err = HUFFMAN_ERR_NOT_IMPLEMENTED;
  std::size_t allocations = 0U;

  {
    const systems_test::AllocationScope scope;
    build_err = huffman_tree_build(&tree, entries.data(), entries.size());
    opt_err =
        huffman_tree_build_optimized(&tree, entries.data(), entries.size());
    full_err = huffman_node_is_full(tree.root, &is_full);
    decode_err = huffman_tree_decode(&tree, bitstream.data(), 6U,
                                     decoded.data(), decoded.size(), &written);
    allocations = scope.allocations();
  }

  EXPECT_EQ(allocations, 0U);
  EXPECT_EQ(build_err, HUFFMAN_OK);
  EXPECT_EQ(opt_err, HUFFMAN_OK);
  EXPECT_EQ(full_err, HUFFMAN_OK);
  EXPECT_TRUE(is_full);
  EXPECT_EQ(decode_err, HUFFMAN_OK);
  EXPECT_EQ(written, 4U);
}

// ============================================================================
// Tier 5: Multi-Thread Reentrancy Stress (HUFF-REQ-010)
// ============================================================================

TEST(HuffmanTreeConcurrencyCTest, ReentrancyConcurrentBuildAndDecode) {
  RecordProperty("requirements", "HUFF-REQ-010");

  constexpr size_t kThreads = 4U;
  constexpr size_t kIterations = 100U;
  std::atomic<size_t> successes{0U};

  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (size_t t = 0U; t < kThreads; ++t) {
    workers.emplace_back([&successes] {
      const std::array<struct huffman_symbol_length, 3> entries = {{
          {.symbol = static_cast<uint8_t>('b'), .length = 2U},
          {.symbol = static_cast<uint8_t>('e'), .length = 1U},
          {.symbol = static_cast<uint8_t>('z'), .length = 2U},
      }};
      const std::array<uint8_t, 1> bitstream = {0x58U};
      std::array<uint8_t, 4> out{};
      struct huffman_tree local_tree{};

      for (size_t i = 0U; i < kIterations; ++i) {
        size_t written = 0U;
        bool is_full = false;
        if (huffman_tree_build(&local_tree, entries.data(), entries.size()) ==
                HUFFMAN_OK &&
            huffman_tree_build_optimized(&local_tree, entries.data(),
                                         entries.size()) == HUFFMAN_OK &&
            huffman_node_is_full(local_tree.root, &is_full) == HUFFMAN_OK &&
            is_full &&
            huffman_tree_decode(&local_tree, bitstream.data(), 6U, out.data(),
                                out.size(), &written) == HUFFMAN_OK &&
            written == 4U && out[0] == static_cast<uint8_t>('e') &&
            out[1] == static_cast<uint8_t>('b') &&
            out[2] == static_cast<uint8_t>('z') &&
            out[3] == static_cast<uint8_t>('e')) {
          successes.fetch_add(1U, std::memory_order_relaxed);
        }
      }
    });
  }

  for (auto& w : workers) {
    w.join();
  }

  EXPECT_EQ(successes.load(), kThreads * kIterations);
}

}  // namespace
