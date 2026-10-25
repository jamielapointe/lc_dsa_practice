#pragma once

/// @file bit_pattern.h
/// @brief Zero-allocation MSB-first bit-stream pattern matching across
/// byte-aligned, O(N * M) naive unaligned, and O(N + M) Knuth-Morris-Pratt
/// (KMP) algorithms in ISO C23.
///
/// Specification: docs/systems/bit_pattern.md (Prefix BITPAT).
///
/// Problem summary:
///   Given an incoming bit stream `in_data` of `data_size` bits (not
///   necessarily a multiple of 8, e.g. 11, 31) and a target `pattern` of
///   `pattern_size` bits (also not necessarily a multiple of 8), determine
///   whether `pattern` occurs as a contiguous bit substring anywhere within the
///   valid bits of `in_data`.
///
/// Bit numbering & padding semantics (BITPAT-REQ-001, BITPAT-REQ-004):
///   - Within each byte of `in_data` and `pattern`, bits are numbered
///   MSB-first:
///     bit index `i` (0 <= i < size) resides at byte `i / 8` (`i >> 3U`) and
///     bit position `7 - (i % 8)` (`7U - (i & 7U)`), i.e.,
///     `(((const uint8_t*)buf)[i >> 3U] >> (7U - (i & 7U))) & 1U`.
///   - When `data_size % 8 != 0` or `pattern_size % 8 != 0`, only bits
///     `0 .. size - 1` (the top `size % 8` bits of the final byte) are valid;
///     the low `8 - (size % 8)` bits in the final byte are non-data padding and
///     must be ignored regardless of their contents.
///
/// Three-tier Rating structure:
///   - Tier 1 (`bit_pattern_find_byte_aligned`):
///     * Completely byte-aligned warm-up: requires byte-aligned bit lengths
///       (`data_size % 8 == 0` and `pattern_size % 8 == 0`, returning
///       `FOUND_ERR_UNALIGNED_SIZE` otherwise) and searches only across
///       byte-aligned starting offsets `s % 8 == 0`
///       (`0 <= byte_offset <= (data_size - pattern_size) / 8`).
///   - Tier 2 (`bit_pattern_find_naive`):
///     * Supports arbitrary non-byte-aligned bit lengths (`data_size >= 1`,
///       `1 <= pattern_size <= BIT_PATTERN_MAX_PATTERN_BITS`), ignoring
///       trailing padding bits and searching across all bit offsets with the
///       O(N * M) sliding-window algorithm.
///   - Tier 3 (`bit_pattern_find_kmp` and `found`):
///     * Precomputes the Knuth-Morris-Pratt longest proper prefix-suffix
///     (`lps`)
///       table in a fixed stack buffer `uint16_t lps[1024]` in O(M) time and
///       scans `in_data` in a single O(N) pass without backing up the stream
///       index, achieving O(N + M) worst-case time and O(1) bounded stack
///       space.
///     * `found(...)` is the official entry point from the problem description
///       and delegates to `bit_pattern_find_kmp`.
///
/// Validation precedence (BITPAT-REQ-011, BITPAT-REQ-012, BITPAT-REQ-013):
///   1. `FOUND_ERR_NULL_PTR` (`in_data == nullptr` or `pattern == nullptr`)
///   2. `FOUND_ERR_EMPTY_DATA` (`data_size == 0U`)
///   3. `FOUND_ERR_EMPTY_PATTERN` (`pattern_size == 0U`)
///   4. `FOUND_ERR_UNALIGNED_SIZE` (`bit_pattern_find_byte_aligned` only:
///      `data_size % 8U != 0U` or `pattern_size % 8U != 0U`)
///   5. `FOUND_ERR_PATTERN_TOO_LONG` (`pattern_size > 1024U`)
///   6. `PATTERN_NOT_FOUND` (`pattern_size > data_size`)
///
/// Thread safety:
///   - All functions are pure, stateless, and reentrant (BITPAT-REQ-008).
///   - Concurrent calls across threads on separate or shared read-only buffers
///     are free of data races.
///
/// Memory model:
///   - Strictly zero heap allocation (`malloc`/`free` forbidden;
///     BITPAT-REQ-009).
///   - Iterative execution only; no recursion (`misc-no-recursion`) or VLAs
///     (BITPAT-REQ-010).
///
/// Implementation outline:
///   1. Implement an inline MSB-first bit reader `read_bit_msb(bytes, bit_idx)`
///      and enforce the exact validation error precedence above.
///   2. Implement `bit_pattern_find_byte_aligned`: verify `data_size % 8 == 0`
///      and `pattern_size % 8 == 0`, handle `pattern_size > data_size`, and
///      compare the pattern against every byte-aligned offset
///      `0 <= byte_offset <= (data_size - pattern_size) / 8` via `memcmp`.
///   3. Implement `bit_pattern_find_naive`: for each start bit `s` in
///      `0 .. data_size - pattern_size`, compare `pattern_size` bits one by one
///      without ever reading bit indices `>= data_size` or `>= pattern_size`.
///   4. Implement `bit_pattern_find_kmp` and `found`: build the KMP `lps` array
///      of size `BIT_PATTERN_MAX_PATTERN_BITS` in O(M) time, then scan the
///      `data_size` stream bits in O(N) time using `lps` fallback transitions
///      on mismatches.
///   Pitfalls to avoid:
///   - Unsigned subtraction underflow when computing `data_size - pattern_size`
///     before checking `pattern_size > data_size`.
///   - Comparing whole trailing bytes when `data_size % 8 != 0` or
///     `pattern_size % 8 != 0`, which leaks garbage padding bits into the
///     comparison.
///   - Passing bit counts instead of byte counts to `memcmp` or byte pointer
///     arithmetic in `bit_pattern_find_byte_aligned`.
///   - Infinite fallback loops in KMP `lps` construction if the loop starts at
///     `i = 0` instead of `i = 1` or if `len` is not updated via
///     `len = lps[len - 1U]`.
///
/// Dual-language safe: can be included by both ISO C23 and C++23 translation
/// units.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Number of bits per byte in a packed bit stream.
static constexpr size_t BIT_PATTERN_BITS_PER_BYTE = 8U;

/// @brief Maximum supported pattern length in bits for zero-heap KMP search.
/// @details Bounds the fixed-size stack `lps` table to 1024 `uint16_t` entries
/// (2048 bytes), avoiding dynamic allocation and variable-length arrays.
static constexpr size_t BIT_PATTERN_MAX_PATTERN_BITS = 1024U;

/// @brief Result and error codes for bit-stream pattern search operations.
/// @details `FOUND_PATTERN = 0` and `PATTERN_NOT_FOUND = 1` match the issue
/// specification; values `> 1` report precondition or capability errors.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum FoundError : int32_t {
  FOUND_PATTERN = 0,
  PATTERN_NOT_FOUND = 1,
  FOUND_ERR_NOT_IMPLEMENTED = 2,
  FOUND_ERR_NULL_PTR = 3,
  FOUND_ERR_EMPTY_DATA = 4,
  FOUND_ERR_EMPTY_PATTERN = 5,
  FOUND_ERR_PATTERN_TOO_LONG = 6,
  FOUND_ERR_UNALIGNED_SIZE = 7,
} FoundError;

static_assert(sizeof(enum FoundError) == 4U, "ABI requires 32-bit FoundError");

/// @brief Returns a human-readable string describing an `enum FoundError`.
/// @param error Status or error enumerator.
/// @return Non-null static string literal describing `error`.
/// @par Thread safety
/// Pure function; safe to call concurrently from any thread.
[[nodiscard]] const char* found_error_to_string(enum FoundError error);

/// @brief Searches for a byte-aligned bit pattern at byte-aligned offsets in a
/// byte-aligned bit stream (Rating Tier 1 warm-up: completely byte-aligned).
/// @param in_data Pointer to the packed MSB-first input bit stream buffer.
/// @param data_size Number of valid bits in `in_data` (must be a non-zero
/// multiple of 8).
/// @param pattern Pointer to the packed MSB-first target bit pattern buffer.
/// @param pattern_size Number of valid bits in `pattern` (must be a non-zero
/// multiple of 8, `<= BIT_PATTERN_MAX_PATTERN_BITS`).
/// @return `FOUND_PATTERN` (0) if `pattern` occurs at any byte-aligned offset
/// (`s % 8 == 0`) in `in_data`, `PATTERN_NOT_FOUND` (1) if `pattern` does not
/// occur at a byte boundary (including when `pattern_size > data_size`), or the
/// first applicable error in precedence order (`FOUND_ERR_NULL_PTR`,
/// `FOUND_ERR_EMPTY_DATA`, `FOUND_ERR_EMPTY_PATTERN`,
/// `FOUND_ERR_UNALIGNED_SIZE`, `FOUND_ERR_PATTERN_TOO_LONG`).
/// @par Thread safety
/// Reentrant and thread-safe on separate or shared read-only buffers.
/// @par Blocking
/// Non-blocking; completes in bounded O(N * M) time.
/// @par Requirements
/// BITPAT-REQ-001, BITPAT-REQ-002, BITPAT-REQ-006, BITPAT-REQ-008,
/// BITPAT-REQ-009, BITPAT-REQ-010, BITPAT-REQ-011, BITPAT-REQ-012,
/// BITPAT-REQ-013, BITPAT-REQ-014
[[nodiscard]] enum FoundError bit_pattern_find_byte_aligned(
    const void* in_data, size_t data_size, const void* pattern,
    size_t pattern_size);

/// @brief Searches for a bit pattern of arbitrary bit length in a bit stream of
/// arbitrary bit length using the naive O(N * M) sliding-window algorithm
/// (Rating Tier 2: non-byte-aligned data sizes).
/// @param in_data Pointer to the packed MSB-first input bit stream buffer.
/// @param data_size Number of valid bits in `in_data` (`>= 1`; trailing bits in
/// the final byte are ignored).
/// @param pattern Pointer to the packed MSB-first target bit pattern buffer.
/// @param pattern_size Number of valid bits in `pattern`
/// (`1 <= pattern_size <= BIT_PATTERN_MAX_PATTERN_BITS`; trailing bits in the
/// final byte are ignored).
/// @return `FOUND_PATTERN` (0) if `pattern` occurs at any bit offset in the
/// first `data_size` bits of `in_data`, `PATTERN_NOT_FOUND` (1) if not found
/// (including when `pattern_size > data_size`), or the first applicable error
/// in precedence order (`FOUND_ERR_NULL_PTR`, `FOUND_ERR_EMPTY_DATA`,
/// `FOUND_ERR_EMPTY_PATTERN`, `FOUND_ERR_PATTERN_TOO_LONG`).
/// @par Thread safety
/// Reentrant and thread-safe on separate or shared read-only buffers.
/// @par Blocking
/// Non-blocking; completes in bounded O(N * M) time.
/// @par Requirements
/// BITPAT-REQ-001, BITPAT-REQ-003, BITPAT-REQ-004, BITPAT-REQ-006,
/// BITPAT-REQ-008, BITPAT-REQ-009, BITPAT-REQ-010, BITPAT-REQ-011,
/// BITPAT-REQ-012, BITPAT-REQ-013, BITPAT-REQ-014
[[nodiscard]] enum FoundError bit_pattern_find_naive(const void* in_data,
                                                     size_t data_size,
                                                     const void* pattern,
                                                     size_t pattern_size);

/// @brief Searches for a bit pattern of arbitrary bit length in a bit stream of
/// arbitrary bit length in linear O(N + M) time using the Knuth-Morris-Pratt
/// (KMP) algorithm (Rating Tier 3: O(N) KMP with all corner cases handled).
/// @param in_data Pointer to the packed MSB-first input bit stream buffer.
/// @param data_size Number of valid bits in `in_data` (`>= 1`; trailing bits in
/// the final byte are ignored).
/// @param pattern Pointer to the packed MSB-first target bit pattern buffer.
/// @param pattern_size Number of valid bits in `pattern`
/// (`1 <= pattern_size <= BIT_PATTERN_MAX_PATTERN_BITS`; trailing bits in the
/// final byte are ignored).
/// @return `FOUND_PATTERN` (0) if `pattern` occurs at any bit offset in the
/// first `data_size` bits of `in_data`, `PATTERN_NOT_FOUND` (1) if not found
/// (including when `pattern_size > data_size`), or the first applicable error
/// in precedence order (`FOUND_ERR_NULL_PTR`, `FOUND_ERR_EMPTY_DATA`,
/// `FOUND_ERR_EMPTY_PATTERN`, `FOUND_ERR_PATTERN_TOO_LONG`).
/// @par Thread safety
/// Reentrant and thread-safe on separate or shared read-only buffers.
/// @par Blocking
/// Non-blocking; completes in bounded O(N + M) worst-case time and O(1) stack
/// space.
/// @par Requirements
/// BITPAT-REQ-001, BITPAT-REQ-004, BITPAT-REQ-005, BITPAT-REQ-006,
/// BITPAT-REQ-008, BITPAT-REQ-009, BITPAT-REQ-010, BITPAT-REQ-011,
/// BITPAT-REQ-012, BITPAT-REQ-013, BITPAT-REQ-014
[[nodiscard]] enum FoundError bit_pattern_find_kmp(const void* in_data,
                                                   size_t data_size,
                                                   const void* pattern,
                                                   size_t pattern_size);

/// @brief Official problem entry point: searches for `pattern` of
/// `pattern_size` bits inside `in_data` of `data_size` bits using the O(N + M)
/// KMP algorithm with all corner cases handled.
/// @param in_data Pointer to the packed MSB-first input bit stream buffer.
/// @param data_size Number of valid bits in `in_data`.
/// @param pattern Pointer to the packed MSB-first target bit pattern buffer.
/// @param pattern_size Number of valid bits in `pattern`.
/// @return `FOUND_PATTERN` (0) if found, `PATTERN_NOT_FOUND` (1) if not found,
/// or an error code `> 1` on invalid arguments.
/// @par Thread safety
/// Reentrant and thread-safe on separate or shared read-only buffers.
/// @par Blocking
/// Non-blocking; completes in bounded O(N + M) worst-case time.
/// @par Requirements
/// BITPAT-REQ-001, BITPAT-REQ-004, BITPAT-REQ-005, BITPAT-REQ-006,
/// BITPAT-REQ-008, BITPAT-REQ-009, BITPAT-REQ-010, BITPAT-REQ-011,
/// BITPAT-REQ-012, BITPAT-REQ-013, BITPAT-REQ-014
[[nodiscard]] enum FoundError found(const void* in_data, size_t data_size,
                                    const void* pattern, size_t pattern_size);

#ifdef __cplusplus
}  // extern "C"
#endif
