#pragma once

/// @file huffman_tree.h
/// @brief Zero-allocation canonical binary prefix tree (Huffman tree)
/// reconstruction, fullness verification, and bitstream decoding in ISO C23.
///
/// Specification: docs/systems/huffman_tree.md (Prefix HUFF).
///
/// Problem summary:
///   Given a byte-to-bit-length encoding table (`symbol -> length`), construct
///   the unique left-aligned canonical binary prefix tree where:
///   1. Values appear only at leaves (`huffman_node_is_leaf(node) == true`).
///   2. Short paths appear to the left of long paths.
///   3. Within the same path length, smaller byte values appear to the left of
///      larger byte values.
///   4. No new leaf can be added to the left of an existing leaf.
///
/// Two-task structure:
///   - Task 1 (Basic + Follow-ups 1, 3, 4):
///     * `huffman_tree_build`: Validates all inputs and Kraft feasibility
///       (rejecting infeasible inputs such as `{'a': 1, 'b': 1, 'c': 2}` with
///       `HUFFMAN_ERR_ILLEGAL_ENCODING`), sorts entries by `(length, symbol)`,
///       and constructs the left-aligned canonical tree while maintaining
///       `node->is_full` in O(1) amortized time.
///     * `huffman_node_is_full`: Models receiving a Huffman tree from an
///       external caller and parsing it to check for fullness. Validates inputs
///       (`root`, `out_is_full`), checks whether the tree/subtree rooted at
///       `root` is full **without** using the `node->is_full` property
///       (pretending `node->is_full` does not exist), and returns
///       `enum huffman_error`.
///     * `huffman_tree_decode`: Validates inputs and decodes an MSB-first
///       bitstream through the constructed tree.
///   - Task 2 (Follow-up 2 + Micro-Optimizations):
///     * `huffman_tree_build_optimized`: Validates all inputs and constructs
///       the identical canonical left-aligned tree in linear time O(nodes)
///       after sorting (using an explicit rightmost-spine ancestor stack rather
///       than restarting from `root` for every symbol, plus optional O(N)
///       counting/bucket sort and micro-optimizations).
///
/// Thread safety:
///   - All functions are reentrant and use no mutable static state
///     (HUFF-REQ-010).
///   - Separate `struct huffman_tree` instances may be built and used
///     concurrently without synchronization.
///   - A built `const struct huffman_tree*` may be shared concurrently across
///     threads for read-only calls (`huffman_node_is_full`,
///     `huffman_tree_decode`).
///
/// Memory model:
///   - Strictly zero heap allocation (`malloc`/`free` forbidden; HUFF-REQ-011).
///   - All tree nodes are allocated from the embedded fixed-capacity array
///     `tree->nodes[HUFFMAN_MAX_NODES]`.
///   - Iterative execution only; no recursion (`misc-no-recursion`) or VLAs
///     (HUFF-REQ-012).
///
/// Dual-language safe: can be included by both ISO C23 and C++23 translation
/// units.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Maximum number of distinct byte symbols in an alphabet (2^8).
static constexpr size_t HUFFMAN_MAX_SYMBOLS = 256U;

/// @brief Maximum supported bit length for any single codeword.
static constexpr uint8_t HUFFMAN_MAX_CODE_LENGTH = 32U;

/// @brief Capacity of the embedded node arena inside `struct huffman_tree`.
/// @details A left-aligned tree with N <= 256 leaves and max depth L <= 32
/// contains at most 2*N + L - 2 = 542 nodes; 1024 provides a power-of-two
/// ceiling.
static constexpr size_t HUFFMAN_MAX_NODES = 1024U;

/// @brief Total capacity of the Huffman tree in terms of possible leaf nodes.
static constexpr uint64_t HUFFMAN_MAX_TREE_CAPACITY =
    1ULL << HUFFMAN_MAX_CODE_LENGTH;

/// @brief Status and error codes returned by Huffman tree operations.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum huffman_error : int32_t {
  HUFFMAN_OK = 0,
  HUFFMAN_ERR_NOT_IMPLEMENTED = 1,
  HUFFMAN_ERR_NULL_PTR = 2,
  HUFFMAN_ERR_EMPTY_INPUT = 3,
  HUFFMAN_ERR_TOO_MANY_SYMBOLS = 4,
  HUFFMAN_ERR_INVALID_LENGTH = 5,
  HUFFMAN_ERR_DUPLICATE_SYMBOL = 6,
  HUFFMAN_ERR_ILLEGAL_ENCODING = 7,
  HUFFMAN_ERR_INVALID_NODE = 8,
  HUFFMAN_ERR_INVALID_BITSTREAM = 9,
  HUFFMAN_ERR_BUFFER_TOO_SMALL = 10,
} huffman_error;

static_assert(sizeof(enum huffman_error) == 4U,
              "ABI requires 32-bit huffman_error");

/// @brief Returns a human-readable string describing a `huffman_error`.
[[nodiscard]] const char* huffman_error_to_string(enum huffman_error error);

/// @brief One `(symbol, code_length)` entry in a Huffman encoding table.
struct huffman_symbol_length {
  uint8_t symbol;
  uint8_t length;
};

/// @brief A single node in the binary prefix tree, allocated from
/// `huffman_tree::nodes`.
struct huffman_node {
  struct huffman_node* parent;
  struct huffman_node* left;
  struct huffman_node* right;
  uint8_t symbol;
  bool has_symbol;
  bool is_full;
};

/// @brief Returns `true` if `node` is a leaf, i.e. it stores a symbol value.
/// @param node Non-null node to inspect (precondition; not checked).
/// @return `node->has_symbol`.
/// @par Thread safety
/// Pure read-only accessor; safe to call concurrently on shared nodes.
[[nodiscard]] static inline bool huffman_node_is_leaf(
    const struct huffman_node* const node) {
  return node->has_symbol;
}

/// @brief Self-contained, heap-free Huffman decoding tree with embedded node
/// pool.
struct huffman_tree {
  struct huffman_node* root;
  size_t node_count;
  size_t leaf_count;
  struct huffman_node nodes[HUFFMAN_MAX_NODES];
};

/// @brief Constructs a canonical left-aligned Huffman tree from an encoding
/// table, validating feasibility and maintaining O(1) `is_full` node state
/// (Task 1: Basic Question + Follow-ups 1 & 3).
/// @param tree Destination tree structure (caller-owned storage).
/// @param entries Array of `(symbol, length)` mappings.
/// @param count Number of entries in `entries`.
/// @return `HUFFMAN_OK` on success, or the first applicable error in precedence
/// order (`HUFFMAN_ERR_NULL_PTR`, `HUFFMAN_ERR_EMPTY_INPUT`,
/// `HUFFMAN_ERR_TOO_MANY_SYMBOLS`, followed by the first entry-level validation
/// failure: `HUFFMAN_ERR_INVALID_LENGTH`, `HUFFMAN_ERR_DUPLICATE_SYMBOL`, or
/// `HUFFMAN_ERR_ILLEGAL_ENCODING`).
/// @par Requirements
/// HUFF-REQ-001, HUFF-REQ-002, HUFF-REQ-003, HUFF-REQ-004, HUFF-REQ-006,
/// HUFF-REQ-011, HUFF-REQ-012, HUFF-REQ-013, HUFF-REQ-014, HUFF-REQ-015,
/// HUFF-REQ-016
[[nodiscard]] enum huffman_error huffman_tree_build(
    struct huffman_tree* tree, const struct huffman_symbol_length* entries,
    size_t count);

/// @brief Constructs a canonical left-aligned Huffman tree using linear-time
/// O(nodes) spine-stack construction after sorting and micro-optimizations
/// (Task 2: Follow-up 2).
/// @param tree Destination tree structure (caller-owned storage).
/// @param entries Array of `(symbol, length)` mappings.
/// @param count Number of entries in `entries`.
/// @return `HUFFMAN_OK` on success, or the first applicable error in precedence
/// order (`HUFFMAN_ERR_NULL_PTR`, `HUFFMAN_ERR_EMPTY_INPUT`,
/// `HUFFMAN_ERR_TOO_MANY_SYMBOLS`, followed by the first entry-level validation
/// failure: `HUFFMAN_ERR_INVALID_LENGTH`, `HUFFMAN_ERR_DUPLICATE_SYMBOL`, or
/// `HUFFMAN_ERR_ILLEGAL_ENCODING`).
/// @par Requirements
/// HUFF-REQ-001, HUFF-REQ-002, HUFF-REQ-003, HUFF-REQ-004, HUFF-REQ-005,
/// HUFF-REQ-006, HUFF-REQ-011, HUFF-REQ-012, HUFF-REQ-013, HUFF-REQ-014,
/// HUFF-REQ-015, HUFF-REQ-016
[[nodiscard]] enum huffman_error huffman_tree_build_optimized(
    struct huffman_tree* tree, const struct huffman_symbol_length* entries,
    size_t count);

/// @brief Checks whether the left-aligned tree or subtree rooted at `root` is
/// full by parsing the tree structure without using `node->is_full` (Task 1:
/// Follow-up 4).
/// @note This function pretends we are given a Huffman tree from an external
/// source where the `node->is_full` property does not exist; it must not read
/// or rely on `node->is_full`.
/// @param root Root node of a constructed left-aligned tree or subtree.
/// @param out_is_full Output boolean set to `true` if every internal node has
/// two children and all leaves store values, `false` otherwise.
/// @return `HUFFMAN_OK` on success, `HUFFMAN_ERR_NULL_PTR` if `root` or
/// `out_is_full` is `nullptr`, or `HUFFMAN_ERR_INVALID_NODE` if `root` is a
/// leaf node or any visited node along the rightmost spine is structurally
/// invalid.
/// @par Requirements
/// HUFF-REQ-007, HUFF-REQ-013
[[nodiscard]] enum huffman_error huffman_node_is_full(
    const struct huffman_node* root, bool* out_is_full);

/// @brief Decodes an MSB-first packed bitstream using a constructed Huffman
/// tree.
/// @param tree Pointer to a constructed `huffman_tree`.
/// @param bitstream Pointer to packed input bytes (may be `nullptr` only when
/// `bit_count == 0`).
/// @param bit_count Number of valid bits to consume from `bitstream`.
/// @param out_bytes Destination buffer for decoded symbols (may be `nullptr`
/// only when `out_capacity == 0`).
/// @param out_capacity Capacity of `out_bytes` in bytes.
/// @param out_written Output count of decoded bytes written to `out_bytes`.
/// @return `HUFFMAN_OK` on success, `HUFFMAN_ERR_NULL_PTR` on null pointer,
/// `HUFFMAN_ERR_INVALID_NODE` if `tree` is unbuilt (`tree->root == nullptr`) or
/// a visited node is structurally corrupt, `HUFFMAN_ERR_INVALID_BITSTREAM` if a
/// bit branches to a null child or `bit_count` ends at an internal node, or
/// `HUFFMAN_ERR_BUFFER_TOO_SMALL` if `out_capacity` is exceeded.
/// @par Requirements
/// HUFF-REQ-001, HUFF-REQ-008, HUFF-REQ-011, HUFF-REQ-013
[[nodiscard]] enum huffman_error huffman_tree_decode(
    const struct huffman_tree* tree, const uint8_t* bitstream, size_t bit_count,
    uint8_t* out_bytes, size_t out_capacity, size_t* out_written);

#ifdef __cplusplus
}  // extern "C"
#endif
