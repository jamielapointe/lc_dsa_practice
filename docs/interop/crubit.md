# Google Crubit C++23 <-> Rust 2024 Interoperability

This document describes the Google Crubit integration architecture, CMake/Cargo build plumbing, C++23 <-> Rust 2024 type-mapping rules, and 5-module showcase
suite in `lc_dsa_practice`.

## Architecture Overview

The interop track provides **bidirectional** bindings between modern C++23 (`-std=c++23`, `-stdlib=libc++`) and idiomatic Rust 2024 (`edition = "2024"`,
`rust-version = "1.99.1"`) using Google Crubit:

- **C++23 -> Rust 2024 (`rs_bindings_from_cc`)**: Generates Rust FFI bindings (`src/lib.rs`), C++ thunk implementations (`*_rust_api_impl.cc`), and a standalone
  generated `Cargo.toml` so Rust crates call C++23 APIs safely.
- **Rust 2024 -> C++23 (`cargo-cpp_api_from_rust` / `cc_bindings_from_rs`)**: Generates C++23 header wrappers (`include/crubit/<crate>.h`) and static libraries
  (`lib<crate>.a`) so C++23 targets call Rust 2024 crates directly.

### Pinned Toolchain & Hermetic Bootstrap

All Crubit sources, Rust compiler internals, and code-generation binaries are bootstrapped into the gitignored `build/crubit/` tree by
[`scripts/crubit/bootstrap_crubit.py`](../../scripts/crubit/bootstrap_crubit.py):

| Artifact | Pin / Location | Notes |
| :--- | :--- | :--- |
| Google Crubit source | `b7805b5d09574033ec3144d59715a539a4f652b0` | Cloned into `build/crubit/src` with local LLVM 23.1.3 compatibility patches |
| Hermetic Rust sysroot | `1.99.1` (`2026-10-15`) | Official `rust` + `rustc-dev` tarballs SHA-256 verified into `build/crubit/toolchain/sysroot` |
| Supported host triples | `x86_64-unknown-linux-gnu`, `aarch64-unknown-linux-gnu`, `aarch64-apple-darwin` | Auto-detected via host OS/architecture |
| Generator binaries | `build/crubit/install/bin/` | `cargo-cpp_api_from_rust`, `cc_bindings_from_rs`, and `rs_bindings_from_cc` |
| Generated build metadata | `build/crubit/crubit_paths.cmake`, `build/crubit/crubit_env.json` | Consumed by CMake and `pixi run clippy-interop` |

Warm invocations of `pixi run crubit-bootstrap` validate JSON stamp files under `build/crubit/stamps/` and return in under 0.5 seconds. `RUSTC_BOOTSTRAP=1` is
scoped strictly to Crubit generator builds and Crubit interop Cargo invocations; handwritten Rust crates use standard Rust 2024 without `#![feature(...)]`
attributes.

### `-stdlib=libc++` Isolation & CMake Integration

Crubit's C++ support runtime and generated thunks require LLVM `libc++` (`-stdlib=libc++`). Because `lc_dsa_practice` isolates interop builds behind
`BUILD_INTEROP=ON` (used by the `interop-debug` and `interop-release` CMake presets):

- Pixi installs `libcxx >= 23.1.4` and `libcxx-devel >= 23.1.4` on `linux-64` and `osx-arm64` (plus `libcxxabi >= 23.1.4` on `linux-64`).
- `cmake/CrubitInterop.cmake` compiles `crubit::support_cc` and all generated `*_impl_cc` targets with `-stdlib=libc++`, `SYSTEM` includes, and
  `CXX_CLANG_TIDY ""` so third-party/generated Crubit code is exempt from project `WarningsAsErrors`.
- Public C++23 headers parsed by `rs_bindings_from_cc` under `src/include/interop/crubit/` stay free of standard library headers such as `<string>`, `<vector>`,
  `<memory>`, `<optional>`, and `<expected>` (Crubit CMake mode does not use Bazel `cc_std`). C++23 tests and demos that use `std::expected` include a separate
  companion header (`traits_and_errors_expected.hpp`) that is not passed to `rs_bindings_from_cc`.
- C++ interop tests use a lightweight header-only harness (`test/cpp/interop/crubit/interop_test_harness.hpp`) instead of conda-forge `GTest::gtest_main` (which
  links `libstdc++` on Linux).
- Each `crubit_add_cpp_api_from_rust` target builds a Rust `staticlib` containing Rust `std`; therefore each C++ test or demo executable links at most one Rust
  `staticlib` target.

### `cmake/CrubitInterop.cmake` API

`cmake/CrubitInterop.cmake` exposes three functions:

1. **`crubit_add_cpp_api_from_rust`** — runs `cargo cpp_api_from_rust` on a normal Rust library crate (do not set `crate-type = ["staticlib"]` in `Cargo.toml`)
   and exposes an `INTERFACE` target `<target_name>` so C++ consumers link `<target_name>` and `#include "crubit/<crate_name>.h"`:

   ```cmake
   crubit_add_cpp_api_from_rust(<target_name>
       MANIFEST_PATH <Cargo.toml> [CRATE_NAME <name>] [OUT_DIR <dir>]
       [FEATURES <f1;f2>] [ALL_FEATURES] [NO_DEFAULT_FEATURES]
   )
   ```

2. **`crubit_add_rust_api_from_cpp`** — invokes [`scripts/crubit/invoke_rs_bindings_from_cc.py`](../../scripts/crubit/invoke_rs_bindings_from_cc.py) to resolve
   Pixi Clang resource headers, `libc++` headers, and macOS SDK sysroots, then generates `lib.rs`, `*_rust_api_impl.cc`, and `Cargo.toml`:

   ```cmake
   crubit_add_rust_api_from_cpp(<target_name>
       CPP_TARGET <cc_tgt> PUBLIC_HEADERS <hdr1;...> [CRATE_NAME <name>] [OUT_DIR <dir>]
       [INCLUDE_DIRS <d1;...>] [SYSTEM_INCLUDE_DIRS <d1;...>] [DEFINES <D1;...>]
       [EXTRA_RS_SRCS <...>] [EXTRA_CPP_SRCS <...>] [CLANG_ARGS <...>]
   )
   ```

3. **`crubit_add_rust_binary_or_test`** — patches `crates.io` dependencies for each `API_TARGETS` crate via `--config patch.crates-io.<crate>.path=...` and
   links the C++ static libraries and `crubit::support_cc` into the final Rust binary or CTest target:

   ```cmake
   crubit_add_rust_binary_or_test(<target_name>
       MANIFEST_PATH <Cargo.toml> [API_TARGETS <t1;...>] [CPP_LIBS <l1;...>]
       [CRATE_NAME <name>] [BINARY_NAME <bin>] [TEST]
       [ALL_FEATURES] [NO_DEFAULT_FEATURES] [FEATURES <f1;...>] [LABELS <l1;...>] [ARGS <a1;...>]
   )
   ```

## Type-Mapping & Best-Practices Matrix (C++23 <-> Rust 2024)

| Concept | C++23 Side | Rust 2024 Side | Crubit Bridge & Best Practices |
| :--- | :--- | :--- | :--- |
| Primitives & trivial structs | `int`, `unsigned int`, `unsigned long long`, `double`, `bool`, POD `struct` | `i32`, `u32`, `u64`, `f64`, `bool`, `#[derive(Clone, Copy)] pub struct` | Passed by value with zero marshalling overhead; keep fields public for transparent C++ field access. |
| Scoped & payload enums | `enum class FlightPhase : int` / generated tagged class for Rust payload `enum` | Unitless `enum` / payload `enum ControlCommand` | C++ `enum class` maps to a Rust newtype/enum; Rust payload enums expose factory constructors and pattern-matched accessors in C++. |
| References & lifetimes | `const T& [[clang::lifetimebound]]`, `T&` | `&T`, `&mut T` | Annotate C++ functions returning references tied to parameters with `[[clang::lifetimebound]]` so Crubit binds Rust output lifetimes to the input borrow. |
| String & slice views | `rs::StrRef`, `rs::SliceRef<const T>`, `rs::SliceRef<T>` | `&str`, `&[T]`, `&mut [T]` | Construct `rs::StrRef` from UTF-8 `std::string_view` (`rs::StrRef::FromUtf8`) and `rs::SliceRef` from `std::span` / arrays for zero-copy views. |
| Non-trivially relocatable RAII | `class LeaseGuard` with user-defined move/destructor | `#[::ctor::recursively_pinned]` + `ctor::emplace!` | Non-trivially relocatable C++ classes cannot be moved by Rust value semantics; stack-pin them with `ctor::emplace!` and `Pin<&mut T>` so `~LeaseGuard()` runs deterministically. |
| Heap ownership & drop | `std::unique_ptr` / opaque Crubit handle (`TelemetryPacketBuffer`) | `Box<T>`, `Vec<u8>`, `String`, `impl Drop` | Rust structs owning heap allocations expose safe methods to C++ and run `Drop` when the C++ wrapper object leaves scope. |
| Traits, operators & formatting | `operator==`, `operator<<`, copy/clone | `#[derive(PartialEq, Clone)]`, `std::fmt::Display` | Rust `PartialEq`, `Clone`, and `Display` project to C++ `operator==`, copy/clone, and `operator<<`; C++ `operator==` projects back to Rust `PartialEq`. |
| Fallible returns & options | `rs::Option<T>`, `rs::Result<T, E>`, companion `std::expected<T, E>` | `Option<T>`, `Result<T, E>` | Rust `Option`/`Result` map to `rs::Option`/`rs::Result` in C++. Use POD outcome structs across `rs_bindings_from_cc` and convert to `std::expected` in C++23-only headers. |

## Showcase Modules

The suite spans five complementary modules under `src/{include,cpp,rust}/interop/crubit/`, exercised bidirectionally by C++23 tests
(`test/cpp/interop/crubit/`), Rust 2024 tests (`test/rust/interop/crubit/crubit_cpp_from_rust_tests/`), five per-module C++23 demo binaries
(`examples/interop/crubit/cpp/`), and one unified Rust 2024 demo binary (`examples/interop/crubit/rust/crubit_showcase_rust_demo/`):

1. **`primitives_enums`** (`cc_primitives_enums` <-> `rs_primitives_enums`)
   - C++23 exposes `FlightPhase`, `Waypoint3d`, `euclidean_distance`, and `advance_phase`.
   - Rust 2024 exposes `KinematicSample`, payload enum `ControlCommand`, `integrate_kinematics`, `make_climb_command`, and `evaluate_command_target`.
2. **`references_views`** (`cc_references_views` <-> `rs_references_views`)
   - C++23 exposes `SignalWindow` with `peak_ref() [[clang::lifetimebound]]`, `select_dominant_window`, and `scale_in_place`.
   - Rust 2024 exposes `rms_slice(&[f64])`, `normalize_slice_in_place(&mut [f64], f64)`, `parse_sensor_tag_id(&str)`, and `RingCursor`.
3. **`ownership_raii`** (`cc_ownership_raii` <-> `rs_ownership_raii`)
   - C++23 exposes non-trivially relocatable `LeaseGuard` with deterministic `~LeaseGuard()` active-lease counting (`active_lease_count()`), constructed in Rust
     via `ctor::emplace!`.
   - Rust 2024 exposes heap-owning `TelemetryPacketBuffer` with `impl Drop` and atomic `live_packet_buffers()` verification in C++.
4. **`traits_errors`** (`cc_traits_errors` <-> `rs_traits_errors`)
   - C++23 exposes `GainVector2d` (`operator==`), `CalibrationCode`, `CalibrationOutcome`, `calibrate_pd_controller`, and C++23-only `to_expected`
     (`std::expected`).
   - Rust 2024 exposes `PidScheduleId` (`Clone`, `PartialEq`, `Display`), `ParseScheduleError`, `lookup_nominal_gain` (`Option<f64>`), and `parse_schedule_id`
     (`Result<PidScheduleId, ParseScheduleError>`).
5. **`telemetry_capstone`** (`cc_telemetry_bridge` + `rs_telemetry_validator`)
   - End-to-end zero-steady-state-heap safety pipeline: C++23 creates CRC-32-verified `SensorFrame` payloads, Rust 2024 `SafetyInterceptor` enforces watchdog
     freshness, monotonicity, finite bounds (`|torque_nm| <= 80.0`), CRC checks, and authenticated fault-latch recovery (`0xA5A5_5A5A_CAFE_BABE`), and C++23
     `ActuatorSink` commits safe torque commands or latches faults.

## Commands & Workflows

All commands run inside the managed Pixi environment:

```bash
# 1. Bootstrap pinned Crubit source, Rust 1.99.1 + rustc-dev sysroot, and generators
pixi run crubit-bootstrap

# 2. Configure, build, and run all bidirectional C++23 <-> Rust 2024 interop tests (Debug)
pixi run test-interop

# 3. Configure, build, and run all interop tests with LTO (Release)
pixi run interop-release

# 4. Build all C++23 and Rust 2024 showcase demo executables
pixi run examples-interop

# 5. Run Clippy (-D warnings) across all handwritten Rust interop crates
pixi run clippy-interop

# 6. Execute all 5 C++23 demos + the Rust 2024 demo and verify structured output
pixi run python scripts/examples/run_crubit_examples.py --preset interop-debug
```

Inspecting bootstrapped tool paths or checking stamp health directly:

```bash
pixi run python scripts/crubit/bootstrap_crubit.py --check --print-env
```
