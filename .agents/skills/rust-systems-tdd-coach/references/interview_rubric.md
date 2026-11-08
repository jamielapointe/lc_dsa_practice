# Green/Refactor Interview Critique Rubric

An interview-grade evaluation rubric and technical critique schema calibrated for senior systems architects, tech leads, and principal engineers preparing for
Rust systems interviews.

---

## 1. Critique Verification Sequence

When the learner announces that their implementation is complete and passes tests, the coach executes the three-stage Green Critique verification sequence:

```bash
# Stage 1: Confirm functional correctness across all 4 test tiers
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml

# Stage 2: Enforce strict static analysis with ZERO warnings
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --all-targets -- -D warnings

# Stage 3: Undefined behavior and memory sanitization audit
# Option A (Pure Rust targets with Miri installed):
pixi run cargo miri test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml

# Option B (Host environments without Miri or targets with C++ FFI):
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml -- --nocapture
```

### 1.1 Miri & Sanitizer Execution Guidelines

- **When to run Miri (`cargo miri test`)**:
  - Pure-Rust modules containing `unsafe` pointer manipulation, transmutes, manual drops, or custom uninitialized memory buffers (`MaybeUninit<T>`).
  - Validates code against the **Stacked Borrows** and **Tree Borrows** aliasing models, detecting provenance violations, unaligned memory accesses, and data
    races in concurrent execution.
- **Handling Conda-Forge / Host Toolchain Without Miri**:
  - In environments where `cargo-miri` is not bundled (e.g. conda-forge `rustc 1.99.1`), the coach notes the toolchain limitation and substitutes static
    auditing of `// SAFETY:` comments alongside AddressSanitizer (ASan) recommendations.
- **FFI Limitation**:
  - Miri interprets Rust MIR and cannot execute compiled C/C++ machine code.
  - For modules involving C++ (`10_sensor_fusion_bridge`), Miri will abort on foreign symbol calls. Cross-language verification must use Clang's
    AddressSanitizer (`-fsanitize=address`) and UndefinedBehaviorSanitizer (`-fsanitize=undefined`) built via CMake or `cxx_build`.

---

## 2. The 4-Pillar Evaluation Rubric

Candidates are evaluated across four systems engineering dimensions:

```text
+---------------------------------------------------------------------------------------------------------+
|                                    4-PILLAR SYSTEMS EVALUATION RUBRIC                                    |
+---------------------------------------------------------------------------------------------------------+
| Pillar 1: Idiomatic Rust & 2024 Standards | Zero clones, iterator pipelines, let-else, use<..> syntax.   |
| Pillar 2: Zero-Cost Layout & Abstraction  | Zero steady-state heap, 64B cache alignment, Acquire/Release.|
| Pillar 3: Memory Safety & Miri Provenance | Strict // SAFETY: proofs, valid provenance, no UB under Miri.|
| Pillar 4: Algorithmic Complexity & Systems| O(1) space, bounded WCET, Send/Sync invariants, panic abort.  |
+---------------------------------------------------------------------------------------------------------+
```

### Pillar 1: Idiomatic Rust & Edition 2024 Standards

Senior systems interviewers expect modern, expressive, zero-overhead idiomatic code that leverages the type system:

1. **Elimination of Defensive Copies**:
   - Total absence of redundant `.clone()` calls or unnecessary intermediate `to_vec()` allocations.
   - Borrowing slices (`&[u8]`, `&str`) directly from arena inputs rather than copying into owned types (`Vec<u8>`, `String`).
2. **Functional Iterator Combinators**:
   - Replacing explicit index loops (`for i in 0..len`) with zero-cost iterator pipelines (`.iter()`, `.chunks_exact()`, `.filter()`, `.map()`, `.fold()`).
   - Using `.chunks_exact()` instead of `.chunks()` when step size divides evenly, allowing the compiler to auto-vectorize and omit the remainder loop.
3. **Idiomatic Control Flow**:
   - Using `let ... else` pattern matching for early-exit error handling and precondition guards.
   - Using the `?` operator instead of verbose, nested `match` blocks.
4. **Rust Edition 2024 Standards**:
   - Correct application of `use<..>` lifetime capture syntax on return-position `impl Trait`.
   - `unsafe extern "C"` blocks and `#[unsafe(no_mangle)]` attribute wrappers.
   - Replacement of legacy lazy-initialization crates with `std::sync::LazyLock`.

### Pillar 2: Zero-Cost Abstractions & Memory Layout

Low-level systems code must be mechanically sympathetic to hardware caches and CPU execution units:

1. **Zero Steady-State Heap Allocation**:
   - Zero dynamic allocations (`malloc`, `Box::new`, `Vec::push`) on steady-state control or data paths. Heap memory is permitted only during initialization.
   - Inline buffer storage using const generics (e.g. `[u8; CAPACITY]`).
2. **Cache-Line Alignment & False-Sharing Prevention**:
   - In concurrent structures (e.g. ring buffers, queues), producer and consumer indices must be isolated on separate 64-byte cache lines using
     `#[repr(align(64))]`.
   - Failure to align concurrent atomics causes **cache-line bouncing** under high core contention (MESI cache protocol invalidation storms), severely
     degrading multi-core throughput.
3. **Precision in Atomic Memory Ordering**:
   - Strictly avoiding sequential consistency (`Ordering::SeqCst`) where weaker orderings suffice.
   - Proper pairing: Producer publishes payload with `Ordering::Release`; consumer observes index with `Ordering::Acquire`.
   - Independent counters or flag resets using `Ordering::Relaxed`.
4. **Bounds-Check Elimination**:
   - Structuring slice indexing so the compiler backend (LLVM) can prove length invariants upfront and eliminate runtime panic branches in hot loops.

### Pillar 3: Memory Safety, Aliasing & Miri Validation

Writing `unsafe` in Rust is a serious architectural responsibility. The standard for `unsafe` in senior systems interviews is uncompromising:

1. **Mandatory `// SAFETY:` Rationale**:
   - Every single `unsafe` block must be immediately preceded by a `// SAFETY:` doc comment.
   - The rationale must explicitly prove:
     1. Non-null and alignment guarantees of all dereferenced pointers.
     2. Valid allocation bounds (no out-of-bounds reads/writes).
     3. Aliasing invariance: No `&mut` reference coexists with any other reference targeting the same memory.
     4. Correct initialization: Memory read via `read()` or `assume_init()` has been fully initialized.
2. **Pointer Provenance & Stacked Borrows**:
   - Pointers derived from references must preserve provenance.
   - Using raw address-of operators (`&raw const`, `&raw mut`) when referencing unaligned or uninitialized struct fields to prevent illegal intermediate
     reference creation.
3. **Miri Cleanliness**:
   - Zero undefined behavior detected under Miri execution.
   - Zero memory leaks: All allocated heap nodes, custom smart pointers, or mapped pages must be deterministically reclaimed via RAII `Drop`.

### Pillar 4: Algorithmic Complexity, Concurrency & Systems Architecture

1. **Computational & Space Complexity**:
   - Steady-state time complexity must match theoretical optimal bounds (e.g. $O(1)$ push/pop for circular queues).
   - Worst-Case Execution Time (WCET): Embedded and real-time paths must have deterministic, bounded execution times without unbounded loops.
2. **Thread Safety & Auto Trait Bounds**:
   - Explicit verification of `Send` and `Sync` auto trait semantics.
   - Data structures shared across threads must enforce `T: Send` for transmission and `T: Sync` for concurrent reference access.
3. **Failure Isolation & Panic Safety**:
   - In systems tracks, unwinding is prohibited (`panic = "abort"`).
   - Functions must maintain valid invariant states even if an error is returned.

---

## 3. Structured Review Feedback Schema

The coach delivers critique feedback using a standardized four-part schema:

```markdown
### 1. Architectural Strengths
[Highlight 2-3 exceptional low-level systems choices: e.g. cache-line padding, zero-copy borrowing, clean static dispatch.]

### 2. Critical Hazards & Invariant Violations
[Identify any undefined behavior, missing // SAFETY: proofs, hidden heap allocations, or false sharing.]

### 3. Interview Trade-offs & Deep Dive
[Discuss alternative design decisions: e.g. intrusive vs arena allocation, Acquire/Release vs SeqCst, CAS vs Mutex, benchmark expectations.]

### 4. Refactoring Diffs
[Provide minimal, concrete code improvements demonstrating idiomatic Edition 2024 patterns.]
```

---

## 4. Concrete Example Critique: SPSC Ring Buffer

To calibrate the coach's review tone and technical depth, consider this reference critique of an SPSC lock-free ring buffer submission:

````markdown
### 1. Architectural Strengths
- **Cache Isolation**: Excellent use of `#[repr(align(64))]` on `head` and `tail` atomic wrappers. This completely eliminates false-sharing invalidations
  across L1 cache lines on ARM64 and x86_64 cores.
- **Zero-Allocation Design**: By parameterizing the ring buffer with const generics (`const CAP: usize`), all storage lives inline without invoking the
  heap allocator during steady-state operations.

### 2. Critical Hazards & Invariant Violations
- **Overly Conservative Ordering**: In `push()`, you used `Ordering::SeqCst` when updating the tail index. For an SPSC queue, `Ordering::Release` is
  sufficient because only the producer modifies `tail`, and the consumer only needs to acquire memory published prior to that update. `SeqCst` emits
  unnecessary full memory fence instructions (`dmb ish` on ARM64, `mfence` or locked instructions on x86).
- **Missing `// SAFETY:` Comment**: At line 42, `(*slot).assume_init_read()` is invoked inside an `unsafe` block without documenting why the slot is
  guaranteed to be initialized.

### 3. Interview Trade-offs & Deep Dive
- **Power-of-Two Capacity**: Your implementation uses modulo division (`index % CAP`). In a performance-critical kernel driver, you should constrain `CAP`
  to a power of two using a `static_assert` or const assertion, replacing the expensive integer division instruction with a bitwise mask (`index & (CAP - 1)`).
  This saves 10-15 CPU cycles per push/pop operation.

### 4. Refactoring Diffs

```diff
- self.tail.store(next_tail, Ordering::SeqCst);
+ // SAFETY: Enforced by single-producer constraint; Release publishes written buffer item.
+ self.tail.store(next_tail, Ordering::Release);
```
````
