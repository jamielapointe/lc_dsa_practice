# C++23 LeetCode Scaffolding Reference

Used by the `leetcode-setup` skill when the user chose **`cpp`**. Follow `cpp-pro` and `cmake-pro`.

## Naming conventions

- `slug_snake`: slug with hyphens replaced by underscores (`add_two_numbers`).
- Header: `src/include/leet_code/<slug_snake>.hpp` (included as `"leet_code/<slug_snake>.hpp"`)
- Source: `src/cpp/leet_code/<slug_snake>.cpp`
- Test: `test/cpp/leet_code/<slug_snake>_test.cpp`
- Library target: `<slug_snake>` (aliased `leet_code::<slug_snake>`); test target: `<slug_snake>_test`
- Namespace: `leet_code`

## Shared data structures

If the starter code references `ListNode`, use the canonical `src/include/leet_code/list_node.hpp`:
`leet_code::ListNode`, `ScopedLinkedList` (RAII, prevents ASan leaks), `create_linked_list`, `linked_list_to_vector`, `free_linked_list`.
Link `leet_code::list_node` in CMake. For other shared types (for example `TreeNode`) add a canonical header under `src/include/leet_code/` first.

## Generate the header (`src/include/leet_code/<slug_snake>.hpp`)

1. `#pragma once`; include the needed standard headers (`<cstddef>`, `<span>`, `<vector>`, ...) and `leet_code/list_node.hpp` if needed.
2. Everything lives in `namespace leet_code { ... }`.
3. Doxygen documentation: `@file`, `@brief` with problem number, title, difficulty, URL, the full problem description with examples (from
   `description_markdown`), verbatim constraints, target complexity, and ownership notes (for example callers own returned `ListNode*`). **CRITICAL:** use `///`
   line comments, not block comments, because the Markdown may contain `/* ... */`.
4. Declare two interfaces:
   - **Idiomatic C++23**: `[[nodiscard]] auto <slug_snake>(...) -> ...;` using non-owning views (`std::span`), value semantics, or `std::expected`.
   - **LeetCode wrapper**: `class Solution { public: auto <camelCaseMethod>(...) -> ...; };` matching the official signature verbatim and delegating to the
     modern function.

## Generate the stub (`src/cpp/leet_code/<slug_snake>.cpp`)

1. Include the header, open `namespace leet_code`.
2. Satisfy `-Wunused-parameter` under `-Werror` with `static_cast<void>(arg);` for every parameter.
3. Return a clean dummy: pointers `nullptr`, numbers `0`, bool `false`, containers `{}`, `std::expected` -> `std::unexpected{"Not implemented"}`.
4. Implement the `Solution` wrapper by delegating to the modern function.

## Generate the 4-tier Google Test suite (`test/cpp/leet_code/<slug_snake>_test.cpp`)

1. Include the header, `<gtest/gtest.h>`, needed std headers (and `leet_code/list_node.hpp` for linked lists). Put tests in an anonymous namespace.
2. Tiers:
   - **Tier 1, canonical**: every official example against both the modern function and `Solution`. **Assert the ACTUAL CORRECT ANSWER** (for example
     `EXPECT_EQ(result, 1)`); never assert the stub's dummy value. Tests SHOULD fail until implemented.
   - **Tier 2, boundary / edge**: minimum sizes, empty containers, carries, max values, zeros.
   - **Tier 3, sanitizer / memory invariants**: no ASan leaks; wrap heap nodes in `ScopedLinkedList`.
   - **Tier 4, stress / OJ simulation**: maximum-constraint inputs generated programmatically (`std::generate`, `std::iota`, loops; never huge literals), a
     parameterized `struct TestCase` loop, and a `std::chrono` guard such as `EXPECT_LT(duration, std::chrono::milliseconds(200))`.
3. TDD-red safety: tests must not dereference a stub `nullptr`. Use `ASSERT_NE(result, nullptr);` first, or `linked_list_to_vector(result)` which returns `{}`
   for `nullptr`, so failures are clean assertions, not SIGSEGV.

## Register in CMake (target-based, `cmake-pro`)

`src/cpp/leet_code/CMakeLists.txt`:

```cmake
add_library(<slug_snake> STATIC <slug_snake>.cpp)
add_library(leet_code::<slug_snake> ALIAS <slug_snake>)

target_include_directories(
    <slug_snake>
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>
)

target_compile_features(<slug_snake> PUBLIC cxx_std_23)

target_link_libraries(
    <slug_snake>
    PRIVATE project_options project_warnings project_sanitizers project_architecture
)
```

(Add `PUBLIC leet_code::list_node` when the problem uses `ListNode`.)

`test/cpp/leet_code/CMakeLists.txt`:

```cmake
add_project_test(<slug_snake>_test SOURCES <slug_snake>_test.cpp LIBS leet_code::<slug_snake>)
```

## Verify (always through Pixi and presets)

```bash
pixi run cmake --preset dev-debug
pixi run cmake --build --preset dev-debug --target <slug_snake>_test     # zero warnings under -Werror and clang-tidy
pixi run ctest --preset dev-debug -R <slug_snake> --output-on-failure    # must FAIL cleanly (TDD red)
pixi run clang-format -i src/include/leet_code/<slug_snake>.hpp src/cpp/leet_code/<slug_snake>.cpp test/cpp/leet_code/<slug_snake>_test.cpp
pixi run format-cmake                                                    # gersemi
```
