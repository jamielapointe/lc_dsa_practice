# BitmapFlip — Software Requirements

| Field | Value |
| --- | --- |
| Component | `systems::reverse_bits`, `systems::flip_bitmap`, `systems::flip_bitmap_stride`, `systems::flip_bitmap_roi` (`src/include/systems/bitmap_flip.hpp`) |
| Requirement prefix | `BMFLP` |
| Status | Draft (TDD red; implementation pending) |
| Integrity context | Practice component written with ISO 26262 (ASIL-B-style) rigor; not a certified work product |
| Target platforms | Linux, embedded Linux, QNX, bare-metal MCUs (pure computation, no OS calls); macOS (development host) |

## 1. Problem statement

The user's prompt, verbatim (typos included; lines re-wrapped only).

> **Flip a monochrome bitmap horizontally**
>
> The problem tests first the ability to write a pair of nested loops, then bit manipulation to reverse the order of bits in a byte, and finally
> hardware-aware optimizations, sub-byte alignment, framebuffer row stride, and concurrency/DMA considerations.
>
> The bit-manipulation portion gives a glimpse into a condidate's problem solving skills... reversing the bits in a byte is something a candidate is
> unlikely to have been asked to do in the past, and tehre are several different solutions, so it's useful to see how the candidate thinks this one through.
>
> **Problem Statement**
>
> Given a monochrome bitmap represented as a flat array of bytes with one bit per pixel, flip it horizontally in place.
>
> *Background*
>
> In embedded systems with small monochrome displays, framebuggers are commonly packed at 1 bit per pixel to conserver SRAM. Flipping the display
> horiztonally (mirror mode) requires reversing the horizontal pixel order of every row-in-place.
>
> *Assume*
>
> - Bytes are 8 bits, or `BITS_PER_BYTE` is defined as `8`.
> - Function prototype:
>
> ```c
> // 'width' is the number of columns (pixels)
> // 'row' is the number of rows in the image
> void flip_bitmap(uint8_t* bitmap, size_t width, size_t, height);
> ```
>
> - Within each bytes, **Bit 0 (LSB)** represents the leftmost pixel of that byte, and **Bit 7 (MSB)** represents the rightmost pixel of that byte.
>
> **Initial Problem 0**
>
> Solve the **Problem Statement** using the function prototype assuming that width is a multiple of 8 (e.g., 8, 16, 24, 32, etc.). Write the
> algorithmic complexity (time & space) in the comments.
>
> **Follow-up Question 1**
>
> If the initial problem is not already optimized, then think about how to Improve the performance
>
> **Follow-up Question 2**
>
> Image widths are not multiples of 8 (e.g., not divisible by 8 or `width %8 != 0`)
>
> **Follow-up Question 3**
>
> Hardware framebuffer stride (pitch) and sub-region (ROI) flipping.
>
> in many display controllers and 2d DMA engines, rows are often padded in memory to a specific byte boundary (stride_bytes), or sometimes a library
> needs to only flip a region of interest (ROI) instead of the entire image.
>
> - Row stride: row
>
> **Follow-up Question 4**
>
> **Note** This is a thought experiment, no one is expected to write or test this code.
>
> - Discuss Concurrency - how to safely use multiple threads on a multi-core system to help with this problem
> - Discuss display tearing & DMA cache maintenance
>
> ---
>
> Some test requirements:
>
> - test small bitmaps with known asymetric bit patterns where forgetting to swap bytes or forgetting to reverse bits with a byte immediately fails.
> - double flip involution property: For an easy-to-write randomized test, feed in an image of random bytes:
>   - flip once and very the image changed using a non-palindromic input to ensure the function is not a no-op.
>   - flip a second tyime and it restores the original image (`flip(flip(image)) == image`).
> - Boundary / edge cases:
>   - even vs odd number of bytes per row
>   - single byte width and single row heights
> - Some larger images
>
> For some of the follow up questions, test against the above, but also add:
>
> - unaligned widths less than 1 byte (width < 8) unaligned multi-byte widths (width / 8 > 0 && width % 8 != 0), including non-zero garbage in the
>   trailing padding bits - to ensure padding is handled correctly.
> - invalid inputs are caught (e.g., bitmap == NULL, width == 0, height == 0, etc.)

## 2. Context and intent

Small monochrome LCD, OLED, and e-paper panels (instrument-cluster tell-tales, robot status displays, handheld tools) keep a 1-bit-per-pixel framebuffer
in SRAM. Mounting the panel the other way round, or a head-up "mirror mode", requires flipping every row in place: there is no room for a second frame.
The routine runs on the display or render task, often on a small core, so it must be allocation-free, bounded in time, and safe against bad geometry
coming from configuration data. It is staged like the interview question: a correct nested loop first, then speed, then real framebuffer layouts.

## 3. Assumptions and decisions

| ID | Assumption / decision | Alternatives considered |
| --- | --- | --- |
| A-1 | Primary API is C++23: `std::span<std::uint8_t>` buffer, `std::expected<void, BitmapFlipError>` result, `noexcept`. The prompt's C prototype is kept as an overload `void flip_bitmap(std::uint8_t*, std::size_t, std::size_t)` (the prompt's stray comma in `size_t, height` is treated as a typo) | C prototype only (rejected: a raw pointer cannot be bounds-checked and `void` cannot report errors) |
| A-2 | One function per stage: `flip_bitmap` (Problem 0 / Follow-ups 1-2), `flip_bitmap_stride` and `flip_bitmap_roi` (Follow-up 3) | One function with optional stride / ROI parameters |
| A-3 | **Full-row flips write the padding bits** (bits past `width` in each row's last pixel byte) **as zero**; their input value is ignored (confirmed by the user) | Preserve the input padding bits; leave them unspecified |
| A-4 | ROI flips change only ROI pixels and preserve every other bit, including padding bits and stride padding bytes | Zero padding bits as in A-3 (rejected: they are outside the ROI) |
| A-5 | Bit 0 (LSB) is the leftmost pixel of a byte, as the prompt states; row `y` starts at byte `y * stride_bytes`; pixel `x` is bit `x % 8` of byte `x / 8` | MSB-first (rejected by the prompt) |
| A-6 | Row stride (confirmed by the user; the prompt's bullet "Row stride: row" was truncated): row `y` starts at `bitmap + y * stride_bytes`; its first `active_width_bytes = (width + 7) / 8` bytes hold active pixels, `stride_bytes >= active_width_bytes`, and bytes `[active_width_bytes, stride_bytes - 1]` are never swapped into the active image (and never written) | Stride in pixels or words |
| A-7 | Requirement (confirmed by the user): the buffer must hold `stride_bytes * height` bytes (the last row includes its stride padding) | `stride_bytes * (height - 1) + ceil(width / 8)` (tighter; rejected for simplicity) |
| A-8 | ROI coordinates are in pixels; ROI edges may fall anywhere inside a byte | Byte-aligned ROIs only (rejected: that would skip the interesting part of Follow-up 3) |
| A-9 | An empty span (null or non-null data) is `kNullPointer`. Validation order: `kNullPointer`, `kZeroDimension`, `kInvalidStride`, `kInvalidRoi`, `kBufferTooSmall`; nothing is written on error | Unspecified order (rejected: tests could not pin the error for inputs with several problems) |
| A-10 | The C-style overload cannot report errors, so invalid input (null pointer, zero width or height) is a silent no-op | Assert / abort (rejected: no aborts in runtime paths) |
| A-11 | `reverse_bits(std::uint8_t)` is public API so the byte-reversal step, called out in the prompt, can be tested on its own | Keep it private (rejected: its tests would be indirect) |
| A-12 | The caller owns the buffer; there is no compile-time capacity. Any `width` / `height` up to `SIZE_MAX` is validated without overflow | Template on image size (rejected: framebuffer geometry is usually run-time configuration) |
| A-13 | The library starts no threads. Follow-up 4 is a written discussion (BMFLP-REQ-014); only reentrancy is tested (BMFLP-REQ-013) | Built-in parallel flip (rejected: the prompt says nobody writes or tests it) |
| A-14 | Follow-up 1 speed is verified by analysis, not by a timing test (wall-clock assertions are unreliable under sanitizers) | A benchmark with a time bound |

## 4. Glossary

| Term | Meaning |
| --- | --- |
| Pixel byte | One of the `ceil(width / 8)` bytes of a row that hold pixels |
| Padding bits | When `width % 8 != 0`: the top `8 - width % 8` bits of a row's last pixel byte; not pixels |
| Stride (pitch) | Distance in bytes between the starts of consecutive rows; `>= ceil(width / 8)` |
| Stride padding bytes | Bytes `[ceil(width / 8), stride_bytes)` of each row; owned by the display hardware |
| ROI | Region of interest: rectangle `x, y, width, height` in pixels inside the framebuffer |
| Involution | An operation that is its own inverse: `flip(flip(image)) == image` |
| Tearing | The display scanning out a frame that is half old, half new |

## 5. Interface overview

The header is authoritative for signatures.

- `kBitsPerByte` — `8`.
- `BitmapFlipError` — `kNotImplemented` (stub only), `kNullPointer`, `kZeroDimension`, `kBufferTooSmall`, `kInvalidStride`, `kInvalidRoi`; `to_string()`.
- `BitmapRoi` — aggregate `{x, y, width, height}` in pixels.
- `reverse_bits(std::uint8_t) -> std::uint8_t` — mirror one byte.
- `flip_bitmap(std::span<std::uint8_t>, width, height) -> std::expected<void, BitmapFlipError>` — packed rows.
- `flip_bitmap_stride(std::span<std::uint8_t>, width, height, stride_bytes) -> std::expected<void, BitmapFlipError>` — strided rows.
- `flip_bitmap_roi(std::span<std::uint8_t>, framebuffer_width, framebuffer_height, stride_bytes, BitmapRoi) -> std::expected<void, BitmapFlipError>`.
- `flip_bitmap(std::uint8_t*, width, height) -> void` — the prompt's C-style prototype.

## 6. Functional requirements

Grouped by exercise stage: Problem 0 and Follow-up 1 (REQ-001 to REQ-007), Follow-up 2 (REQ-008, REQ-009), Follow-up 3 (REQ-010, REQ-011), and the
C-style entry point (REQ-012).

### BMFLP-REQ-001: Pixel layout and flip definition

The component shall treat bit 0 (LSB) of each byte as the leftmost pixel of that byte and bit 7 as the rightmost, and after a flip pixel `x` of every row
shall hold the value pixel `width - 1 - x` of the same row held before.

- **Verification:** Test

### BMFLP-REQ-002: Byte bit reversal

`reverse_bits(v)` shall return `v` with bit `i` moved to bit `7 - i`, for all 256 values.

- **Verification:** Test

### BMFLP-REQ-003: Aligned rows with even and odd byte counts

When `width % 8 == 0`, `flip_bitmap` shall reverse both the order of the row's bytes and the bit order inside every byte, including the middle byte of a row
with an odd byte count.

- **Verification:** Test
- **Rationale:** Swapping bytes without reversing their bits (or the reverse) mirrors 8-pixel blocks instead of pixels: a silently wrong display.

### BMFLP-REQ-004: Every row, in place, row order kept

`flip_bitmap` shall flip all `height` rows in place and shall not reorder rows.

- **Verification:** Test

### BMFLP-REQ-005: Double-flip involution

For any valid input, flipping twice shall restore the original pixels (and, for aligned widths and ROI flips, the original bytes exactly). Flipping a
non-palindromic image once shall change it.

- **Verification:** Test

### BMFLP-REQ-006: Documented complexity

The implementation shall state its time and space complexity in a comment in `bitmap_flip.cpp`, for Problem 0 and again after Follow-up 1. Time shall be
`O(width * height)`; auxiliary space shall be `O(1)`.

- **Verification:** Review (comment present and consistent with the code)

### BMFLP-REQ-007: Follow-up 1, optimized inner loop

The optimized implementation shall avoid per-pixel work in the inner loop for aligned rows, and the comment in `bitmap_flip.cpp` shall explain what was
optimized and why it is faster on the intended target. The API and all tests shall be unchanged.

- **Verification:** Analysis (code review of the inner loop; optional local benchmark)

### BMFLP-REQ-008: Unaligned widths

When `width % 8 != 0` (including `width < 8`), `flip_bitmap` shall mirror the `width` pixels of each row as defined in BMFLP-REQ-001: the mirrored pixels
start at bit 0 of the row's first byte.

- **Verification:** Test
- **Rationale:** Mirroring whole bytes without re-aligning shifts the image by `8 - width % 8` pixels.

### BMFLP-REQ-009: Padding bits

The input value of padding bits shall not affect any pixel, and full-row flips (`flip_bitmap`, `flip_bitmap_stride`) shall write every padding bit as zero
(assumption A-3).

- **Verification:** Test
- **Rationale:** Garbage in padding bits leaking into visible pixels is the classic Follow-up 2 bug; zeroed padding keeps output deterministic.

### BMFLP-REQ-010: Row stride

`flip_bitmap_stride` shall flip the `ceil(width / 8)` pixel bytes of each row `y`, starting at byte `y * stride_bytes`, with the same result as
`flip_bitmap` on each row, and shall never write stride padding bytes or bytes past row `height - 1`.

- **Verification:** Test
- **Rationale:** Stride padding may belong to the display controller or DMA engine; writing it corrupts hardware-owned memory.

### BMFLP-REQ-011: Region of interest

`flip_bitmap_roi` shall, for every row `y` in `[roi.y, roi.y + roi.height)`, set pixel `x` in `[roi.x, roi.x + roi.width)` to the previous value of pixel
`2 * roi.x + roi.width - 1 - x`, and shall preserve every other bit of the buffer, including pixels that share a byte with the ROI edges.

- **Verification:** Test
- **Rationale:** Clobbering neighbour pixels at a partial-byte ROI edge corrupts UI elements next to the region.

### BMFLP-REQ-012: C prototype wrapper

`void flip_bitmap(std::uint8_t* bitmap, std::size_t width, std::size_t height)` shall behave as `flip_bitmap` on a span of `ceil(width / 8) * height`
bytes, and shall do nothing when `bitmap` is null or `width` or `height` is zero.

- **Verification:** Test

## 7. Concurrency requirements and Follow-up 4

### BMFLP-REQ-013: Reentrancy

All functions shall be reentrant: no mutable static state (in particular no lookup table filled in lazily at run time). Concurrent calls on different
buffers, on disjoint row ranges of one buffer, or with ROIs covering disjoint row ranges shall be free of data races and give the same result as sequential
calls.

- **Verification:** Test (`BitmapFlipConcurrencyTest`, run under TSan)
- **Rationale:** A lazily initialized table is a data race the first time two display tasks flip at once.

### BMFLP-REQ-014: Follow-up 4 discussion

The user shall answer the following in a comment in `bitmap_flip.cpp` (no code or tests required):

1. How would you split one flip across several cores? What is the unit of work, and what must two workers never share (think bytes, cache lines, ROI edges)?
2. When does adding threads stop paying off for a framebuffer of a few kilobytes?
3. The display controller scans the framebuffer out continuously. What does a viewer see if you flip in place mid-scan, and which display-side mechanisms
   avoid it?
4. The framebuffer is read by a DMA engine and written by a CPU with a data cache. Which cache maintenance operations are needed, in which order relative to
   starting the DMA, and what goes wrong without them? What changes if the buffer is in non-cacheable memory?

- **Verification:** Review

## 8. Resource and memory requirements

### BMFLP-REQ-015: No heap allocation

No function shall allocate heap memory.

- **Verification:** Test (`systems_test::AllocationScope`)
- **Rationale:** Steady-state allocation risks fragmentation, unbounded latency, and memory exhaustion (ISO 26262-6:2018 Table 6 item 1b; MISRA C++:2023
  Rule 21.6.1).

### BMFLP-REQ-016: Bounded stack, in place

The implementation shall work in the caller's buffer with `O(1)` auxiliary storage (scalars, or a fixed constant table), without recursion, VLAs, or a
row-sized scratch buffer.

- **Verification:** Review
- **Rationale:** A row buffer on the stack scales with the panel width and can overflow a small task stack.

## 9. Robustness and failure-model requirements

### BMFLP-REQ-017: Null or empty buffer

The span-based functions shall return `kNullPointer` when the span is empty or its data pointer is null.

- **Verification:** Test

### BMFLP-REQ-018: Zero dimensions

The span-based functions shall return `kZeroDimension` when `width` or `height` is zero, and `flip_bitmap_roi` also when `roi.width` or `roi.height` is zero.

- **Verification:** Test

### BMFLP-REQ-019: Stride too small

`flip_bitmap_stride` and `flip_bitmap_roi` shall return `kInvalidStride` when `stride_bytes < ceil(width / 8)` (framebuffer width for the ROI variant).

- **Verification:** Test
- **Rationale:** A short stride makes rows overlap, so flipping one row corrupts the next.

### BMFLP-REQ-020: ROI outside the framebuffer

`flip_bitmap_roi` shall return `kInvalidRoi` unless `roi.x + roi.width <= framebuffer_width` and `roi.y + roi.height <= framebuffer_height`, evaluated
without `std::size_t` overflow.

- **Verification:** Test
- **Rationale:** A wrapped-around bounds check lets an out-of-range ROI write outside the framebuffer.

### BMFLP-REQ-021: Buffer too small

The span-based functions shall return `kBufferTooSmall` when the span holds fewer than `stride_bytes * height` bytes (`ceil(width / 8) * height` for
`flip_bitmap`), with every size computation (including `ceil(width / 8)`) evaluated without `std::size_t` overflow.

- **Verification:** Test
- **Rationale:** A product that wraps to a small value passes the check and the flip then writes past the buffer.

### BMFLP-REQ-022: Validation order, untouched on error

Validation shall complete before any byte is written, so the buffer is unchanged on error. When several checks fail, the first in this order shall be
reported: `kNullPointer`, `kZeroDimension`, `kInvalidStride`, `kInvalidRoi`, `kBufferTooSmall`.

- **Verification:** Test

## 10. Portability requirements

### BMFLP-REQ-023: Portable computation

The component shall be plain C++23 with fixed-width integers: no OS headers or calls, no compiler intrinsics required (`<bit>` is allowed), no type punning
(`std::bit_cast` or byte access instead of `reinterpret_cast` to wider types), and no assumption about host endianness for word-at-a-time code.

- **Verification:** Review
- **Rationale:** The same routine must build for Cortex-M, RISC-V, AArch64, and x86-64 hosts.

## 11. Failure modes (mini-FMEA)

| Failure mode | Effect | Detection / mitigation | Requirement |
| --- | --- | --- | --- |
| Bytes swapped but bits not reversed (or the reverse) | Display mirrored in 8-pixel blocks | Asymmetric known-answer tests | BMFLP-REQ-003 |
| Unaligned row not re-aligned | Image shifted by `8 - width % 8` pixels | Unaligned known-answer and randomized tests | BMFLP-REQ-008 |
| Padding garbage shifted into pixels | Random dots at the left edge | Garbage-padding tests | BMFLP-REQ-009 |
| Stride padding or ROI neighbours overwritten | Corrupt hardware-owned bytes or UI | Canary bytes, bit-exact "unchanged" oracle | BMFLP-REQ-010, BMFLP-REQ-011 |
| Size or bounds product wraps | Out-of-bounds write | Overflow-safe checks, `SIZE_MAX` tests | BMFLP-REQ-020, BMFLP-REQ-021 |
| Lazy table initialized from two tasks | Data race, wrong pixels | Constant tables only; TSan test | BMFLP-REQ-013 |
| Flip during scan-out | Tearing | Double buffering / vsync (discussion) | BMFLP-REQ-014 |
| Dirty cache lines not cleaned before DMA | Display shows stale pixels | Cache clean before DMA (discussion) | BMFLP-REQ-014 |

## 12. Standards guidance (informative)

| Topic | Reference | How it applies here |
| --- | --- | --- |
| Dynamic memory | ISO 26262-6:2018 Table 6 (1b); MISRA C++:2023 Rule 21.6.1; AUTOSAR C++14 A18-5-1 | No heap at all |
| Recursion, bounded stack | ISO 26262-6:2018 Table 6 (1e) | Loops only; no row-sized stack buffer |
| Defensive implementation | ISO 26262-6:2018 Table 1 (1d) | Every geometry parameter validated, overflow-safe |
| Exceptions | Project policy (see `safety.md`) | `noexcept` API, `std::expected` errors |
| Casts and type punning | MISRA C++:2023 Rule 8.2.2; AUTOSAR C++14 A5-2-4 | `static_cast` only; `std::bit_cast` for word access |

Rule numbers are informative pointers; confirm them against licensed copies of the standards before relying on them.

## 13. Out of scope

- Vertical flip, rotation, and other pixel formats (2 bpp, 8 bpp, MSB-first).
- Display driver, DMA, and cache-maintenance code (Follow-up 4 is discussion only).
- A multi-threaded flip inside the library.

## 14. Implementation hints (optional, spoiler-free)

- Write the per-pixel nested loop first and keep it around as a mental reference; then make it faster.
- For Follow-up 1, compare the cost of the byte reversal itself with the cost of the loop around it; then think about how many pixels one load and one store
  can move on your target.
- For Follow-up 2, it helps to treat a row as one long bit string and ask where its first pixel lands after you mirror whole bytes.
- For Follow-up 3, partial bytes at the ROI edges need a read-modify-write with a mask.
- Check integer promotions: `~byte` and `byte << n` are `int`, which `-Wconversion` and UBSan will point out.
