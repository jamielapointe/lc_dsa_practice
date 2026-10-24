/// @file huffman_tree.c
/// @brief Canonical Huffman tree reconstruction, fullness verification, and
/// bitstream decoding in ISO C23.
///
/// Specification: docs/systems/huffman_tree.md (Prefix HUFF).
///
/// Complexity analysis (HUFF-REQ-009):
///   Let N = `count` (number of symbols, N <= 256) and L = max codeword length
///   (L <= 32). The constructed left-aligned tree has V <= 2*N + L - 2 <= 542
///   total nodes.
///   1. Task 1 (`huffman_tree_build`, `huffman_node_is_full`,
///      `huffman_tree_decode`):
///      - Input validation & Kraft check (`validate_entries`): O(N) time and
///        O(1) auxiliary space (256-byte boolean seen table + 64-bit
///        fixed-point accumulator).
///      - Sorting phase (`sort_entries`): O(N^2) worst-case / O(N) best-case
///        (pre-sorted) time via in-place insertion sort over at most 256
///        2-byte entries; O(N) stack copy (`sorted_entries[256]`).
///      - Tree construction phase (`huffman_tree_build`):
///        * Root-down descent for symbol i takes O(length_i) steps, where each
///          step checks `current_node->left->is_full` in O(1) time. Across all
///          N symbols this is O(sum(length_i)) = O(N * L) time.
///        * `refresh_full`: Only climbs `parent` pointers when a node's
///          `is_full` transitions from `false` to `true`. Since each of the
///          < N two-child internal nodes transitions to `true` at most once
///          across the entire build, `refresh_full` runs in O(1) amortized time
///          per symbol (O(N) total across the build).
///        * Overall Task 1 build time: O(N^2 + N * L) = O(1) bounded by
///          N <= 256, L <= 32.
///      - `huffman_node_is_full` (without using `node->is_full`): Because the
///        input tree is left-aligned, every left subtree of an internal node
///        with a right child is already full. Walking only the rightmost spine
///        (`curr = curr->right`) from `root` verifies structural validity and
///        fullness in O(L) time (at most 32 steps) and O(1) auxiliary space.
///      - `huffman_tree_decode`: O(bit_count) time and O(1) auxiliary space.
///      - Auxiliary space complexity: O(1) bounded stack (no recursion, no heap
///        allocation, fixed 256-entry sort buffer and 256-byte seen table).
///   2. Task 2 (`huffman_tree_build_optimized`):
///      - Validation & counting/bucket sort
///      (`validate_and_bucket_sort_entries`):
///        * Single O(N) pass over `entries[0..count-1]` validates code lengths,
///          detects duplicate symbols via a 256-byte `symbol_lengths` table,
///          accumulates the exact 64-bit Kraft sum, and histograms code
///          lengths in `length_offsets[1..32]`.
///        * Prefix-sums the 32 length buckets in O(L) steps and scatters
///          present symbols in ascending byte order into `sorted_entries`,
///          eliminating the O(N^2) insertion sort in O(N + Sigma + L) = O(1)
///          bounded time and O(1) stack space.
///      - Linear-time tree construction after sorting:
///        * Because `sorted_entries` is ordered ascending by `(length,
///        symbol)`,
///          leaves are added in strict left-to-right pre-order.
///        * Instead of restarting from `tree->root` on every symbol, the
///          builder maintains the deepest open internal node `curr` (and its
///          depth `curr_depth`) along the active rightmost spine.
///        * For each leaf of depth `length`, it descends from `curr_depth` to
///          `length - 1` by allocating new internal nodes, attaches the leaf at
///          depth `length`, and then pops upward (`curr = curr->parent`,
///          `--curr_depth`) while `curr->right != nullptr`, marking each popped
///          ancestor `curr->is_full = true`.
///        * Every node in the constructed tree (V <= 2*N + L - 2, or V = 2*N -
///        1
///          for a full tree) is allocated/pushed once and popped at most once,
///          giving strictly O(V) = O(N) post-sorting time and O(1) auxiliary
///          space with zero bit manipulation.
///      - Pipeline & Load-Store Queue (LSQ) micro-optimizations:
///        * Dual even/odd length histograms (`length_offsets` + `odd_counts`)
///          in the 2x-unrolled validation pass eliminate 4-cycle LSQ
///          store-to-load forwarding stalls when consecutive symbols share the
///          same codeword length.
///        * The 64-bit Kraft shift-add is hoisted out of the N-symbol pass into
///          the 32-iteration prefix-sum loop (`cnt << (32 - len)`).
///        * For full 256-symbol alphabets, a 2x-unrolled branchless scatter
///          loop pre-loads both bucket offsets prior to storing and resolves
///          same-length collisions with a 1-cycle conditional select (`csel`).
///        * Arena allocation uses a register-resident `node_cursor` pointer and
///          writes `tree->node_count` / `tree->leaf_count` once upon
///          completion; after the first pure-left leaf insertion, the spine
///          loop branches only on `curr_depth == target_depth` vs
///          `curr_depth < target_depth`.

#include "systems/huffman_tree.h"

#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

[[nodiscard]] const char* huffman_error_to_string(
    const enum huffman_error error) {
  switch (error) {
    case HUFFMAN_OK:
      return "ok";
    case HUFFMAN_ERR_NOT_IMPLEMENTED:
      return "not implemented";
    case HUFFMAN_ERR_NULL_PTR:
      return "null pointer argument";
    case HUFFMAN_ERR_EMPTY_INPUT:
      return "empty symbol-length table (count == 0)";
    case HUFFMAN_ERR_TOO_MANY_SYMBOLS:
      return "symbol count exceeds HUFFMAN_MAX_SYMBOLS (256)";
    case HUFFMAN_ERR_INVALID_LENGTH:
      return "code length outside valid range [1, HUFFMAN_MAX_CODE_LENGTH]";
    case HUFFMAN_ERR_DUPLICATE_SYMBOL:
      return "duplicate symbol in encoding table";
    case HUFFMAN_ERR_ILLEGAL_ENCODING:
      return "illegal encoding: violates Kraft's inequality (tree overflow)";
    case HUFFMAN_ERR_INVALID_NODE:
      return "invalid internal node (left child is null)";
    case HUFFMAN_ERR_INVALID_BITSTREAM:
      return "invalid or truncated bitstream during decode";
    case HUFFMAN_ERR_BUFFER_TOO_SMALL:
      return "output buffer too small for decoded symbols";
  }
  return "unknown huffman_error";
}

[[nodiscard]] static enum huffman_error validate_entries(
    const struct huffman_symbol_length* const entries, const size_t count) {
  bool seen[HUFFMAN_MAX_SYMBOLS] = {};
  uint64_t used_capacity = 0ULL;
  for (size_t i = 0U; i < count; ++i) {
    const uint8_t length = entries[i].length;
    const uint8_t symbol = entries[i].symbol;
    if (length < 1U || length > HUFFMAN_MAX_CODE_LENGTH) {
      return HUFFMAN_ERR_INVALID_LENGTH;
    }
    if (seen[symbol]) {
      return HUFFMAN_ERR_DUPLICATE_SYMBOL;
    }
    seen[symbol] = true;
    const uint64_t weight = 1ULL << (HUFFMAN_MAX_CODE_LENGTH - length);
    if (ckd_add(&used_capacity, used_capacity, weight) ||
        used_capacity > HUFFMAN_MAX_TREE_CAPACITY) {
      return HUFFMAN_ERR_ILLEGAL_ENCODING;
    }
  }
  return HUFFMAN_OK;
}

// Sort by length and then by symbol in ascending order.
[[nodiscard]] static inline bool entry_less_than(
    const struct huffman_symbol_length a,
    const struct huffman_symbol_length b) {
  return (bool)((a.length < b.length) ||
                (a.length == b.length && a.symbol < b.symbol));
}

// In-place sort of entries. Since max count is 256, insertion sort runs in
// bounded time with zero heap or VLA allocation.
static void sort_entries(struct huffman_symbol_length* const entries,
                         const size_t count) {
  for (size_t i = 1U; i < count; ++i) {
    const struct huffman_symbol_length key = entries[i];
    size_t j = i;
    while (j > 0U && entry_less_than(key, entries[j - 1U])) {
      entries[j] = entries[j - 1U];
      --j;
    }
    entries[j] = key;
  }
}

// Refreshes the is_full flag for the node and all of its ancestors.
static inline void refresh_full(struct huffman_node* const node) {
  struct huffman_node* current_node = node;
  while (current_node != nullptr) {
    current_node->is_full =
        (bool)(huffman_node_is_leaf(current_node) ||
               (current_node->left != nullptr && current_node->left->is_full &&
                current_node->right != nullptr &&
                current_node->right->is_full));
    if (!current_node->is_full) {
      break;
    }
    current_node = current_node->parent;
  }
}

static inline void reset_tree(struct huffman_tree* const tree) {
  tree->root = nullptr;
  tree->node_count = 0U;
  tree->leaf_count = 0U;
}

[[nodiscard]] static inline enum huffman_error reset_tree_error(
    struct huffman_tree* const tree, const enum huffman_error error) {
  reset_tree(tree);
  return error;
}

[[nodiscard]] static inline bool huffman_node_is_valid(
    const struct huffman_node* const node) {
  if (node == nullptr) {
    return false;
  }
  if (huffman_node_is_leaf(node)) {
    // A leaf node may not have children.
    return (bool)((node->left == nullptr) && (node->right == nullptr));
  }
  // An internal node of a built tree must have at least a left child.
  return (bool)(node->left != nullptr);
}

// Internal allocation from the embedded huffman_tree->nodes arena; avoids heap
// allocation and VLAs.
[[nodiscard]] static struct huffman_node* allocate_node(
    struct huffman_tree* const tree, struct huffman_node* const parent,
    const bool is_valid_symbol, const uint8_t symbol) {
  if (tree->node_count >= HUFFMAN_MAX_NODES) {
    return nullptr;
  }
  struct huffman_node* const node = &tree->nodes[tree->node_count];
  node->parent = parent;
  node->left = nullptr;
  node->right = nullptr;
  node->symbol = symbol;
  node->has_symbol = is_valid_symbol;
  node->is_full = false;
  tree->node_count++;
  if (huffman_node_is_leaf(node)) {
    tree->leaf_count++;
    node->is_full = true;
  }
  return node;
}

[[nodiscard]] static inline bool huffman_read_bit_msb(
    const uint8_t* const bitstream, const size_t bit_idx) {
  const size_t byte_idx = bit_idx >> 3U;
  const uint8_t bit_pos = 7U - (uint8_t)(bit_idx & 7U);
  return ((bitstream[byte_idx] >> bit_pos) & 1U) != 0U;
}

[[nodiscard]] enum huffman_error huffman_tree_build(
    struct huffman_tree* const tree,
    const struct huffman_symbol_length* const entries, const size_t count) {
  if (tree == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }

  // Ensure tree is cleared before checking any other errors so it is guaranteed
  // to be in a valid empty state on failure (HUFF-REQ-016).
  reset_tree(tree);

  if (entries == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  if (count == 0U) {
    return HUFFMAN_ERR_EMPTY_INPUT;
  }
  if (count > HUFFMAN_MAX_SYMBOLS) {
    return HUFFMAN_ERR_TOO_MANY_SYMBOLS;
  }

  const enum huffman_error err = validate_entries(entries, count);
  if (err != HUFFMAN_OK) {
    return err;
  }

  // Fixed-capacity stack buffer avoids VLAs and heap allocation (HUFF-REQ-011,
  // HUFF-REQ-012).
  struct huffman_symbol_length sorted_entries[HUFFMAN_MAX_SYMBOLS] = {};
  memcpy(sorted_entries, entries, count * sizeof(struct huffman_symbol_length));
  sort_entries(sorted_entries, count);

  tree->root = allocate_node(tree, nullptr, false, 0U);
  if (tree->root == nullptr) {
    return reset_tree_error(tree, HUFFMAN_ERR_INVALID_NODE);
  }

  for (size_t i = 0U; i < count; ++i) {
    const uint8_t length = sorted_entries[i].length;
    const uint8_t symbol = sorted_entries[i].symbol;
    struct huffman_node* current_node = tree->root;
    for (uint8_t depth = 1U; depth < length; ++depth) {
      if (current_node->left == nullptr) {
        current_node->left = allocate_node(tree, current_node, false, 0U);
        if (current_node->left == nullptr) {
          return reset_tree_error(tree, HUFFMAN_ERR_INVALID_NODE);
        }
        current_node = current_node->left;
      } else if (!current_node->left->is_full) {
        current_node = current_node->left;
      } else if (current_node->right == nullptr) {
        current_node->right = allocate_node(tree, current_node, false, 0U);
        if (current_node->right == nullptr) {
          return reset_tree_error(tree, HUFFMAN_ERR_INVALID_NODE);
        }
        current_node = current_node->right;
      } else {
        if (current_node->right->is_full) {
          return reset_tree_error(tree, HUFFMAN_ERR_ILLEGAL_ENCODING);
        }
        current_node = current_node->right;
      }
    }
    if (current_node->left == nullptr) {
      current_node->left = allocate_node(tree, current_node, true, symbol);
      if (current_node->left == nullptr) {
        return reset_tree_error(tree, HUFFMAN_ERR_INVALID_NODE);
      }
    } else if (current_node->right == nullptr) {
      current_node->right = allocate_node(tree, current_node, true, symbol);
      if (current_node->right == nullptr) {
        return reset_tree_error(tree, HUFFMAN_ERR_INVALID_NODE);
      }
    } else {
      return reset_tree_error(tree, HUFFMAN_ERR_ILLEGAL_ENCODING);
    }
    refresh_full(current_node);
  }

  return HUFFMAN_OK;
}

[[nodiscard]] static inline struct huffman_node* alloc_internal_node(
    struct huffman_node** const cursor, struct huffman_node* const parent) {
  struct huffman_node* const node = *cursor;
  *cursor = node + 1;
  node->parent = parent;
  node->left = nullptr;
  node->right = nullptr;
  node->symbol = 0U;
  node->has_symbol = false;
  node->is_full = false;
  return node;
}

[[nodiscard]] static inline struct huffman_node* alloc_leaf_node(
    struct huffman_node** const cursor, struct huffman_node* const parent,
    const uint8_t symbol) {
  struct huffman_node* const node = *cursor;
  *cursor = node + 1;
  node->parent = parent;
  node->left = nullptr;
  node->right = nullptr;
  node->symbol = symbol;
  node->has_symbol = true;
  node->is_full = true;
  return node;
}

[[nodiscard]] static enum huffman_error validate_and_bucket_sort_entries(
    const struct huffman_symbol_length* const restrict entries,
    const size_t count,
    struct huffman_symbol_length* const restrict sorted_entries) {
  uint8_t symbol_lengths[HUFFMAN_MAX_SYMBOLS] = {};
  uint16_t length_offsets[HUFFMAN_MAX_CODE_LENGTH + 2U] = {};
  uint16_t odd_counts[HUFFMAN_MAX_CODE_LENGTH + 2U] = {};

  size_t i = 0U;
  for (; i + 1U < count; i += 2U) {
    const uint8_t len0 = entries[i].length;
    const uint8_t sym0 = entries[i].symbol;
    if ((uint8_t)(len0 - 1U) >= HUFFMAN_MAX_CODE_LENGTH) {
      return HUFFMAN_ERR_INVALID_LENGTH;
    }
    if (symbol_lengths[sym0] != 0U) {
      return HUFFMAN_ERR_DUPLICATE_SYMBOL;
    }
    symbol_lengths[sym0] = len0;
    length_offsets[(size_t)len0 + 1U]++;

    const uint8_t len1 = entries[i + 1U].length;
    const uint8_t sym1 = entries[i + 1U].symbol;
    if ((uint8_t)(len1 - 1U) >= HUFFMAN_MAX_CODE_LENGTH) {
      return HUFFMAN_ERR_INVALID_LENGTH;
    }
    if (symbol_lengths[sym1] != 0U) {
      return HUFFMAN_ERR_DUPLICATE_SYMBOL;
    }
    symbol_lengths[sym1] = len1;
    odd_counts[(size_t)len1 + 1U]++;
  }
  for (; i < count; ++i) {
    const uint8_t length = entries[i].length;
    const uint8_t symbol = entries[i].symbol;
    if ((uint8_t)(length - 1U) >= HUFFMAN_MAX_CODE_LENGTH) {
      return HUFFMAN_ERR_INVALID_LENGTH;
    }
    if (symbol_lengths[symbol] != 0U) {
      return HUFFMAN_ERR_DUPLICATE_SYMBOL;
    }
    symbol_lengths[symbol] = length;
    length_offsets[(size_t)length + 1U]++;
  }

  uint64_t used_capacity = 0ULL;
  uint16_t running_offset = 0U;
  for (size_t len = 1U; len <= HUFFMAN_MAX_CODE_LENGTH; ++len) {
    const uint16_t cnt =
        (uint16_t)(length_offsets[len + 1U] + odd_counts[len + 1U]);
    used_capacity += (uint64_t)cnt << (HUFFMAN_MAX_CODE_LENGTH - len);
    length_offsets[len] = running_offset;
    running_offset = (uint16_t)(running_offset + cnt);
  }

  if (used_capacity > HUFFMAN_MAX_TREE_CAPACITY) {
    return HUFFMAN_ERR_ILLEGAL_ENCODING;
  }

  if (count == HUFFMAN_MAX_SYMBOLS) {
    for (size_t sym = 0U; sym < HUFFMAN_MAX_SYMBOLS; sym += 2U) {
      const uint8_t len0 = symbol_lengths[sym];
      const uint8_t len1 = symbol_lengths[sym + 1U];
      const uint16_t idx0 = length_offsets[len0];
      const uint16_t raw_idx1 = length_offsets[len1];
      const uint16_t idx1 = (len0 == len1) ? (uint16_t)(idx0 + 1U) : raw_idx1;
      length_offsets[len0] = (uint16_t)(idx0 + 1U);
      length_offsets[len1] = (uint16_t)(idx1 + 1U);
      sorted_entries[idx0] = (struct huffman_symbol_length){
          .symbol = (uint8_t)sym,
          .length = len0,
      };
      sorted_entries[idx1] = (struct huffman_symbol_length){
          .symbol = (uint8_t)(sym + 1U),
          .length = len1,
      };
    }
  } else {
    for (size_t sym = 0U; sym < HUFFMAN_MAX_SYMBOLS; ++sym) {
      const uint8_t length = symbol_lengths[sym];
      if (length != 0U) {
        const uint16_t dst_idx = length_offsets[length]++;
        sorted_entries[dst_idx] = (struct huffman_symbol_length){
            .symbol = (uint8_t)sym,
            .length = length,
        };
      }
    }
  }

  return HUFFMAN_OK;
}

[[nodiscard]] enum huffman_error huffman_tree_build_optimized(
    struct huffman_tree* const restrict tree,
    const struct huffman_symbol_length* const restrict entries,
    const size_t count) {
  if (tree == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }

  // Ensure tree is cleared before checking any other errors so it is guaranteed
  // to be in a valid empty state on failure (HUFF-REQ-016).
  reset_tree(tree);

  if (entries == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  if (count == 0U) {
    return HUFFMAN_ERR_EMPTY_INPUT;
  }
  if (count > HUFFMAN_MAX_SYMBOLS) {
    return HUFFMAN_ERR_TOO_MANY_SYMBOLS;
  }

  struct huffman_symbol_length sorted_entries[HUFFMAN_MAX_SYMBOLS];
  const enum huffman_error err =
      validate_and_bucket_sort_entries(entries, count, sorted_entries);
  if (err != HUFFMAN_OK) {
    return err;
  }

  struct huffman_node* node_cursor = tree->nodes;
  struct huffman_node* const root = alloc_internal_node(&node_cursor, nullptr);
  tree->root = root;
  struct huffman_node* curr = root;

  // Insert first symbol along the pure-left spine from root.
  const uint8_t first_len = sorted_entries[0].length;
  const uint8_t first_sym = sorted_entries[0].symbol;
  uint8_t curr_depth = (uint8_t)(first_len - 1U);
  for (uint8_t d = 0U; d < curr_depth; ++d) {
    struct huffman_node* const next = alloc_internal_node(&node_cursor, curr);
    curr->left = next;
    curr = next;
  }
  curr->left = alloc_leaf_node(&node_cursor, curr, first_sym);

  // For all remaining symbols (i >= 1), `curr` at `curr_depth` is guaranteed to
  // have `curr->left` full and `curr->right == nullptr`.
  for (size_t i = 1U; i < count; ++i) {
    const uint8_t target_depth = (uint8_t)(sorted_entries[i].length - 1U);
    const uint8_t symbol = sorted_entries[i].symbol;

    if (curr_depth == target_depth) {
      curr->right = alloc_leaf_node(&node_cursor, curr, symbol);
      do {
        curr->is_full = true;
        curr = curr->parent;
        --curr_depth;
      } while (curr != nullptr && curr->right != nullptr);
    } else {
      struct huffman_node* next = alloc_internal_node(&node_cursor, curr);
      curr->right = next;
      curr = next;
      ++curr_depth;
      while (curr_depth < target_depth) {
        next = alloc_internal_node(&node_cursor, curr);
        curr->left = next;
        curr = next;
        ++curr_depth;
      }
      curr->left = alloc_leaf_node(&node_cursor, curr, symbol);
    }
  }

  tree->node_count = (size_t)(node_cursor - tree->nodes);
  tree->leaf_count = count;
  return HUFFMAN_OK;
}

[[nodiscard]] enum huffman_error huffman_node_is_full(
    const struct huffman_node* const root, bool* const out_is_full) {
  // Follow-up 4 (HUFF-REQ-007, HUFF-REQ-013):
  //   Pretend we are given a left-aligned Huffman tree from an external caller
  //   where the `node->is_full` property does NOT exist. We **cannot** read or
  //   rely on `node->is_full`; we must parse the tree structure to check for
  //   fullness.
  if (root == nullptr || out_is_full == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  *out_is_full = false;
  if (huffman_node_is_leaf(root) || !huffman_node_is_valid(root)) {
    return HUFFMAN_ERR_INVALID_NODE;
  }

  // In a properly formed left-aligned Huffman tree, every left subtree of an
  // internal node with a right child is already full, so we only need to walk
  // the rightmost spine.
  const struct huffman_node* curr = root;
  while (curr->right != nullptr) {
    if (!huffman_node_is_valid(curr)) {
      return HUFFMAN_ERR_INVALID_NODE;
    }
    curr = curr->right;
  }
  if (!huffman_node_is_valid(curr)) {
    return HUFFMAN_ERR_INVALID_NODE;
  }
  *out_is_full = huffman_node_is_leaf(curr);
  return HUFFMAN_OK;
}

[[nodiscard]] enum huffman_error huffman_tree_decode(
    const struct huffman_tree* const tree, const uint8_t* const bitstream,
    const size_t bit_count, uint8_t* const out_bytes, const size_t out_capacity,
    size_t* const out_written) {
  if (out_written == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  *out_written = 0U;
  if (tree == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  if (bit_count > 0U && bitstream == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  if (out_capacity > 0U && out_bytes == nullptr) {
    return HUFFMAN_ERR_NULL_PTR;
  }
  if ((tree->root == nullptr) || (tree->node_count == 0U) ||
      (tree->leaf_count == 0U) || huffman_node_is_leaf(tree->root) ||
      !huffman_node_is_valid(tree->root)) {
    return HUFFMAN_ERR_INVALID_NODE;
  }
  if (bit_count == 0U) {
    return HUFFMAN_OK;
  }

  const struct huffman_node* current = tree->root;
  size_t written = 0U;
  for (size_t bit_idx = 0U; bit_idx < bit_count; ++bit_idx) {
    const bool bit = huffman_read_bit_msb(bitstream, bit_idx);
    current = bit ? current->right : current->left;

    // Taking a non-existent branch indicates an unmapped code in the bitstream.
    if (current == nullptr) {
      *out_written = written;
      return HUFFMAN_ERR_INVALID_BITSTREAM;
    }

    // Verify the node itself is not structurally corrupt.
    if (!huffman_node_is_valid(current)) {
      *out_written = written;
      return HUFFMAN_ERR_INVALID_NODE;
    }

    if (huffman_node_is_leaf(current)) {
      if (written >= out_capacity) {
        *out_written = written;
        return HUFFMAN_ERR_BUFFER_TOO_SMALL;
      }
      out_bytes[written] = current->symbol;
      ++written;
      current = tree->root;
    }
  }

  *out_written = written;

  // If traversal did not finish back at the root, the bitstream ended mid-code.
  if (current != tree->root) {
    return HUFFMAN_ERR_INVALID_BITSTREAM;
  }

  return HUFFMAN_OK;
}
