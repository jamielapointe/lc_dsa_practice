---
trigger: glob
description: Conventions, CMake 4.5+ registration, Clang 23.1.5+ static analysis, and testing rules for Common C++23 and C++23/ISO C23 LeetCode tracks.
globs: "src/include/**/*.hpp, src/include/**/*.h, src/cpp/**/*, src/c/**/*, test/cpp/**/*, test/c/**/*, examples/cpp/**/*, examples/c/**/*, benchmarks/cpp/**/*, benchmarks/c/**/*, examples/interop/crubit/cpp/**/*, examples/interop/crubit/CMakeLists.txt, cmake/**/*, CMakeLists.txt, CMakePresets.json, .clang-format, .clang-tidy, .gersemirc"
---

# C++23, ISO C23 & CMake 4.5+ Rules

## CMake 4.5+ & Preset Invariants

- Drive CMake strictly via `CMakePresets.json` (Schema v11, builds under `build/<preset>`): `pixi run cmake --workflow --preset <name>`. Avoid overriding with
  `ALLOW_NON_PIXI_TOOLCHAIN`.
- Presets: `dev-debug`, `ci-debug`, `dev-release`, `asan`, `ubsan`, `tsan`, `coverage`, `benchmark`, `interop-debug`, `interop-release`, `sysroot-release`,
  `cross-aarch64`, `cross-riscv64`.
- Root `CMakeLists.txt` enforces `CMD_NON_TARGET_DIRECTIVE`, `CMD_INSTALL_ABSOLUTE_DESTINATION`, and `CMD_EXPERIMENTAL` as `SEND_ERROR`, requires C++23 / ISO
  C23 with extensions disabled, and emits CPS `PACKAGE_INFO` + SPDX 3.0.1 `SBOM` when `INSTALL_LC_DSA=ON`.
- Keep `ENABLE_CXX_MODULES=OFF` by default so `ccache`, `clang-tidy`, and `misc-include-cleaner` stay deterministic.
- Register public headers via `FILE_SET HEADERS` (`BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include"`) and sources via `FILE_SET sources TYPE SOURCES`.
- Format all CMake files with `pixi run format-cmake` (`gersemi`, `.gersemirc`: 100 columns, 4-space indent).

## Clang 23.1.5+ Style & Static Analysis

- Formatting: Google style via `.clang-format` (`pixi run format-cpp`).
- Static analysis: `.clang-tidy` runs with `WarningsAsErrors: "*"`. Suppress intentionally unused parameters with `static_cast<void>(x)`.
- **Google Integer Types rule (C++)**: allow `int`, unqualified `<cstdint>` fixed-width types (`uint32_t`, `int64_t`, etc.), and `size_t`/`ptrdiff_t`
  (`<cstddef>`); never use built-in `short`, `long`, or `unsigned`.
- Deliberate `NOLINT` comments (for example, a C array backing a fixed-capacity buffer) MUST carry a one-line justification.
- Avoid `std::chrono` literals (`10ms`); `misc-include-cleaner` cannot attribute them. Include public POSIX headers directly.

## Track: Common C++23 (`common`)

- Shared header-only C++23 utilities live in `src/include/common/<name>.hpp` (`namespace common`, included as `"common/<name>.hpp"`), exposed via the
  `common::common` `INTERFACE` target in `src/cpp/common/CMakeLists.txt` (example: `common/math_extra.hpp` providing `common::Integer` and
  `common::checked_add`).
- `common::common` is auto-linked across `leet_code`, `systems`, `interop`, examples, tests, and benchmarks.
- When adding a header under `src/include/common/`, append it to `COMMON_HEADERS` in `src/cpp/common/CMakeLists.txt` so CMake verifies standalone compilation
  (`CMAKE_VERIFY_INTERFACE_HEADER_SETS`), runs `clang-tidy` (`--header-filter=.*/src/include/common/.*`), and exports it in CPS/SBOM manifests.
- Add unit tests in `test/cpp/common/<name>_test.cpp`, registered in `test/cpp/common/CMakeLists.txt` via
  `add_project_test(<name>_test SOURCES <name>_test.cpp LIBS common::common LABELS "common")` with `CXX_CLANG_TIDY` header-filter targeting
  `.*/src/include/common/.*`.

## Track: C++23 LeetCode (`leet_code`)

- Each problem `<slug_snake>` owns `src/include/leet_code/<slug_snake>.hpp`, `src/cpp/leet_code/<slug_snake>.cpp`, and
  `test/cpp/leet_code/<slug_snake>_test.cpp`, registered in `src/cpp/leet_code/CMakeLists.txt` and `test/cpp/leet_code/CMakeLists.txt`.
- Header contract: `#pragma once`, `namespace leet_code`, Doxygen `@file`/`@brief`, a modern C++23 API (`[[nodiscard]] auto f(...) -> ...`, `std::span`,
  `std::optional`/`std::expected`), and a LeetCode-compatible `class Solution` wrapper delegating to it.
- CMake registration: `leet_code_add_library(<slug> SOURCES <slug>.cpp [HEADERS ...] [LIBS ...])` (alias `leet_code::<slug>`) and
  `add_project_test(<slug>_test SOURCES <slug>_test.cpp LIBS leet_code::<slug>)`.
- Reuse shared structures in `src/include/leet_code/list_node.hpp` (`ListNode`, RAII `ScopedLinkedList`).
- Tests follow 4 tiers: canonical examples, boundary cases, invariants, and a max-constraint stress test. New TDD-red stubs must compile cleanly and return
  dummy values without crashing (guard pointers with `ASSERT_NE`).
- Wall-clock assertions are noisy under sanitizers; keep bounds generous. `NumberOfIslandsTest.MaxConstraintsGrid` and `LruCacheTest.StressTest` are
  single-threaded and excluded from the `tsan` test preset; keep new wall-clock tests out of the threading signal.

## Track: ISO C23 LeetCode (`leet_code_c`, Scaffolded)

- Scaffolded for future strict ISO C23 (`-std=c23`, `-ftrivial-auto-var-init=zero`) problems (currently 0 problems; `leetcode-setup` covers `cpp`, `python`,
  and `rust`).
- Register libraries in `src/c/leet_code/CMakeLists.txt` with `leet_code_c_add_library(<slug> SOURCES <slug>.c [HEADERS ...] [LIBS ...])` (alias
  `leet_code_c::<slug>`, public headers in `src/include/leet_code/<slug>.h`, tests in `test/c/leet_code/`).
