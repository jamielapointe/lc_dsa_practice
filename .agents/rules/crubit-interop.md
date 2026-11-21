---
trigger: glob
description: Hermetic toolchain, libc++ isolation, public header purity, and single-staticlib rules for Google Crubit C++23 <-> Rust 2024 FFI interop.
globs: "src/include/interop/**/*, src/cpp/interop/**/*, src/rust/interop/**/*, test/cpp/interop/**/*, test/rust/interop/**/*, examples/interop/**/*, scripts/crubit/**/*, cmake/CrubitInterop.cmake, docs/interop/crubit.md"
---

# Crubit C++23 <-> Rust 2024 Interop Rules

## Primary References

- Consult `docs/interop/crubit.md` for architecture, type-mapping matrix, and showcase documentation, and `cmake/CrubitInterop.cmake` for CMake binding
  generation helpers.

## Hermetic Toolchain & Bootstrap

- `scripts/crubit/bootstrap_crubit.py` pins Crubit commit `b7805b5d09574033ec3144d59715a539a4f652b0` and Rust `1.100.0` + `rustc-dev` under `build/crubit/`.
- Keep `RUSTC_BOOTSTRAP=1` scoped strictly to Crubit generators and interop Cargo invocations — never export it globally or for root workspace crates.

## Isolation & Binding Invariants

- Interop targets build only when `BUILD_INTEROP=ON` (presets `interop-debug` and `interop-release`) and use `-stdlib=libc++`.
- Public C++ headers parsed by `rs_bindings_from_cc` MUST avoid STL headers (`<string>`, `<vector>`, `<memory>`, `<optional>`, `<expected>`).
- C++ interop tests use `test/cpp/interop/crubit/interop_test_harness.hpp` and link **at most one Rust `staticlib` per binary** to avoid duplicate Rust runtime
  symbols.
- Interop Rust crates (`src/rust/interop`, `examples/interop`, `test/rust/interop`) MUST remain excluded from root `Cargo.toml`.

## Verification

- Bootstrap toolchain: `pixi run crubit-bootstrap`.
- Build and test: `pixi run test-interop` (`interop-debug`), `pixi run interop-release` (`interop-release`), and `pixi run clippy-interop`.
- Verify showcase demos: `pixi run examples-interop` (or `pixi run python scripts/examples/run_crubit_examples.py --preset interop-debug`).
