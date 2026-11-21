---
trigger: glob
description: Rust 1.100+ (Edition 2024) Cargo workspace configuration, strict Clippy/rustfmt gates, LeetCode and Systems Rust conventions, and crash_course exclusion rules.
globs: "src/rust/**/*.rs, src/rust/**/Cargo.toml, test/rust/**/*.rs, test/rust/**/Cargo.toml, examples/interop/**/*.rs, examples/interop/**/Cargo.toml, Cargo.toml, .cargo/config.toml, rustfmt.toml, clippy.toml"
---

# Rust 1.100+ (Edition 2024) Rules

## Workspace & Toolchain Invariants

- Target **Rust `1.100+`**, `edition = "2024"`, `resolver = "3"`, `rust-version = "1.100"`. Always invoke Cargo through `pixi run` (`test-rust`, `clippy`,
  `format-rust`, `cov-rust`).
- Cargo build artifacts isolate to `build/cargo/` via `.cargo/config.toml` (`target-dir = "build/cargo"`) and Pixi's `CARGO_TARGET_DIR`. Never commit `target/`.
- Formatting (`rustfmt.toml`): `max_width = 100`, `edition = "2024"`, `style_edition = "2024"`, `hex_literal_case = "Lower"`. Run `pixi run format-rust` and
  `pixi run format-toml` after `Cargo.toml` edits.
- **Strict Clippy & Rustc lints** (`pixi run clippy` runs `--workspace --all-targets -- -D warnings`):
  - Denied in root `Cargo.toml`: `unwrap_used`, `panic`, `todo`, `unimplemented`, `clone_on_ref_ptr`, `enum_glob_use`, `wildcard_imports`,
    `undocumented_unsafe_blocks`, `non_ascii_idents`, `unsafe_code`, `unsafe_op_in_unsafe_fn`.
  - Disallowed methods in `clippy.toml`: `std::panic::catch_unwind` (release builds use `panic = "abort"`), `Option::unwrap`, and `Result::unwrap`.
  - Complexity thresholds (`clippy.toml`): cognitive complexity <= 25, too-many-lines <= 100, too-many-arguments <= 7.
- Coverage gate: `pixi run cov-rust` (`cargo-llvm-cov --workspace --all-targets --fail-under-lines 85`).

## Track: Rust LeetCode (`leet_code`)

- Crate lives in `src/rust/leet_code/` with one module per problem `src/rust/leet_code/src/<slug_snake>.rs`, exported via `pub mod <slug_snake>;` in
  `src/rust/leet_code/src/lib.rs`.
- Integration tests live in `test/rust/leet_code/<slug_snake>_test.rs` and MUST be registered as a `[[test]]` target in `src/rust/leet_code/Cargo.toml`.
- Each module exposes:
  - An idiomatic safe function (`&[i32]`, `Result<T, <Name>Error>` or `Option<T>`, `#[must_use]`, checked/widened overflow protection).
  - The LeetCode compatibility wrapper `pub struct Solution;` with `impl Solution { pub fn <method>(...) -> ... }`.
- Reuse shared structures in `src/rust/leet_code/src/list_node.rs` (`ListNode`, `create_linked_list`, `linked_list_to_vec`).
- Tests follow 4 tiers: canonical examples, boundary & corner cases, invariants & ownership, and stress/simulation.

## Track: Systems Rust (`systems`)

- Crate lives in `src/rust/systems/` (`src/lib.rs` + `src/<name>.rs`); integration tests live in `test/rust/systems/<name>_test.rs` with `[[test]]` entries in
  `src/rust/systems/Cargo.toml`.
- Compile-time capacity and inline storage via const generics (e.g., `BoundedFifo<T, const CAPACITY: usize>`).
- Zero heap allocation in steady-state operations; no `Vec` or `VecDeque` after initialization.
- Public operations are safe, deterministic, and return `Result<T, <Name>Error>`. Blocking APIs provide try, timed (`push_timeout`, `pop_timeout`), and
  blocking variants plus a `close()` path waking all waiters.
- Safe concurrency primitives (e.g., `BoundedFifo`) use `std::sync::{Mutex, Condvar}` with zero `unsafe` (`unsafe_code = "deny"`).
- OS/FFI shared-memory modules (`shm_pubsub`, `platform`) isolate item-scoped `#[allow(unsafe_code)]` with multi-point `// SAFETY:` proofs — never crate-wide
  `#![allow(unsafe_code)]`.
- Tests cover 4 tiers (canonical, boundary/timeouts/drainage, RAII/drop invariants, multi-threaded MPMC stress) and declare ASPICE traceability tags
  (`// requirements: <PREFIX>-REQ-NNN`), verified by `pixi run check-traceability`.

## Excluded Sub-Workspaces

- `src/rust/crash_course` is excluded from root `Cargo.toml` so active TDD-red `todo!()` exercises from `.agents/skills/rust-systems-tdd-coach` (enforcing
  Edition 2024, `resolver = "3"`, `rust-version = "1.100"`, `#![deny(unsafe_op_in_unsafe_fn)]`, and C++/Rust interop via `cxx`/raw C ABI/`Pin<&mut T>`) never
  break root CI gates.
- `src/rust/interop`, `examples/interop`, and `test/rust/interop` are excluded from root `Cargo.toml` and built exclusively through the Crubit workflows (see
  `.agents/rules/crubit-interop.md`).
