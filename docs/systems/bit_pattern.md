# BitPattern — Software Requirements

| Field | Value |
| --- | --- |
| Component | `bit_pattern` (`src/include/systems/bit_pattern.h`, `src/c/systems/bit_pattern.c`) |
| Requirement prefix | `BITPAT` |
| Status | Implemented (ISO C23) |
| Integrity context | Practice component written with ISO 26262 / MISRA C:2023 rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX, bare-metal MCUs (pure ISO C23 computation, no OS calls); macOS (development host) |

## 1. Problem statement

> **Find a pattern in the given bit stream**
>
> ## Question Details
>
> You are given an incoming bit stream/data and the size in number of bits (which may not be a multiple of 8, e.g., it could be 11, 31, etc.). You are also
> given a bit pattern and size in bits (again not necessarily a multiple of 8).
>
> ### example definition
>
> ```c
> enum FoundError found(void *in_data, size_t data_size, void *pattern, size_t pattern_size);
> ```
>
> ### Function Expectations
>
> `enum FoundError` will have `FOUND_PATTERN = 0`, `PATTERN_NOT_FOUND = 1` and can optionally have other errors greater than 1.
>
> ## Rating
>
> - Can implement a solution for byte aligned data
> - Can implement the O(N^2) algorithm for non-byte aligned data sizes
> - Can find a solution with O(N) (KNP) algorithm with all corner cases handled

## 2. Context and intent

Aerospace telemetry receivers, CAN-FD / automotive sensor bridges, and serial communication links frequently scan raw incoming bit streams for synchronization
markers, frame delimiters, or fault signatures whose bit length is not a multiple of 8 and whose start position may fall at any bit offset across byte
boundaries. On a deterministic microcontroller or safety core, the matcher must operate in-place over caller-supplied buffers without dynamic memory allocation
(`malloc`/`free`), variable-length arrays, or recursion, and must guarantee linear $O(N + M)$ worst-case execution time (WCET) via Knuth-Morris-Pratt (KMP) so
adversarial or repetitive bit streams cannot trigger quadratic stalls.

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | Track focus is strict ISO C23 (`src/include/systems/bit_pattern.h`, `src/c/systems/bit_pattern.c`, `test/c/systems/bit_pattern_c_test.cpp`, and `test/c/systems/systems_c23_test.c`), matching `huffman_tree` | Adding a redundant C++23 wrapper class (`extern "C"` header is already dual-language safe) |
| A-2 | Bit numbering within each byte is **MSB-first**: bit index $i$ ($0 \le i < \text{size}$) is `(((const uint8_t*)buf)[i >> 3U] >> (7U - (i & 7U))) & 1U`. When `size % 8 != 0`, only the top `size % 8` bits of the final byte `buf[(size - 1) / 8]` are part of the stream/pattern; the low `8 - (size % 8)` bits are ignored padding | LSB-first bit numbering |
| A-3 | Input buffer pointers use `const void*` (`found(const void* in_data, size_t data_size, const void* pattern, size_t pattern_size)`), which accepts both mutable `void*`/`uint8_t*` and `const void*`/`const uint8_t*` without violating `-Wcast-qual` | Non-const `void*` (forces callers with read-only buffers to cast away `const`) |
| A-4 | All three Rating tiers are exposed as individually testable functions (`bit_pattern_find_byte_aligned`, `bit_pattern_find_naive`, `bit_pattern_find_kmp`) in addition to the official prompt function `found(...)` (which delegates to `bit_pattern_find_kmp`) | Exposing only `found(...)` (would prevent unit-testing the byte-aligned and $O(N \cdot M)$ tiers directly once KMP is implemented) |
| A-5 | `bit_pattern_find_byte_aligned` is a completely byte-aligned warm-up tier: it requires `data_size % 8 == 0` and `pattern_size % 8 == 0` (returning `FOUND_ERR_UNALIGNED_SIZE` otherwise) and searches only byte-aligned starting offsets $s \bmod 8 = 0$ ($0 \le \text{byte\_offset} \le (\text{data\_size} - \text{pattern\_size}) / 8$), whereas `bit_pattern_find_naive` and `bit_pattern_find_kmp` / `found` search all bit offsets $0 \le s \le \text{data\_size} - \text{pattern\_size}$ | Searching cross-byte bit offsets in `bit_pattern_find_byte_aligned` |
| A-6 | Zero-size inputs are rejected with explicit error codes $> 1$: `data_size == 0` returns `FOUND_ERR_EMPTY_DATA` and `pattern_size == 0` returns `FOUND_ERR_EMPTY_PATTERN`. When `pattern_size > data_size` (and both sizes are otherwise valid), all functions return `PATTERN_NOT_FOUND` (`1`) | Treating empty pattern as `FOUND_PATTERN` (`0`) |
| A-7 | Maximum supported `pattern_size` is `BIT_PATTERN_MAX_PATTERN_BITS = 1024U` bits so the KMP longest proper prefix-suffix (`lps`) table fits in a fixed `uint16_t lps[1024]` stack array (`2048` bytes) with zero heap allocation and no VLAs; `pattern_size > 1024U` returns `FOUND_ERR_PATTERN_TOO_LONG` | Heap-allocated or VLA `lps` table (violates zero-allocation and bounded-stack safety rules) |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| MSB-first bit index | Bit $i$ of buffer `B` is bit $7 - (i \bmod 8)$ of `B[i / 8]`; bit 0 is the most-significant bit ($2^7$) of `B[0]` |
| Padding bits | When $\text{size} \bmod 8 \ne 0$, the low $8 - (\text{size} \bmod 8)$ bits of the final byte `B[(size - 1) / 8]` that lie at bit indices $\ge \text{size}$ |
| Byte-aligned sizes | `data_size % 8 == 0` and `pattern_size % 8 == 0`, so every byte in both buffers contains 8 valid bits and zero padding bits |
| LPS table | KMP Longest Proper Prefix which is also a Suffix table: `lps[q]` stores the length of the longest proper prefix of `pattern[0..q]` that is also a suffix of `pattern[0..q]` |

## 5. Interface overview

The header `src/include/systems/bit_pattern.h` is authoritative for signatures:

- Constants: `BIT_PATTERN_BITS_PER_BYTE = 8U`, `BIT_PATTERN_MAX_PATTERN_BITS = 1024U`.
- `enum FoundError : int32_t` — status and error codes:
  - `FOUND_PATTERN = 0` — pattern occurs at least once in the valid bits of `in_data`.
  - `PATTERN_NOT_FOUND = 1` — inputs are valid, but `pattern` does not occur in `in_data` (including when `pattern_size > data_size`).
  - `FOUND_ERR_NOT_IMPLEMENTED = 2` — returned by unimplemented TDD red stubs.
  - `FOUND_ERR_NULL_PTR = 3` — `in_data == nullptr` or `pattern == nullptr`.
  - `FOUND_ERR_EMPTY_DATA = 4` — `data_size == 0U`.
  - `FOUND_ERR_EMPTY_PATTERN = 5` — `pattern_size == 0U`.
  - `FOUND_ERR_PATTERN_TOO_LONG = 6` — `pattern_size > BIT_PATTERN_MAX_PATTERN_BITS` (`1024U`).
  - `FOUND_ERR_UNALIGNED_SIZE = 7` — `data_size % 8U != 0U` or `pattern_size % 8U != 0U` (returned by `bit_pattern_find_byte_aligned` only).
- `found_error_to_string(error)` — human-readable description of any `enum FoundError` value.
- `bit_pattern_find_byte_aligned(in_data, data_size, pattern, pattern_size)` — Rating Tier 1: completely byte-aligned warm-up (`data_size % 8 == 0` and
  `pattern_size % 8 == 0`, matching only at byte-aligned starting offsets $s \bmod 8 = 0$).
- `bit_pattern_find_naive(in_data, data_size, pattern, pattern_size)` — Rating Tier 2: $O(N \cdot M)$ sliding-window bit matcher for arbitrary bit sizes and
  any bit offset.
- `bit_pattern_find_kmp(in_data, data_size, pattern, pattern_size)` — Rating Tier 3: $O(N + M)$ Knuth-Morris-Pratt bit stream matcher with bounded stack `lps`
  table and all corner cases handled.
- `found(in_data, data_size, pattern, pattern_size)` — Official prompt entry point delegating to `bit_pattern_find_kmp`.

## 6. Functional requirements

### BITPAT-REQ-001: MSB-first bit stream numbering and match semantics

Within both `in_data` and `pattern`, bit index $i$ ($0 \le i < \text{size}$) shall be defined as bit $7 - (i \bmod 8)$ of byte $\lfloor i / 8 \rfloor$, i.e.
`(((const uint8_t*)buf)[i >> 3U] >> (7U - (i & 7U))) & 1U`. For general bit-stream search (`bit_pattern_find_naive`, `bit_pattern_find_kmp`, and `found`), a
`pattern` of `pattern_size` bits matches `in_data` of `data_size` bits if and only if there exists at least one bit offset $s$ with
$0 \le s \le \text{data\_size} - \text{pattern\_size}$ such that for all $j \in [0, \text{pattern\_size})$, bit $s + j$ of `in_data` equals bit $j$ of
`pattern`. When a match exists, the search functions shall return `FOUND_PATTERN` (`0`); when no match exists on valid inputs, they shall return
`PATTERN_NOT_FOUND` (`1`).

- **Verification:** Test

### BITPAT-REQ-002: Tier 1 — Byte-aligned stream search

`bit_pattern_find_byte_aligned(in_data, data_size, pattern, pattern_size)` shall search for `pattern` in `in_data` when both `data_size` and `pattern_size` are
non-zero multiples of 8 (`data_size % 8 == 0` and `pattern_size % 8 == 0`), checking only byte-aligned starting offsets ($s \bmod 8 = 0$, i.e. byte offsets
$0 \le b \le (\text{data\_size} - \text{pattern\_size}) / 8$). It shall return `FOUND_PATTERN` (`0`) when `pattern` matches at a byte boundary in `in_data`,
and `PATTERN_NOT_FOUND` (`1`) when `pattern` does not occur at a byte boundary (including when it occurs only at a cross-byte bit offset $s \bmod 8 \ne 0$).
If either `data_size % 8 != 0` or `pattern_size % 8 != 0`, it shall return `FOUND_ERR_UNALIGNED_SIZE` without reading either buffer.

- **Verification:** Test
- **Rationale:** Provides a simple warm-up byte-window matcher (`memcmp` over whole bytes) before generalizing to arbitrary bit offsets and non-byte-aligned
  sizes in Tiers 2 and 3.

### BITPAT-REQ-003: Tier 2 — Naive O(N * M) search for arbitrary non-byte-aligned bit sizes

`bit_pattern_find_naive(in_data, data_size, pattern, pattern_size)` shall accept arbitrary bit lengths `data_size >= 1` and
`1 <= pattern_size <= BIT_PATTERN_MAX_PATTERN_BITS` (including non-multiples of 8 such as `1`, `5`, `11`, `31`, and sizes $< 8$) and determine whether `pattern`
occurs at any bit offset $0 \le s \le \text{data\_size} - \text{pattern\_size}$ using an $O(N \cdot M)$ sliding-window bit comparison.

- **Verification:** Test

### BITPAT-REQ-004: Trailing padding bits in the final partial byte are ignored

When `data_size % 8 != 0` or `pattern_size % 8 != 0`, the remaining $8 - (\text{size} \bmod 8)$ low-order bits in the final byte
`((const uint8_t*)buf)[(size - 1) / 8]` are non-data padding bits. `bit_pattern_find_naive`, `bit_pattern_find_kmp`, and `found` shall never read or compare
bits at index $\ge \text{data\_size}$ in `in_data` or $\ge \text{pattern\_size}$ in `pattern`, and arbitrary `0`/`1` garbage in those trailing padding bits
shall not cause false positives or false negatives.

- **Verification:** Test
- **Rationale:** Unmasked trailing bits in the final byte of an unaligned buffer are a classic embedded defect that causes spurious matches or missed patterns.

### BITPAT-REQ-005: Tier 3 — Linear-time O(N + M) Knuth-Morris-Pratt (KMP) bit stream search

`bit_pattern_find_kmp(in_data, data_size, pattern, pattern_size)` and `found(in_data, data_size, pattern, pattern_size)` shall precompute the
Knuth-Morris-Pratt longest proper prefix-suffix (`lps`) table over the `pattern_size` bits in $O(M)$ time and scan the `data_size` bits of `in_data` in a
single $O(N)$ pass without backing up the stream bit index ($O(N + M)$ total time), producing the exact same result as `bit_pattern_find_naive` across all
valid inputs, including repetitive self-overlapping patterns (e.g. `1111...10` inside `1111...1110`) and matches at the very first or very last bit of
`in_data`.

- **Verification:** Test
- **Rationale:** Naive $O(N \cdot M)$ matching degrades quadratically on repetitive preamble or idle-fill bit sequences, violating real-time WCET budgets.

### BITPAT-REQ-006: Pattern longer than data stream returns PATTERN_NOT_FOUND

When all pointer and size arguments are valid (`in_data != nullptr`, `pattern != nullptr`, `data_size >= 1`,
`1 <= pattern_size <= BIT_PATTERN_MAX_PATTERN_BITS`, plus byte-alignment for `bit_pattern_find_byte_aligned`) and `pattern_size > data_size`, all search
functions shall return `PATTERN_NOT_FOUND` (`1`) in $O(1)$ time without reading from either buffer.

- **Verification:** Test

### BITPAT-REQ-007: Documented complexity analysis

`src/c/systems/bit_pattern.c` shall document the time and auxiliary space complexity of:

1. Tier 1 (`bit_pattern_find_byte_aligned`);
2. Tier 2 (`bit_pattern_find_naive`), showing why worst-case bit comparisons scale as $O((N - M + 1) \cdot M) = O(N \cdot M)$;
3. Tier 3 (`bit_pattern_find_kmp` and `found`), explaining why KMP preprocessing takes at most $2M$ steps and stream scanning takes at most $2N$ steps for
   strictly $O(N + M)$ worst-case time and $O(1)$ bounded stack space.

- **Verification:** Review

## 7. Concurrency requirements

### BITPAT-REQ-008: Reentrancy and thread-safe concurrent bit stream search

All functions in `bit_pattern` shall be pure, stateless, and reentrant with no mutable `static` or global state. Concurrent calls to `found`,
`bit_pattern_find_byte_aligned`, `bit_pattern_find_naive`, and `bit_pattern_find_kmp` across multiple threads—whether on independent buffers or sharing the
same read-only `in_data` and `pattern` buffers—shall be free of data races under ThreadSanitizer.

- **Verification:** Test (`BitPatternConcurrencyCTest`, run under TSan)
- **Rationale:** Multiple telemetry or bus-monitoring tasks on an ECU may scan bit streams concurrently without external locking.

## 8. Resource and memory requirements

### BITPAT-REQ-009: Zero heap allocation

No function in `bit_pattern` shall allocate dynamic memory (`malloc`, `calloc`, `realloc`, `free`). The KMP `lps` table shall reside in a fixed-capacity stack
array bounded by `BIT_PATTERN_MAX_PATTERN_BITS` (`1024`).

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Dynamic allocation in a real-time parser risks heap fragmentation, unbounded latency, and out-of-memory faults (ISO 26262-6:2018 Table 6
  item 1b; MISRA C:2023 Dir 4.12 / Rule 21.3).

### BITPAT-REQ-010: Bounded stack and no recursion or VLAs

All functions shall execute iteratively without recursion (`misc-no-recursion`) or variable-length arrays (`-Wvla`), using $O(1)$ stack space bounded by
`BIT_PATTERN_MAX_PATTERN_BITS * sizeof(uint16_t)` (`2048` bytes).

- **Verification:** Review (enforced by `clang-tidy` `misc-no-recursion` and `-Werror=vla`)
- **Rationale:** Variable-length arrays or recursion sized by caller parameters risk stack overflow on small embedded RTOS task stacks.

## 9. Robustness and failure-model requirements

### BITPAT-REQ-011: Null pointer rejection

Every search function (`found`, `bit_pattern_find_byte_aligned`, `bit_pattern_find_naive`, `bit_pattern_find_kmp`) shall return `FOUND_ERR_NULL_PTR` when
`in_data == nullptr` or `pattern == nullptr`.

- **Verification:** Test

### BITPAT-REQ-012: Empty data, empty pattern, and oversized pattern rejection

Every search function shall return:

- `FOUND_ERR_EMPTY_DATA` when `data_size == 0U`;
- `FOUND_ERR_EMPTY_PATTERN` when `pattern_size == 0U`;
- `FOUND_ERR_PATTERN_TOO_LONG` when `pattern_size > BIT_PATTERN_MAX_PATTERN_BITS` (`1024U`).
- **Verification:** Test
- **Rationale:** Validating `1 <= pattern_size <= BIT_PATTERN_MAX_PATTERN_BITS` before building the KMP `lps` table prevents out-of-bounds stack writes.

### BITPAT-REQ-013: Deterministic validation precedence order

When an input violates multiple preconditions, validation shall report the first error in this exact order before reading any buffer bytes:

1. `FOUND_ERR_NULL_PTR` (`in_data == nullptr` or `pattern == nullptr`)
2. `FOUND_ERR_EMPTY_DATA` (`data_size == 0U`)
3. `FOUND_ERR_EMPTY_PATTERN` (`pattern_size == 0U`)
4. `FOUND_ERR_UNALIGNED_SIZE` (`bit_pattern_find_byte_aligned` only: `data_size % 8U != 0U` or `pattern_size % 8U != 0U`)
5. `FOUND_ERR_PATTERN_TOO_LONG` (`pattern_size > BIT_PATTERN_MAX_PATTERN_BITS`)
6. `PATTERN_NOT_FOUND` (`pattern_size > data_size`)

- **Verification:** Test

## 10. Portability requirements

### BITPAT-REQ-014: Pure ISO C23 portability and dual-language header safety

The component shall be written in strict ISO C23 (`-std=c23`) using fixed-width integers (`<stdbool.h>`, `<stddef.h>`, `<stdint.h>`), with no POSIX or
OS-specific headers, no unaligned multi-byte pointer dereferences, and no host-endianness assumptions, and the public header shall be usable from both ISO C23
and C++23 translation units.

- **Verification:** Test (`systems_c23_test` and `bit_pattern_c_test`)

## 11. Failure modes (mini-FMEA)

| Failure mode | Effect | Detection / mitigation | Requirement |
| --- | --- | --- | --- |
| Byte-only search (`bit_pattern_find_byte_aligned`) used on unaligned bit stream | Only detects matches at byte boundaries ($s \bmod 8 = 0$) and rejects non-multiple-of-8 sizes | Tier 2 (`bit_pattern_find_naive`) and Tier 3 (`bit_pattern_find_kmp` / `found`) handle arbitrary bit offsets and bit lengths | BITPAT-REQ-002, BITPAT-REQ-003, BITPAT-REQ-005 |
| Unmasked trailing padding bits in final partial byte | False match against garbage padding or missed valid match | Explicit `0x00` vs `0xFF` padding-garbage invariance tests | BITPAT-REQ-004 |
| Naive $O(N \cdot M)$ matcher fed repetitive bit stream | Quadratic worst-case execution time spike on real-time task | KMP $O(N + M)$ failure-function matcher (`bit_pattern_find_kmp` / `found`) | BITPAT-REQ-005 |
| Unsigned underflow on `data_size - pattern_size` when `pattern_size > data_size` | Out-of-bounds buffer read across gigabytes of address space | Check `pattern_size > data_size` before loop bound calculation | BITPAT-REQ-006 |
| `pattern_size > 1024` passed to KMP | Stack buffer overflow in fixed `lps` array | Reject `pattern_size > BIT_PATTERN_MAX_PATTERN_BITS` with `FOUND_ERR_PATTERN_TOO_LONG` | BITPAT-REQ-012 |

## 12. Standards guidance (informative)

| Topic | Reference | How it applies here |
| --- | --- | --- |
| Dynamic memory | ISO 26262-6:2018 Table 6 (1b); MISRA C:2023 Dir 4.12, Rule 21.3 | Fixed stack `lps[1024]` table; zero `malloc`/`free` |
| Recursion and bounded stack | ISO 26262-6:2018 Table 6 (1e); MISRA C:2023 Rule 17.2, Rule 18.8 | Iterative loops only; no VLAs |
| Defensive input validation | ISO 26262-6:2018 Table 1 (1d); CERT C `ARR30-C`, `INT30-C` | Guard `pattern_size > data_size` before subtraction; bound `pattern_size <= 1024` |
| Process traceability | ASPICE SWE.1 / SWE.4 | Requirement IDs traced to GoogleTest cases via `RecordProperty` |

Rule numbers are informative pointers; confirm them against licensed copies of the standards before relying on them.

## 13. Out of scope

- Returning all match bit offsets or match counts (the problem specifies a boolean-like existence result via `FOUND_PATTERN` vs `PATTERN_NOT_FOUND`).
- Regular expression or wildcard bit masks ("don't care" bits `X`).
- Streaming across multiple disconnected packet fragments without reassembling a contiguous buffer.

## 14. Implementation hints (optional, spoiler-free)

- **Bit extraction helper:** Write a `static inline uint8_t read_bit_msb(const uint8_t* bytes, size_t bit_idx)` helper that computes `byte_idx = bit_idx >> 3U`
  and `shift = 7U - (uint8_t)(bit_idx & 7U)`. Because the loop bounds strictly enforce `bit_idx < data_size` and `pat_idx < pattern_size`, trailing padding bits
  in the final byte are never accessed.
- **Tier 1 (Byte-aligned warm-up):** When `data_size % 8 == 0` and `pattern_size % 8 == 0`, convert both bit sizes to byte counts
  (`data_bytes = data_size >> 3U`, `pattern_bytes = pattern_size >> 3U`) and compare `pattern_bytes` at each byte offset
  `0 <= s <= data_bytes - pattern_bytes` via `memcmp`.
- **Tier 2 ($O(N \cdot M)$ naive):** Guard `if (pattern_size > data_size) return PATTERN_NOT_FOUND;` first so
  `const size_t max_start = data_size - pattern_size` cannot wrap around in unsigned `size_t` arithmetic.
- **Tier 3 ($O(N + M)$ KMP):** Build `uint16_t lps[BIT_PATTERN_MAX_PATTERN_BITS]` in $O(M)$ time where `lps[i]` is the length of the longest proper prefix of
  `pattern[0..i]` that matches a suffix of `pattern[0..i]`. During the $O(N)$ scan, maintain the current matched prefix length `q`; on a mismatch when `q > 0`,
  fall back to `q = lps[q - 1]` without advancing the data bit index. Notice that for short patterns ($M \le 64$), the Shift-And / Bitap automaton can also run
  in $O(N)$ time using a single `uint64_t` bitmask register, while KMP handles all $M \le 1024$ uniformly in $O(N + M)$ time.
