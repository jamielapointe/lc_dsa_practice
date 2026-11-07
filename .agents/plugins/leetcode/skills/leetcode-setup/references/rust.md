# Rust LeetCode Scaffolding Reference

Used by the `leetcode-setup` skill when the user chose **`rust`**. Follow `rust-pro` (Rust 1.99.1+, Google Rust Styleguide, Clippy clean).

## Naming conventions

- `slug_snake`: slug with hyphens replaced by underscores (`add_two_numbers`).
- Module: `src/rust/leet_code/src/<slug_snake>.rs`
- Test: `test/rust/leet_code/<slug_snake>_test.rs`
- Crate: `leet_code` (part of root Cargo workspace).

## Shared data structures

If the problem uses `ListNode`, use `crate::list_node::ListNode`. For trees, use `crate::tree_node::TreeNode`.

## Generate the module (`src/rust/leet_code/src/<slug_snake>.rs`)

1. Top-level doc comments (`//!` or `///`): LeetCode problem ID, title, difficulty, URL, description markdown, constraints, and target complexity.
2. Two interfaces:
   - **Idiomatic Rust**: Accepts borrowed slices (`&[i32]`, `&str`) instead of owned `Vec`/`String`,
     returns `Result<T, <Name>Error>` or `Option<T>` with `#[must_use]`.
   - **LeetCode wrapper**: `pub struct Solution;` with `impl Solution { pub fn <official_method>(...) -> ... }`
     delegating to the idiomatic function.
3. Stub returns clean dummy value (`Err(<Name>Error::NotImplemented)`, `vec![]`, `0`), never panicking.
   Never use panic!(), todo!(), or unimplemented!() in stubs.
   Suppress unused parameters with `let _ = arg;` or `_arg`.

## Generate the 4-tier test suite (`test/rust/leet_code/<slug_snake>_test.rs`)

1. Tiers:
   - **Tier 1, canonical**: Official examples tested against both the idiomatic function and `Solution`. Assert actual expected answers.
   - **Tier 2, boundary / edge**: Minimum sizes, empty inputs, zeros, negative numbers, overflow boundaries.
   - **Tier 3, invariants**: Property checks, input immutability, differential verification against reference model.
   - **Tier 4, stress / OJ simulation**: Maximum constraint inputs generated with iterators, guarded with `std::time::Instant`.
2. Red-state safety: Tests must fail on assertions, not panics.

## Register in crate

Add `pub mod <slug_snake>;` to `src/rust/leet_code/src/lib.rs`.

## Verify (always through Pixi)

```bash
pixi run cargo test --test <slug_snake>_test   # must FAIL cleanly (TDD red)
pixi run cargo clippy -- -D warnings          # zero warnings
pixi run cargo fmt --check                    # formatted
```
