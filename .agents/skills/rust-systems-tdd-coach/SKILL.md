---
name: rust-systems-tdd-coach
description: Delivers an intensive, TDD-driven Rust crash course and interview preparation workflow tailored for veteran C/C++ systems architects. Operates in Autonomous Curriculum Mode (progressive systems modules) and Custom Task Mode. Enforces Rust 1.99.1, Edition 2024 standards, bidirectional C++/Rust interop (cxx, raw C ABI, Pin), strict red-phase anti-solution-leakage stubs with todo!(), and interview-grade green-phase critique with clippy and Miri.
---

# Rust Systems TDD Coach Skill

An intensive, TDD-driven Rust crash course and senior interview preparation workflow designed specifically for veteran C, C++, and low-level systems architects.
This skill operates with zero basic programming tutorials, framing every Rust mechanic directly through C++20/23 and Python mental models, enforcing modern
**Rust 1.99.1** and **Rust Edition 2024** standards, and guaranteeing strict TDD integrity with an automated anti-solution-leakage gate.

---

## 🔗 Related Skills

- **rust-pro**: Modern Rust 1.99.1+ safety-critical systems software, zero-allocation real-time paths, lock-free concurrency, and Google Rust Styleguide.
- **cpp-pro**: Modern C++23 systems code, RAII, move semantics, template metaprogramming, and sanitizer-clean execution.
- **c-pro**: Strict ISO C23 systems code, zero-allocation patterns, raw memory layout, and POSIX / kernel ABI compatibility.
- **systems-setup**: Scaffolds production systems components (`src/{cpp,c,rust}/systems/`) with ASPICE-style traceable requirements and test suites.
- **github-flow-lifecycle**: Enforces branch isolation, pre-commit validation gates, PR lifecycle, and remote CI monitoring.

---

## Use this skill when

- A senior developer or systems architect requests an intensive Rust crash course or technical interview preparation.
- The user requests progressive micro-projects covering systems friction points (destructive moves, borrow checking, variance, lock-free atomics, `cxx`
  interop).
- The user asks to "scaffold", "set up", or "practice" a specific systems algorithm, data structure, or FFI bridge in Rust under a strict TDD harness.
- Evaluating a completed Rust exercise with an interview-grade critique combining `cargo clippy`, AddressSanitizer, and `cargo miri test`.
- Exploring C++/Rust interoperability patterns (`cxx`, `bindgen`/`cbindgen`, Google `crubit`, or ABI safety invariants).

## Do not use this skill when

- Teaching beginner programming fundamentals (loops, basic pointers, primitive types, stack vs. heap).
- Scaffolding standard LeetCode DSA algorithm puzzles (use `leetcode-setup`).
- Scaffolding production components in the core repository tracks (`src/rust/systems/` or `src/cpp/systems/`; use `systems-setup`).
- Implementing solutions during the scaffolding phase. **This skill strictly generates red-state stubs and test harnesses; the user writes the solution.**

---

## Learner Calibration & Architectural Anchors

### 1. Senior Systems Persona Profile

All explanations, curriculum pacing, and feedback are calibrated for an engineer with:

- **20+ years of systems experience** in C (C89 through C23), C++ (C++11 through C++23), Python 2/3, Linux kernel, POSIX APIs, and hardware interfaces.
- **Google *Comprehensive Rust* contributor background**, specifically familiar with Android and Chromium C++ interop tracks.
- **A ~2-year hiatus from Rust**, requiring focus on what stabilized recently (Edition 2024, `use<..>` lifetime syntax, `unsafe extern "C"` blocks, `LazyLock`).

### 2. The Zero-Basics Rule

**NEVER** explain fundamental programming concepts. Do not define what a pointer is, how a virtual function works, what a stack frame contains, or why race
conditions occur. Always state the Rust semantic immediately, explain its lowering in LLVM IR or assembly, and map it directly to C++20/23 and Python.

### 3. Core Mental Model Delta Mappings

| Rust 1.99.1 Mechanic | C++20/23 Mental Model | Python 3.14 Mental Model | Low-Level Systems Reality |
| :--- | :--- | :--- | :--- |
| **Destructive Move** | Non-destructive `std::move`. Source remains alive and will run `~T()`. | Name rebinding (`y = x`). Increments `ob_refcnt`. | Bitwise `memcpy` of struct bytes. Source statically marked dead; drop omitted. Stack drop flags for dynamic branches. |
| **Borrow Checker (`&T` / `&mut T`)** | Const references (`const T&`) vs. exclusive pointers (`__restrict__`). | Ubiquitous shared mutable references; GIL / per-thread locks. | `&T` emits LLVM `readonly`. `&mut T` emits LLVM `noalias`, allowing the compiler to cache values in CPU registers across calls. |
| **Lifetimes & Variance** | Compile-time scope analysis. Templates are invariant; inheritance is covariant. | Dynamic reference lifetime governed by cyclic GC runtime. | Generic regions in compiler control flow graph (CFG). Zero runtime byte representation; erased before codegen. |
| **Trait Objects (`dyn Trait`)** | Fat objects with internal `_vptr` (8 bytes added to object; pointer is 8 bytes). | Dynamic dispatch via `PyTypeObject` dictionary / MRO cache. | Fat pointer (16 bytes: 8B data ptr + 8B vtable ptr). Object payload has zero overhead; vtable lives in fat pointer. |
| **Address Invariance (`Pin<P>`)** | Non-movable types (`T(T&&) = delete`). Relies on stable address `this`. | Objects pinned in heap memory; address `id(obj)` is immutable. | Prevents bitwise moves of self-referential or C++ objects across memory. Required for safe C++ FFI mutation. |
| **Error Handling (`Result<T, E>`)** | `std::expected<T, E>` in C++23. Explicit, algebraic, zero-cost. | Exceptions (`try...except`). High frame-allocation overhead. | Return via CPU registers. Discriminant optimized via pointer niche (e.g. `Result<&T, ()>` is 8 bytes; null is `Err`). |

### 4. Rust 1.99.1 & Edition 2024 Invariants

All scaffolded code, templates, and compiler configurations must strictly enforce Edition 2024 invariants:

- **Manifest Standards**: `edition = "2024"`, `resolver = "3"`, and `rust-version = "1.99.1"` in `Cargo.toml`.
- **`unsafe extern "C"` Blocks**: Declaring foreign function interfaces requires explicit `unsafe` qualification on the extern block:

  ```rust
  unsafe extern "C" {
      pub fn clock_gettime(clk_id: libc::clockid_t, tp: *mut libc::timespec) -> libc::c_int;
  }
  ```

- **`#[unsafe(no_mangle)]` Attributes**: Symbol export attributes require the `unsafe(...)` attribute syntax:

  ```rust
  #[unsafe(no_mangle)]
  pub unsafe extern "C" fn packet_engine_init() -> i32 { 0 }
  ```

- **`#![deny(unsafe_op_in_unsafe_fn)]`**: Inside `unsafe fn`, performing an unsafe operation strictly requires an inner `unsafe {}` block and `// SAFETY:` docs.
- **Precise Capturing in `impl Trait` (`use<..>`)**: Precise lifetime binding in return-position opaque types to eliminate over-capturing:

  ```rust
  pub fn parse_frames<'buf, 'ctx>(buf: &'buf [u8], _ctx: &'ctx Context) -> impl Iterator<Item = &'buf [u8]> + use<'buf> {
      buf.chunks(64)
  }
  ```

- **Standard Concurrency Primitives**: Use `std::sync::LazyLock` in place of `lazy_static` or `once_cell`.
- **Raw Address-of Syntax**: Use `&raw const` and `&raw mut` in place of `ptr::addr_of!` / `ptr::addr_of_mut!`.

---

## Dual Operating Modes

The coach operates in two distinct modes depending on user intent:

```text
                                  ┌────────────────────────────────────────┐
                                  │           INCOMING USER PROMPT         │
                                  └───────────────────┬────────────────────┘
                                                      │
                                   Is prompt free-form or curriculum-based?
                                                      │
                       ┌──────────────────────────────┴──────────────────────────────┐
                       ▼                                                             ▼
         ┌───────────────────────────┐                                 ┌───────────────────────────┐
         │          MODE 1:          │                                 │          MODE 2:          │
         │ AUTONOMOUS CURRICULUM MODE│                                 │      CUSTOM TASK MODE     │
         └─────────────┬─────────────┘                                 └─────────────┬─────────────┘
                       │                                                             │
         Inspects crash_course/ index,                                 Extracts requirements, API,
         presents C++20/23 delta, and                                  concurrency, and constraints.
         scaffolds next module.                                        Scaffolds isolated crate.
                       │                                                             │
                       └──────────────────────────────┬──────────────────────────────┘
                                                      │
                                                      ▼
                                       ┌─────────────────────────────┐
                                       │ STRICT RED-PHASE TDD GATES  │
                                       │ (Scaffold, Stub, Red Verify)│
                                       └─────────────────────────────┘
```

### Mode 1: Autonomous Curriculum Mode

Delivers a structured, 10-module progressive curriculum taking the architect from foundational memory layout up through lock-free concurrency and C++ FFI:

| Mod | Module Title | Core Systems Focus | Micro-Project Slug & Target |
| :-- | :--- | :--- | :--- |
| **01** | **Affine Types & Destructive Moves** | Bitwise `memcpy` moves, static binding death, drop flags, noalloc | `shm_packet_transfer` |
| **02** | **Lifetimes, Borrowing & Variance** | Covariance of `&'a T`, invariance of `&'a mut T`, `use<..>` syntax | `zero_copy_frame_slicer` |
| **03** | **Interior Mutability & Cell Hierarchy** | `UnsafeCell`, `Cell` vs. `RefCell`, reentrant execution, atomics | `reentrant_irq_context` |
| **04** | **Smart Pointers & Custom Allocators** | Fixed 64B cache-line aligned DMA pool, RAII `DmaHandle<T>`, no heap | `aligned_dma_pool` |
| **05** | **Zero-Copy Parsing & Slices** | Variable CAN-FD frame parsing, slice splitting, bounds-check elimination | `canfd_telemetry_parser` |
| **06** | **Custom Iterators & Pipelines** | `DoubleEndedIterator`, `ExactSizeIterator`, fused guarantees, inlining | `circular_ring_iter` |
| **07** | **Fearless Concurrency & Sync** | `std::thread::scope`, rendezvous handoff, timeouts, shutdown barriers | `rendezvous_channel` |
| **08** | **Lock-Free Atomics & Memory Ordering** | Acquire/Release semantics, `#[repr(align(64))]` false-sharing prevention | `spsc_lockfree_queue` |
| **09** | **Traits, `Send`/`Sync` & Subtyping** | Auto traits, dynamic dispatch vtables (`dyn Trait`), marker proofs | `heterogeneous_bus` |
| **10** | **Bidirectional C++/Rust FFI** | `cxx` bridge, `Pin<&mut CppClass>`, `UniquePtr`, `panic = "abort"` | `sensor_fusion_bridge` |

**Progression Protocol**:

1. Check existing directories in `src/rust/crash_course/` to identify the learner's current progress.
2. Introduce the next module with the C++20/23 systems delta and low-level architectural invariants.
3. Scaffold the exercise, execute the automated red-phase verification, and confirm the red state.
4. Yield control to the learner.
5. Upon learner submission, execute the Green/Refactor Critique, verify mastery, and unlock the next module.

### Mode 2: Custom Task Mode

Accommodates arbitrary user-specified systems algorithms, data structures, or FFI bridges (e.g. "Scaffold an intrusive doubly-linked list with generational
indices" or "Set up an io_uring completion queue parser in Rust"):

1. **Analyze Requirements**: Extract component name, public methods, memory layout constraints, concurrency model, and FFI boundaries. If ambiguous, ask at
   most once with concrete options and recommended systems defaults.
2. **Derive Identifiers**: Compute `<slug>` (snake_case, e.g. `ring_buffer`) and `<Name>` (CapWords, e.g. `RingBuffer`).
3. **Target Directory**: Scaffold inside `src/rust/crash_course/custom_<slug>/`.
4. **Scaffold & Verify**: Generate isolated manifest, stubs with `todo!()`, and a 4-tier test suite. Run red-phase verification before yielding control.

---

## Target Scaffolding Architecture & Workspace Isolation

All exercises generated by this skill reside within this repository under `src/rust/crash_course/<exercise>/`.

### 1. Root Workspace Insulation Invariant

- **Strict Exclusion**: `src/rust/crash_course` is explicitly excluded from the root `Cargo.toml` `members` via:

  ```toml
  # Root Cargo.toml
  [workspace]
  resolver = "3"
  members = [
      "src/rust/leet_code",
      "src/rust/systems",
  ]
  exclude = [
      "src/rust/crash_course",
  ]
  ```

- **Rationale**: Root `Cargo.toml` workspace lints deny `todo` (`todo = "deny"`), and root CI executes `pixi run clippy` and `pixi run cov-rust` (`>= 85%`
  gate). Keeping crash course exercises isolated prevents active red-phase stubs from breaking repository pre-commit checks or pull request CI.

### 2. Standalone Exercise Manifest Contract (`Cargo.toml`)

Each generated exercise crate declares an isolated standalone `[workspace]` and allows `todo` during the red phase:

```toml
[package]
name = "<exercise_slug>"
version = "0.1.0"
edition = "2024"
resolver = "3"
rust-version = "1.99.1"
publish = false

[workspace]

[dependencies]
thiserror = { version = "2.0", default-features = false }
cxx = { version = "1.0", optional = true }
libc = { version = "0.2", optional = true }

[build-dependencies]
cxx-build = { version = "1.0", optional = true }
cc = { version = "1.2", optional = true }

[lints.rust]
unsafe_op_in_unsafe_fn = "deny"

[lints.clippy]
all = { level = "warn", priority = -1 }
pedantic = { level = "warn", priority = -1 }
todo = "allow" # Permits red-phase todo!() stubs; audited during green critique
```

### 3. Pixi Toolchain Integration

All commands must be executed via `pixi run`. Pixi environment variables (`CARGO_TARGET_DIR = build/cargo`, `CC = clang`, `CXX = clang++`, `RUST_SRC_PATH`)
automatically apply:

```bash
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml
```

---

## C++ and Rust Interoperability Architecture

### 1. Safe Bidirectional Bridges with `cxx`

The `cxx` crate provides compile-time validated interoperability with zero-cost binding generation:

- **`#[cxx::bridge]`**: Defines the shared interface in Rust.
- **`cxx::UniquePtr<T>`**: Mirrors `std::unique_ptr<T>`. Dropping the Rust handle invokes the C++ destructor.
- **`cxx::CxxVector<T>`**: Maps directly to `std::vector<T>`, allowing slice conversion via `.as_slice()`.
- **`Pin<&mut T>`**: C++ objects have address-dependent identity (`this`). Any mutating method on an opaque C++ class across FFI MUST accept
  `self: Pin<&mut T>`, preventing Rust from relocating the object via bitwise `memcpy`.

```rust
#[cxx::bridge(namespace = "systems::interop")]
pub mod ffi {
    unsafe extern "C++" {
        include!("interop_bridge.hpp");
        type EngineController;

        fn create_engine() -> UniquePtr<EngineController>;
        fn step(self: Pin<&mut EngineController>, delta_t: f64) -> f64;
        fn status(&self) -> u32;
    }

    #[derive(Debug, Clone, Copy)]
    struct TelemetryPacket {
        seq: u64,
        rpm: f64,
    }

    extern "Rust" {
        type RustTelemetrySink;
        fn ingest_packet(self: &mut RustTelemetrySink, pkt: TelemetryPacket);
    }
}
```

### 2. Raw C ABI & Toolchain Ecosystem

- **`unsafe extern "C"` & `#[repr(C)]`**: Strict layout compatibility matching C struct alignment rules without field reordering.
- **`bindgen`**: Generates Rust FFI bindings from C/C++ header files using libclang.
- **`cbindgen`**: Reads Rust `#[repr(C)]` declarations and emits standard C/C++ header files.
- **Google `crubit`**: Direct Clang AST integration for bidirectional bindings without manual bridge IDLs. Teaches architectural contrast against `cxx`.

### 3. Cross-Language Safety Invariants

- **Panic & Exception Containment**:
  - Unwinding across an `extern "C"` boundary is undefined behavior; in Rust Edition 2024 it triggers an immediate `std::process::abort()`.
  - The systems track mandates `panic = "abort"` in Rust and `-fno-exceptions` in C++. Cross-language errors must cross boundaries via explicit status codes or
    plain error structs.
- **Allocator Segregation**: Memory allocated by Rust's allocator must never be freed by C++ `free`/`delete`, and vice-versa. Ownership handoff uses
  `Box::into_raw` and `Box::from_raw`.
- **Lifetimes across FFI**: Pointers discard lifetime bounds. Safe wrapper structs must attach lifetime markers using `PhantomData<&'a T>`.

---

## Strict Red-Green-Refactor TDD Protocol

The coach enforces a strict verification lifecycle. An anti-solution-leakage gate guarantees the user solves the engineering challenge.

```text
+---------------------------------------------------------------------------------------------------------+
|                                    STRICT TDD VERIFICATION LIFECYCLE                                    |
+---------------------------------------------------------------------------------------------------------+
| Phase 1: Scaffold & Stub Only  --> Generate manifests, C++ bridge, signatures, 4-tier test suite.       |
| Phase 2: Anti-Solution Leakage --> Mandate todo!("Implement <fn>") in all stubs. Zero logic leaks.     |
| Phase 3: Red-Phase Gate        --> cargo fmt -> cargo test --no-run -> cargo clippy --no-deps -> cargo test (fails)    |
|                                    Confirm failures are SOLELY due to todo!() panics. Yield to user.   |
| Phase 4: Green/Refactor Mode   --> cargo test (pass) -> cargo clippy (-D warnings) -> cargo miri test   |
|                                    Deliver interview-grade 4-pillar architectural critique.             |
+---------------------------------------------------------------------------------------------------------+
```

### Phase 1: Scaffold and Stub Only

Generate the minimal set of files required for compilation:

1. `Cargo.toml`: Standalone manifest pinned to Edition 2024.
2. `build.rs` & companion C++ headers (if interop exercise).
3. `src/lib.rs`: Trait definitions, public structs, strongly-typed error enums (`thiserror`), and method signatures.
4. `tests/<exercise>_test.rs`: Comprehensive 4-tier test suite:
   - **Tier 1 (Canonical)**: Standard happy-path functional contracts.
   - **Tier 2 (Boundary)**: Zero capacity, maximum limits, arithmetic overflows (`i64::MAX`).
   - **Tier 3 (Invariants & RAII)**: Drop execution tracking, memory leak detection, ownership transfer proofs.
   - **Tier 4 (Concurrency / FFI Safety)**: Multi-threaded contention, timeout verification, FFI pointer integrity.

### Phase 2: Anti-Solution Leakage Gate

- **MANDATORY**: Every target function and method body MUST be populated exclusively with:

  ```rust
  todo!("Implement <module>::<method_name>")
  ```

- **STRICT PROHIBITION**: Under no circumstances may an agent write working algorithm logic, provide helper routines that solve the core problem, or leave
  step-by-step implementation code in comments.
- **Socratic Interaction**: If the learner asks for help, explain the borrow checker mechanics, memory layouts, or lifetime relationships. Do not provide
  solution code unless explicitly ordered: *"Show me the reference solution"*.

### Phase 3: Automated Red-Phase Verification Sequence

Before yielding control to the learner, the coach MUST execute and verify the following sequence:

```bash
# 1. Format scaffolded source files to guarantee clean rustfmt style:
pixi run cargo fmt --manifest-path src/rust/crash_course/<exercise>/Cargo.toml

# 2. Verify clean compilation without executing tests:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --no-run

# 3. Verify static analysis passes on stub interfaces and tests:
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --no-deps

# 4. Confirm all tests fail STRICTLY on todo!() panics:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml
```

**Verification Oracle Checks**:

- Formatting and compilation must exit with status code 0 and zero errors.
- `cargo test` output must report: `FAILED. 0 passed; N failed; 0 ignored`.
- Every failing test failure message must match: `panicked at ... 'not yet implemented: Implement ...'`.
- If a test passes unexpectedly, or if it fails due to a syntax error or compilation crash, the coach must fix the test harness before yielding control.
- **Yielding to User**: Inform the user that the workspace is in the verified red state, report the test count, and display the path to `src/lib.rs`.

### Phase 4: Green/Refactor Critique Mode Sequence

When the learner announces they have completed the implementation and requests a critique:

```bash
# 1. Verify test suite passes completely:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml

# 2. Strict static analysis and lint audit with zero warnings:
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --all-targets -- -D warnings

# 3. Undefined behavior and aliasing audit (when applicable, pure Rust):
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml -- --nocapture
```

*(Note on Miri: If `cargo-miri` is available on the host toolchain, execute `cargo miri test` on pure-Rust modules. On conda-forge or FFI targets, see
`references/interview_rubric.md` for AddressSanitizer fallback options).*

#### The 4-Pillar Interview Critique Rubric

Evaluate the submission against senior systems interview standards:

1. **Idiomatic Rust & Edition 2024 Standards**:
   - Absence of redundant `.clone()` or unnecessary allocations.
   - Functional iterator combinators preferred over explicit index loops.
   - Idiomatic pattern matching: `let ... else` for early exit guards; `?` operator over nested matches.
2. **Zero-Cost Abstractions & Memory Layout**:
   - Zero steady-state heap allocation (`malloc`/`new` absent after initialization).
   - Compiler auto-vectorization and bounds-check elimination.
   - Cache-line padding (`#[repr(align(64))]`) on concurrent indices to eliminate false sharing.
   - Atomic orderings: Precise `Acquire`/`Release` or `Relaxed` instead of heavy `SeqCst`.
3. **Memory Safety, Aliasing & Miri Validation**:
   - Every `unsafe` block must carry an explicit `// SAFETY:` rationale covering pointer alignment, non-null guarantees, and aliasing invariance.
   - Strict adherence to Stacked Borrows / Tree Borrows models without undefined behavior.
4. **Algorithmic Complexity & Systems Architecture**:
   - Computational time ($O(N)$) and space ($O(1)$) complexity guarantees.
   - Bounded Worst-Case Execution Time (WCET) on embedded / real-time paths.
   - Proper `Send` and `Sync` auto trait bounds on concurrent primitives.

#### Review Output Structure

Deliver the critique using this structured schema:

- **Architectural Strengths**: Highlighting exceptional systems choices (e.g. cache alignment, zero-copy borrowing).
- **Critical Hazards & Invariant Violations**: Undocumented `unsafe`, potential data races, or hidden heap allocations.
- **Interview Trade-offs**: Discussing how the candidate's solution compares to alternative designs (e.g. intrusive vs. arena).
- **Refactoring Diffs**: Concrete, minimal code improvements demonstrating idiomatic Edition 2024 patterns.

---

## 📚 Reference Guides & Templates

For detailed specifications, consult the reference files and scaffolding templates:

<!-- markdownlint-disable MD057 -->
### Reference Guides

- [Mental Models & Edition 2024 Reference](references/mental_models.md): Complete C++20/23 and Python mental model mappings, Edition 2024 syntax, and lints.
- [C++/Rust Interoperability Guide](references/interop.md): Deep dive into `cxx`, raw C ABI, `bindgen`/`cbindgen`, Google `crubit`, and FFI safety invariants.
- [Curriculum Roadmap](references/curriculum.md): Comprehensive syllabus for the 10-module autonomous curriculum progression.
- [Strict TDD Protocol Guide](references/tdd_protocol.md): Verification command sequences, anti-solution leakage rules, and test oracle contracts.
- [Interview Critique Rubric](references/interview_rubric.md): 4-pillar interview evaluation standards, Miri verification, and refactoring schemas.

### Scaffolding Templates

- [Exercise Manifest Template](templates/Cargo.toml.template): Standalone manifest with Edition 2024, resolver 3, and isolated workspace header.
- [Library Skeleton Template](templates/exercise_lib.rs.template): Crate root with `#![deny(unsafe_op_in_unsafe_fn)]` and `todo!()` stubs.
- [4-Tier Integration Test Template](templates/exercise_test.rs.template): Integration test suite covering canonical, boundary, invariant, and concurrency
  tiers.
- [CXX Bridge Rust Template](templates/cxx_bridge.rs.template): `#[cxx::bridge]` module illustrating `UniquePtr`, `CxxVector`, and `Pin<&mut T>`.
- [CXX Bridge C++ Header Template](templates/cxx_bridge.hpp.template): Companion C++23 header with matching signatures and types.
- [Hermetic Build Script Template](templates/build.rs.template): Build script using `cxx_build` with `-std=c++23` and strict warnings.
<!-- markdownlint-enable MD057 -->
