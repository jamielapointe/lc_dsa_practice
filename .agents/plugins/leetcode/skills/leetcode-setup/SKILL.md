---
name: leetcode-setup
description: Automates setting up LeetCode DSA practice problems in modern C++23 with CMake and Google Test. Fetches problem descriptions and metadata via the Python LeetCode MCP server (tool get_problem), generates Doxygen-documented headers, stub source implementations that compile cleanly under -Werror, comprehensive 4-tier Google Test suites, and integrates targets into CMakeLists.txt.
---

# LeetCode Setup Skill

## 🔗 Related Skills
- **cpp-pro**: Modern C++23 standards, RAII, memory safety, and strict compiler conformance.
- **cmake-pro**: Target-based CMake 4.4.2+, modular architecture, and CTest integration.
- **python-pro**: Python tooling and MCP server execution standards.

---

## Use this skill when
- The user requests to "set up", "initialize", "add", or "scaffold" a LeetCode problem (e.g., "Set up LeetCode #2", "Scaffold LeetCode add-two-numbers").
- A developer wants to start the Test-Driven Development (TDD) cycle for a LeetCode problem in modern C++23.
- Generating modern C++23 problem scaffolding with LeetCode `Solution` class compatibility wrapper.

## Do not use this skill when
- Implementing the actual algorithmic solution (this skill strictly produces barebones stubs returning dummy values to establish the TDD Red state).
- Working on non-LeetCode C++ components or general project configuration.

---

## Autonomous Execution Workflow

### Step 1: Query Problem Data via MCP Server
Invoke the LeetCode MCP server tool `get_problem`:
```json
{
  "tool": "get_problem",
  "arguments": {
    "problem_query": "<problem_number_or_slug>"
  }
}
```
The tool accepts numeric ID (e.g. `2`, `#2`), URL slug (`add-two-numbers`), title, or full URL.
It returns a `ProblemDetails` object:
- `frontend_id`: Problem number (e.g. `2`)
- `question_id`: Internal question ID
- `title`: Problem title (e.g. `"Add Two Numbers"`)
- `slug`: URL slug (e.g. `"add-two-numbers"`)
- `difficulty`: `"Easy"`, `"Medium"`, or `"Hard"`
- `description_markdown`: Clean Markdown converted from problem description
- `cpp_snippet`: Official LeetCode C++ starter code snippet
- `sample_test_case`: Raw default sample test case string
- `example_test_cases`: Parsed example input test cases
- `constraints`: Array of constraint strings preserving math notation (e.g. `10^4`)
- `topic_tags`: Array of associated topic tags

Derive naming conventions:
- `slug_snake` = slug with hyphens replaced by underscores (e.g. `add_two_numbers`)
- Header: `include/lc_dsa/<slug_snake>.hpp`
- Source: `src/<slug_snake>.cpp`
- Test: `tests/<slug_snake>_test.cpp`
- Library Target: `<slug_snake>` (aliased as `lc_dsa::<slug_snake>`)
- Test Target: `<slug_snake>_test`

---

### Step 2: Shared Data Structure Resolution
Analyze the official C++ code snippet:
- If the signature references `ListNode`:
  - Check if `include/lc_dsa/list_node.hpp` exists.
  - `#include "lc_dsa/list_node.hpp"` in both header and test files.
  - Leverage `lc_dsa::ListNode`, `lc_dsa::ScopedLinkedList` (RAII container to prevent AddressSanitizer leaks), `lc_dsa::create_linked_list`, `lc_dsa::linked_list_to_vector`, and `lc_dsa::free_linked_list`.
- If the signature references other shared data structures (e.g. `TreeNode`), ensure canonical definitions in `include/lc_dsa/`.

---

### Step 3: Generate C++23 Header (`include/lc_dsa/<slug_snake>.hpp`)
Requirements:
1. Include guard: `#pragma once`.
2. Include necessary standard headers (`<cstddef>`, `<span>`, `<vector>`, etc.) and `lc_dsa/list_node.hpp` if needed.
3. Open namespace `namespace lc_dsa { ... }`.
4. Comprehensive Doxygen comments:
   - `@file <slug_snake>.hpp`
   - `@brief` summary, problem number, title, difficulty, URL.
   - Include the full problem description with examples by explicitly fetching `description_markdown` from the MCP response.
   - **CRITICAL:** Use C++ line comments (`///`) instead of block comments (`/* ... */`) for the header comments, because the markdown may contain nested block comments (`/* ... */`) which would break compilation.
   - Algorithmic complexity targets ($O(N)$ time, $O(1)$ or $O(N)$ auxiliary space).
   - Verbatim problem constraints.
   - Ownership notes (e.g., if returning `ListNode*`, document that caller assumes ownership).
5. Declare two interfaces:
   - **Idiomatic Modern C++23 Interface**:
     - `[[nodiscard]] auto <slug_snake>(...) -> ...;`
     - Uses non-owning views (`std::span`), value semantics, or monadic types where applicable.
   - **LeetCode Compatibility Wrapper**:
     - `class Solution { public: auto <camelCaseMethod>(...) -> ...; };`
     - Conforms verbatim to LeetCode's class method signature, delegating to the modern function.

---

### Step 4: Generate Barebones Source (`src/<slug_snake>.cpp`)
Requirements:
1. Include the corresponding header `#include "lc_dsa/<slug_snake>.hpp"`.
2. Open namespace `namespace lc_dsa { ... }`.
3. Parameter Suppression under `-Werror`:
   - Enforce `-Wunused-parameter` compliance by suppressing all input parameters:
     ```cpp
     static_cast<void>(arg1);
     static_cast<void>(arg2);
     ```
4. Return a clean dummy / stub value:
   - Pointer types (`ListNode*`): `return nullptr;`
   - Numeric types: `return 0;`
   - Boolean types: `return false;`
   - Container types (`std::vector<int>`): `return {};`
   - Monadic types (`std::expected`): `return std::unexpected{"Not implemented"};`
5. Implement the `Solution` wrapper delegating directly to `<slug_snake>(...)`.

---

### Step 5: Generate Comprehensive 4-Tier Google Test Suite (`tests/<slug_snake>_test.cpp`)
Requirements:
1. Include `#include "lc_dsa/<slug_snake>.hpp"`, `#include <gtest/gtest.h>`, and standard containers.
2. If using linked lists, include `#include "lc_dsa/list_node.hpp"`.
3. Place tests in an anonymous namespace `namespace { ... }`.
4. Structure into the 4-Tier Architecture:
   - **Tier 1: Canonical Feature Coverage**:
     - Tests for all official LeetCode example test cases.
     - Validate both modern function and `Solution` wrapper.
     - **CRITICAL:** Tests MUST assert the **ACTUAL CORRECT ANSWER** from the problem description (e.g., `EXPECT_EQ(result, 1);`). Do NOT assert the dummy stub's return value (e.g. `EXPECT_EQ(result, 0);`) just to make the test pass. The point of TDD Red State is that these tests SHOULD FAIL until the user implements the algorithm.
   - **Tier 2: Boundary, Corner & Edge Cases**:
     - Minimum length inputs (e.g. single node lists, empty containers).
     - Carries propagating across digits, unequal lengths, maximum digit values (`9 + 9`).
     - Zero elements (`[0] + [0]`).
   - **Tier 3: Sanitizer & Memory Invariants**:
     - Zero memory leaks under AddressSanitizer (`-fsanitize=address`).
     - Always wrap heap-allocated nodes with `lc_dsa::ScopedLinkedList` or ensure explicit deallocation.
   - **Tier 4: Stress Workloads & OJ Simulation**:
     - Maximum constraint test (e.g. 100-node lists).
     - Parameterized test loop with `struct TestCase`.
     - Wrap stress tests in a `std::chrono` execution timer (e.g., `EXPECT_LT(duration, std::chrono::milliseconds(200))`) to catch $O(N^2)$ solutions.
     - **CRITICAL:** Programmatically generate massive boundary inputs (e.g., using `std::generate`, `std::iota`, or nested `for` loops) rather than writing out massive literal arrays which blow up the context window.
5. Non-Crashing Assertions for Dummy Return (TDD Red State):
   - Because the dummy stub returns `nullptr`, tests MUST NOT dereference the result without verification.
   - Use `ASSERT_NE(result, nullptr);` before accessing members, or use `lc_dsa::linked_list_to_vector(result)` which safely handles `nullptr` by returning an empty vector `{}`.
   - This ensures Google Test fails with clean diagnostic assertion failures rather than crashing with `SIGSEGV` or ASan null pointer exceptions.

---

### Step 6: Update CMake Build System
Adhering strictly to `cmake-pro` target-based design:
1. In `src/CMakeLists.txt`, append the static library target:
   ```cmake
   add_library(<slug_snake> STATIC <slug_snake>.cpp)
   add_library(lc_dsa::<slug_snake> ALIAS <slug_snake>)

   target_include_directories(
     <slug_snake>
     PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>)

   target_compile_features(<slug_snake> PUBLIC cxx_std_23)

   target_link_libraries(
     <slug_snake>
     PRIVATE project_options project_warnings project_sanitizers project_architecture)
   ```
2. In `tests/CMakeLists.txt`, append the test target using the modular `add_project_test` helper:
   ```cmake
   add_project_test(
     <slug_snake>_test
     SOURCES <slug_snake>_test.cpp
     LIBS lc_dsa::<slug_snake>)
   ```
   (If the problem uses `ListNode`, ensure `lc_dsa::list_node` is accessible or linked).

---

### Step 7: Compilation & Test Verification
1. Build the new test target:
   ```bash
   cmake --build --preset dev-debug --target <slug_snake>_test
   ```
   Verify compilation succeeds with exit code 0 and ZERO warnings under `-Werror`.
2. Run CTest:
   ```bash
   ctest --preset dev-debug -R <slug_snake>_test --output-on-failure
   ```
   Verify tests execute and fail cleanly on assertion checks (confirming TDD Red state).
3. Validate code formatting:
   ```bash
   clang-format -i include/lc_dsa/<slug_snake>.hpp src/<slug_snake>.cpp tests/<slug_snake>_test.cpp
   ```
