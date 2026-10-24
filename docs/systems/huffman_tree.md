# HuffmanTree — Software Requirements

| Field              | Value                                                                                                         |
| ------------------ | ------------------------------------------------------------------------------------------------------------- |
| Component          | `huffman_tree` (`src/include/systems/huffman_tree.h`, `src/c/systems/huffman_tree.c`)                         |
| Requirement prefix | `HUFF`                                                                                                        |
| Status             | Implemented (ISO C23)                                                                                         |
| Integrity context  | Practice component written with ISO 26262 / MISRA C:2023 rigor; not a certified work product                  |
| Target platforms   | Linux, embedded Linux, QNX, bare-metal MCUs (pure ISO C23 computation, no OS calls); macOS (development host) |

## 1. Problem statement

> **Decode a Huffman Tree**
>
> ## Basic Question
>
> - many compression algorithms encode data using binary prefix trees (aka Huffman Trees)
> - In a binary prefix tree, values can only appear at the leaves of the tree
> - To decode a file, we read bits from the file, use them to follow a path in the tree to a leaf, which determines a value:
>   - We start at the root of the tree
>   - When encountering a 0-bit, we descend to the left child, and when encountering a 1-bit we descend to the right child
>   - When reaching a leaf, we emit the value stored in the leaft
>
> To make sure the mapping only represents a single tree, the tree must satisfy the following properties:
>
> 1. Short paths are to the left of long paths
> 2. Within the same length, smaller values appear to the left of larger values
>
> **The main question** Given the encoding (byte->length mapping), construct a valid tree. Analyze the runtime complexity.
>
> ### Example
>
> Input: `{'b': 2, 'e': 1, 'z': 2}`
>
> Output tree:
>
> ```text
>   *
>  / \
> e   *           e = 0
>    / \          b = 10
>   b   z         z = 11
> ```
>
> ### Assume
>
> #### Tree Fullness
>
> tl;dr at first, you may assume the tree is "full"
>
> - The criteria above gives a unique defninition of a tree only if it is "full" and no other values can be added
>   - Formally - all nodes either have exactly two children, or are leaves with values
> - However, there is no formal requirement that the tree is full
>   - We only require the tree to be "aligned to the left" as possible
>   - Formally: **We cannot add a new leaf to the left of an existing leaf**
>
> ## Follow-up Questions
>
> ### Follow-up Question 1
>
> **If relevant, optimize "is full" to run in (amortized) constant time**
>
> ### Follow-up Question 2
>
> **Construct the tree (after the sorting) in linear time**
>
> - The input should be sorted by path length and then by byte value
> - Describe how the algorithm can be changed to make the tree construction phase run in linear time (in input size)
>
> ### Follow-up Question 3
>
> **Check if an encoding is legal**
>
> - Some inputs may represent infeasible trees
>
> ### Follow-up Question 4
>
> **Check if a left-aligned tree is full**

## 2. Context and intent

Embedded firmware images, CAN-FD / automotive diagnostic payloads (UDS `RequestDownload`), and sensor calibration blobs are frequently compressed with
canonical Huffman (prefix) codes such as DEFLATE. To decode a stream deterministically on a microcontroller or real-time safety core without dynamic memory
allocation (`malloc`/`free`), the decoder reconstructs the canonical binary prefix tree from the transmitted `(symbol, code_length)` table inside a fixed-size
node arena, verifies that the code-length table is feasible before processing untrusted data, and decodes bitstreams in bounded time and space.

## 3. Assumptions and decisions

| ID  | Assumption / decision                                                                                                                                                                                                                                                                                   | Alternatives considered                                                                                                                |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| A-1 | Track focus is strict ISO C23 (`src/include/systems/huffman_tree.h`, `src/c/systems/huffman_tree.c`, `test/c/systems/huffman_tree_c_test.cpp`)                                                                                                                                                          | C++23 wrapper class (deferred; C23 header is `extern "C"` dual-language safe)                                                          |
| A-2 | `struct huffman_tree` embeds a fixed-capacity node pool (`nodes[HUFFMAN_MAX_NODES]`), and `struct huffman_node` uses `parent` / `left` / `right` pointers into that pool plus `symbol`, `has_symbol` (queried via `huffman_node_is_leaf`), and `is_full`                                                 | Heap-allocated nodes (rejected: violates zero-allocation rule) or integer pool indices                                                 |
| A-3 | Input mapping is passed as `const struct huffman_symbol_length* entries` with `size_t count`, where each entry holds `{uint8_t symbol, uint8_t length}`                                                                                                                                                 | Fixed 256-byte array `uint8_t lengths[256]`                                                                                            |
| A-4 | Maximum symbol count is `HUFFMAN_MAX_SYMBOLS = 256`; maximum code length is `HUFFMAN_MAX_CODE_LENGTH = 32`; node pool size is `HUFFMAN_MAX_NODES = 1024`; max tree leaf capacity is `HUFFMAN_MAX_TREE_CAPACITY = 1ULL << 32`                                                                            | `HUFFMAN_MAX_CODE_LENGTH = 255` (rejected: `32` fits exact fixed-point Kraft arithmetic in `uint64_t` and covers all practical codecs) |
| A-5 | The primary question and Follow-ups 1, 3, and 4 are merged into Task 1 (`huffman_tree_build`, `huffman_node_is_full`, and `huffman_tree_decode`); Task 2 is the optimized implementation (`huffman_tree_build_optimized`) featuring linear-time tree construction after sorting and micro-optimizations | Separate functions per follow-up question (rejected by user in favor of two primary tasks: basic and optimized)                        |
| A-6 | Every operational function validates its inputs and returns `[[nodiscard]] enum huffman_error` (`HUFFMAN_OK` on success, specific error code on invalid or infeasible input such as `{'a': 1, 'b': 1, 'c': 2}`)                                                                                         | Returning raw booleans or leaving invalid inputs unchecked                                                                             |
| A-7 | `huffman_tree_decode` reads bits MSB-first within each byte (`(bitstream[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1`), starting at bit 0 of `bitstream[0]`                                                                                                                                                | LSB-first bit packing                                                                                                                  |
| A-8 | `huffman_node_is_full` (Follow-up 4) must not read or rely on `node->is_full`; it must treat `is_full` as if it does not exist and verify fullness strictly from the tree's structural links and leaf state                                                                                             | Returning `root->is_full` directly (rejected: bypasses the Follow-up 4 structural check)                                               |

## 4. Glossary

| Term                | Meaning                                                                                                                                     |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Canonical order     | Sorted ascending by `(length, symbol)`: shorter paths first; ties broken by smaller `uint8_t` symbol value                                  |
| Left-aligned tree   | A binary prefix tree in which no new leaf can be added to the left of any existing leaf                                                     |
| Full node / subtree | A leaf node (`has_symbol == true`), or an internal node whose `left` and `right` children are both non-null and full                        |
| Kraft sum           | $\sum_{i=0}^{n-1} 2^{-\ell_i}$; an encoding is feasible iff the Kraft sum is $\le 1$, and produces a full tree iff the Kraft sum equals $1$ |

## 5. Interface overview

The header `src/include/systems/huffman_tree.h` is authoritative for signatures:

- Constants: `HUFFMAN_MAX_SYMBOLS = 256U`, `HUFFMAN_MAX_CODE_LENGTH = 32U`, `HUFFMAN_MAX_NODES = 1024U`, `HUFFMAN_MAX_TREE_CAPACITY = 1ULL << 32U`.
- `enum huffman_error : int32_t` — status and error codes (`HUFFMAN_OK`, `HUFFMAN_ERR_NOT_IMPLEMENTED`, `HUFFMAN_ERR_NULL_PTR`, `HUFFMAN_ERR_EMPTY_INPUT`,
  `HUFFMAN_ERR_TOO_MANY_SYMBOLS`, `HUFFMAN_ERR_INVALID_LENGTH`, `HUFFMAN_ERR_DUPLICATE_SYMBOL`, `HUFFMAN_ERR_ILLEGAL_ENCODING`,
  `HUFFMAN_ERR_INVALID_NODE`, `HUFFMAN_ERR_INVALID_BITSTREAM`, `HUFFMAN_ERR_BUFFER_TOO_SMALL`) and `huffman_error_to_string()`.
- `struct huffman_symbol_length` — `{uint8_t symbol; uint8_t length;}`.
- `struct huffman_node` — `{struct huffman_node* parent; struct huffman_node* left; struct huffman_node* right; uint8_t symbol; bool has_symbol; bool is_full;}`
  plus inline helper `huffman_node_is_leaf(node)`.
- `struct huffman_tree` — `{struct huffman_node* root; size_t node_count; size_t leaf_count; struct huffman_node nodes[HUFFMAN_MAX_NODES];}`.
- `huffman_tree_build(tree, entries, count)` — Task 1 (Basic Question + Follow-ups 1 & 3): validates input and legality, sorts by `(length, symbol)`, and
  constructs the canonical left-aligned tree while maintaining `node->is_full`.
- `huffman_tree_build_optimized(tree, entries, count)` — Task 2 (Follow-up 2 + micro-optimizations): validates input and legality, sorts, and constructs the
  canonical left-aligned tree in linear time after sorting using an active rightmost-spine stack.
- `huffman_node_is_full(root, out_is_full)` — Task 1 (Follow-up 4): validates `root` and `out_is_full` and checks whether the left-aligned tree/subtree
  rooted at `root` is full **without** using the `node->is_full` property (pretending `is_full` does not exist).
- `huffman_tree_decode(tree, bitstream, bit_count, out_bytes, out_capacity, out_written)` — Bitstream decoder using the constructed tree.

## 6. Functional requirements

### HUFF-REQ-001: Leaf-only values and binary path semantics

In every constructed tree, symbol values shall appear only at leaf nodes (`huffman_node_is_leaf(node) == true`, `left == nullptr`, `right == nullptr`), and
every internal node shall have `huffman_node_is_leaf(node) == false` and at least one child (`left != nullptr` or `right != nullptr`). Descending `left` on a
`0`-bit and `right` on a `1`-bit from `tree->root` for a symbol's assigned code of length $\ell$ shall reach a leaf storing that exact `symbol` at depth $\ell$.

- **Verification:** Test

### HUFF-REQ-002: Canonical ordering (short paths left, smaller symbols left)

`huffman_tree_build` and `huffman_tree_build_optimized` shall accept `entries` in any order and place leaves such that:

1. If symbol $A$ has a shorter path length than symbol $B$ ($\ell_A < \ell_B$), the path to $A$ is lexicographically to the left of the path to $B$.
2. If symbols $A$ and $B$ have the same path length ($\ell_A = \ell_B$) and $A < B$, the path to $A$ is lexicographically to the left of the path to $B$.

For `{'b': 2, 'e': 1, 'z': 2}`, `'e'` shall be at path `0` (`root->left`), `'b'` at path `10` (`root->right->left`), and `'z'` at path `11`
(`root->right->right`).

- **Verification:** Test

### HUFF-REQ-003: Left-aligned non-full tree construction

When the input encoding is legal but does not fill the entire binary tree ($\sum 2^{-\ell_i} < 1$), `huffman_tree_build` and `huffman_tree_build_optimized`
shall construct a left-aligned tree in which no new leaf could be added to the left of any existing leaf (equivalently, every internal node whose `right` child
is non-null has a non-null, full `left` subtree).

- **Verification:** Test
- **Rationale:** Incomplete Huffman tables (for example a single-symbol stream or sparse alphabet) must still produce a unique, deterministic prefix tree.

### HUFF-REQ-004: Follow-up 1 — O(1) node fullness property during construction

Every `struct huffman_node` in a constructed tree shall maintain `is_full` in amortized $O(1)$ time such that `node->is_full` is `true` for every leaf node and
`true` for an internal node if and only if both `node->left` and `node->right` are non-null and have `is_full == true`. Checking whether any child subtree is
full during tree construction shall run in $O(1)$ time.

- **Verification:** Test

### HUFF-REQ-005: Follow-up 2 — Optimized linear-time tree construction

`huffman_tree_build_optimized(tree, entries, count)` shall construct the exact same canonical left-aligned tree as `huffman_tree_build`, performing the tree
construction phase after sorting in $O(\text{nodes})$ linear time (using an explicit stack of open ancestor nodes along the rightmost active spine rather than
re-walking from `tree->root` for every symbol).

- **Verification:** Test

### HUFF-REQ-006: Follow-up 3 — Infeasible encoding rejection

`huffman_tree_build` and `huffman_tree_build_optimized` shall verify that the input encoding is feasible before or during construction (satisfying Kraft's
inequality $\sum_{i=0}^{\text{count}-1} 2^{-\ell_i} \le 1$). When an input represents an infeasible tree where the tree becomes full before all paths are added
(for example, `{'a': 1, 'b': 1, 'c': 2}`), both functions shall return `HUFFMAN_ERR_ILLEGAL_ENCODING`.

- **Verification:** Test
- **Rationale:** Accepting an over-subscribed prefix table would either overflow node storage or overwrite existing leaves, corrupting decoded output.

### HUFF-REQ-007: Follow-up 4 — Structural tree/subtree fullness check (without `is_full`)

`huffman_node_is_full(root, out_is_full)` models receiving a Huffman tree or subtree rooted at `root` from an external source and parsing the tree structure to
check for fullness. It **shall not** read or rely on the `node->is_full` property (pretending `node->is_full` does not exist):

- If `root == nullptr` or `out_is_full == nullptr`, it shall return `HUFFMAN_ERR_NULL_PTR`.
- If `root` is a leaf node (`huffman_node_is_leaf(root)`) or any visited node along the rightmost spine is structurally invalid (`!huffman_node_is_valid`), it
  shall return `HUFFMAN_ERR_INVALID_NODE`.
- Otherwise, by walking the rightmost spine (`curr = curr->right`) of the left-aligned tree without inspecting `is_full`, it shall set `*out_is_full = true` if
  and only if the tree/subtree rooted at `root` is full (ending at a valid leaf), set `*out_is_full = false` when incomplete, and return `HUFFMAN_OK`.
- **Verification:** Test

### HUFF-REQ-008: Bitstream decoding through the constructed tree

`huffman_tree_decode(tree, bitstream, bit_count, out_bytes, out_capacity, out_written)` shall read `bit_count` bits MSB-first from `bitstream`, traversing from
`tree->root` (`0` = `left`, `1` = `right`) and emitting each leaf's `symbol` into `out_bytes` while resetting to `tree->root` for the next bit. It shall store
the number of emitted bytes in `*out_written` and return:

- `HUFFMAN_OK` when all `bit_count` bits are consumed and the traversal ends cleanly at the root (including when `bit_count == 0`);
- `HUFFMAN_ERR_INVALID_NODE` when `tree` is unbuilt (`tree->root == nullptr`, `tree->node_count == 0`, or `tree->leaf_count == 0`) or a visited node is
  structurally corrupt (`!huffman_node_is_valid`);
- `HUFFMAN_ERR_INVALID_BITSTREAM` when a bit branches to a `nullptr` child in a non-full tree, or `bit_count` expires mid-path at an internal node;
- `HUFFMAN_ERR_BUFFER_TOO_SMALL` when emitting a symbol would exceed `out_capacity`.
- **Verification:** Test

### HUFF-REQ-009: Documented complexity analysis

`src/c/systems/huffman_tree.c` shall document the time and auxiliary space complexity of:

1. Task 1 (`huffman_tree_build` and `huffman_node_is_full`), including sorting, root-down insertion with $O(1)$ `is_full` tracking, and Kraft legality
   checking;
2. Task 2 (`huffman_tree_build_optimized`), explaining how maintaining the active rightmost spine (and counting/radix sort if used) makes tree construction run
   in linear time.

- **Verification:** Review

## 7. Concurrency requirements

### HUFF-REQ-010: Reentrancy and thread-safe concurrent tree operations

All functions shall be reentrant and use no mutable `static` or global state. Concurrent calls to `huffman_tree_build`, `huffman_tree_build_optimized`,
`huffman_node_is_full`, and `huffman_tree_decode` on independent `struct huffman_tree` instances (or concurrent read-only calls to `huffman_node_is_full` and
`huffman_tree_decode` sharing a `const struct huffman_tree*`) shall be free of data races under ThreadSanitizer.

- **Verification:** Test (`HuffmanTreeConcurrencyCTest`, run under TSan)
- **Rationale:** Multiple communication or decompression channels on an ECU may decode frames concurrently on different RTOS tasks.

## 8. Resource and memory requirements

### HUFF-REQ-011: Zero heap allocation

No function in `huffman_tree` shall allocate heap memory (`malloc`, `calloc`, `realloc`, `free`). All tree nodes shall reside within the caller-provided
`struct huffman_tree` arena (`nodes[HUFFMAN_MAX_NODES]`).

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Dynamic allocation in a decoder risks heap fragmentation, unbounded worst-case execution time, and out-of-memory faults (ISO 26262-6:2018
  Table 6 item 1b; MISRA C:2023 Dir 4.12 / Rule 21.3).

### HUFF-REQ-012: Bounded stack and no recursion

All functions shall execute iteratively without recursion (`misc-no-recursion`) or variable-length arrays (`-Wvla`), using $O(1)$ stack frames bounded by
`HUFFMAN_MAX_CODE_LENGTH` (`32`).

- **Verification:** Review (enforced by `clang-tidy` `misc-no-recursion` and `-Werror=vla`)
- **Rationale:** Unbounded recursion risks stack overflow on small embedded RTOS task stacks.

## 9. Robustness and failure-model requirements

### HUFF-REQ-013: Null pointer rejection across all functions

Every function shall return `HUFFMAN_ERR_NULL_PTR` when a required pointer argument is `nullptr` (`tree`, `entries`, `root`, `out_is_full`, `out_written`,
`bitstream` when `bit_count > 0`, or `out_bytes` when `out_capacity > 0`).

- **Verification:** Test

### HUFF-REQ-014: Empty input, excessive symbol count, and invalid code lengths

`huffman_tree_build` and `huffman_tree_build_optimized` shall return:

- `HUFFMAN_ERR_EMPTY_INPUT` when `count == 0`;
- `HUFFMAN_ERR_TOO_MANY_SYMBOLS` when `count > HUFFMAN_MAX_SYMBOLS` (`256`);
- `HUFFMAN_ERR_INVALID_LENGTH` when any entry has `length == 0` or `length > HUFFMAN_MAX_CODE_LENGTH` (`32`).
- **Verification:** Test

### HUFF-REQ-015: Duplicate symbol detection

`huffman_tree_build` and `huffman_tree_build_optimized` shall return `HUFFMAN_ERR_DUPLICATE_SYMBOL` when two or more entries in `entries[0..count-1]` specify
the same `symbol` byte.

- **Verification:** Test

### HUFF-REQ-016: Deterministic validation order and clean state on error

When multiple errors are present in `huffman_tree_build` or `huffman_tree_build_optimized`, validation shall report the first error in this exact order:

1. `HUFFMAN_ERR_NULL_PTR`
2. `HUFFMAN_ERR_EMPTY_INPUT`
3. `HUFFMAN_ERR_TOO_MANY_SYMBOLS`
4. (`HUFFMAN_ERR_INVALID_LENGTH` OR `HUFFMAN_ERR_DUPLICATE_SYMBOL` OR `HUFFMAN_ERR_ILLEGAL_ENCODING`)

On any error (other than `tree == nullptr`), `tree->root` shall be `nullptr`, `tree->node_count` shall be `0`, and `tree->leaf_count` shall be `0`.

- **Verification:** Test

## 10. Portability requirements

### HUFF-REQ-017: Pure ISO C23 portability

The component shall be written in strict ISO C23 (`-std=c23`) using fixed-width integers (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<stdckdint.h>`), with no
POSIX or OS-specific headers, and the public header shall be usable from both ISO C23 and C++23 translation units.

- **Verification:** Test (`systems_c23_test` and `huffman_tree_c_test`)

## 11. Failure modes (mini-FMEA)

| Failure mode                                                                                                 | Effect                                                                | Detection / mitigation                                                                                                     | Requirement  |
| ------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------- | ------------ |
| Over-subscribed lengths (e.g. `{'a': 1, 'b': 1, 'c': 2}`)                                                    | Tree full before all leaves placed; out-of-bounds or overwritten leaf | Exact 64-bit fixed-point Kraft sum check ($\sum 2^{32-\ell_i} \le 2^{32}$) and root fullness check before/during insertion | HUFF-REQ-006 |
| Duplicate symbol in input table                                                                              | Ambiguous encoding, wasted leaf slot                                  | 256-bit / boolean seen-table check during input validation                                                                 | HUFF-REQ-015 |
| Code length 0 or $> 32$                                                                                      | Undefined shift in $1ULL \ll (32 - \ell)$ or path stack overflow      | Explicit range check `1 <= length <= 32` prior to bit shifts                                                               | HUFF-REQ-014 |
| Truncated or corrupted bitstream                                                                             | Decoder stops at internal node or takes `nullptr` branch              | Return `HUFFMAN_ERR_INVALID_BITSTREAM` if child is `nullptr` or cursor $\ne$ `root` after `bit_count` bits                 | HUFF-REQ-008 |
| Degenerate internal node (`!huffman_node_is_leaf(node) && left == nullptr`) passed to `huffman_node_is_full` | Misclassified subtree state                                           | Return `HUFFMAN_ERR_INVALID_NODE`                                                                                          | HUFF-REQ-007 |

## 12. Standards guidance (informative)

| Topic                       | Reference                                                       | How it applies here                                                        |
| --------------------------- | --------------------------------------------------------------- | -------------------------------------------------------------------------- |
| Dynamic memory              | ISO 26262-6:2018 Table 6 (1b); MISRA C:2023 Dir 4.12, Rule 21.3 | Fixed node pool inside `struct huffman_tree`; zero `malloc`/`free`         |
| Recursion and bounded stack | ISO 26262-6:2018 Table 6 (1e); MISRA C:2023 Rule 17.2           | Iterative traversal and stack-spine construction only                      |
| Defensive input validation  | ISO 26262-6:2018 Table 1 (1d); CERT C `INT34-C`                 | Validate `1 <= length <= 32` before shifting; reject infeasible Kraft sums |
| Process traceability        | ASPICE SWE.1 / SWE.4                                            | Requirement IDs traced to GoogleTest cases via `RecordProperty`            |

Rule numbers are informative pointers; confirm them against licensed copies of the standards before relying on them.

## 13. Out of scope

- Building an optimal Huffman tree from raw symbol _frequencies_ (this component reconstructs the canonical decoding tree from the `(symbol, code_length)`
  mapping).
- Multi-bit table-accelerated (`FAST_BITS` lookup table) decoding; `huffman_tree_decode` traverses the explicit binary prefix tree.
- Dynamic or adaptive Huffman coding.

## 14. Implementation hints (optional, spoiler-free)

- **Kraft's inequality in exact integer arithmetic (Follow-up 3 & 4):** With $\ell_i \in [1, 32]$, each leaf at depth $\ell_i$ consumes a fraction
  $2^{-\ell_i}$ of the tree's leaf capacity. Scaling by $2^{32}$ turns each term into an exact integer weight $2^{32 - \ell_i}$ (`1ULL << (32U - length)`).
  The encoding is legal iff the sum of weights is $\le 2^{32}$ (`1ULL << 32U`), and the resulting left-aligned tree is full iff the sum equals $2^{32}$.
- **Node pool bound:** In any left-aligned binary prefix tree with $N \le 256$ leaves and maximum depth $L \le 32$, there are at most $N - 1$ two-child
  internal nodes and at most $L - 1$ one-child internal nodes (all lying along the rightmost spine), so total nodes never exceed $2N + L - 2 = 542 < 1024$.
- **Constant-time fullness during construction (Follow-up 1):** A leaf is born with `is_full = true`. An internal node becomes `is_full = true` as soon as both
  its `left` and `right` children exist and have `is_full == true`. Walking `parent` upward only while `node->is_full` transitions to `true` maintains `is_full`
  in $O(1)$ amortized time per symbol.
- **Structural fullness check on an external tree (Follow-up 4):** In `huffman_node_is_full`, we pretend `node->is_full` does not exist and parse the given
  tree. A general tree can be verified iteratively in $O(\text{nodes})$ time and $O(1)$ auxiliary space using `parent` pointers; if the tree is known to be
  left-aligned, any incomplete internal node must lie along the rightmost path (`curr = curr->right`) from `root`, allowing an $O(\text{depth})$ check.
- **Linear-time construction after sorting (Follow-up 2 / Task 2):** When symbols are sorted by `(length, symbol)`, consecutive leaves visit the tree in strict
  left-to-right pre-order. Instead of walking down from `root` for every symbol, keep a small stack of active (non-full) ancestor nodes along the current path
  (at most 32 deep). Pop full nodes from the top of the stack to find the lowest open ancestor for the next leaf, then push new internal nodes down to
  `length - 1` and attach the leaf. Every node in the tree is pushed once and popped at most once. Furthermore, because `length` $\in [1, 32]$ and `symbol`
  $\in [0, 255]$ are small integers, sorting itself can be done via counting/bucket sort in $O(N)$ time!
