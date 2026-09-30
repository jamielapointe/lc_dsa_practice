# Project: LeetCode DSA Practice Antigravity Plugin & C++23 Automation

## Architecture

The project delivers an Antigravity Plugin in `.agents/plugins/leetcode` that automates the setup of LeetCode DSA practice problems in modern C++23.

```text
                      +------------------------------------------+
                      |         Antigravity Agent Runtime        |
                      +------------------------------------------+
                               |                        |
         1. Activates Skill    |                        | 2. Invokes Tool
                               v                        v
        +-------------------------------+   +------------------------------------+
        | LeetCode Setup Skill          |   | Python MCP Server (leetcode-mcp)   |
        | (skills/leetcode-setup/       |   | - FastMCP / MCPServer (mcp>=2.2.0) |
        |  SKILL.md)                    |   | - LeetCode GraphQL API client      |
        +-------------------------------+   | - Pydantic models & Markdown       |
                       |                    +------------------------------------+
                       | 3. Generates                   |
                       v                                v 4. Queries (https://leetcode.com/graphql)
        +-------------------------------+
        | C++23 Practice Files & Build  |
        | - include/lc_dsa/<slug>.hpp   |
        | - include/lc_dsa/list_node.hpp|
        | - src/<slug>.cpp              |
        | - tests/<slug>_test.cpp       |
        | - src/CMakeLists.txt          |
        | - tests/CMakeLists.txt        |
        +-------------------------------+
```

## Feature Inventory

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Plugin Manifest & Config | `plugin.json` and `mcp_config.json` exposing stdio transport via `uv` | M2 | R1, survey_2 |
| 2 | Python Packaging | Isolated `pyproject.toml` with `uv`, `ruff`, `mypy`, `pytest` adhering to `python-pro` | M2 | R1, survey_2 |
| 3 | LeetCode GraphQL Client | Persistent `httpx.AsyncClient` with browser headers and exponential backoff retry | M2 | R2, survey_2 |
| 4 | Problem Identification & Slug Resolution | Resolve numeric ID, `#ID`, slug, title, or URL via `problemsetQuestionList` | M2 | R2, survey_2 |
| 5 | Problem Detail Extraction | Fetch description, C++ starter snippet, test cases, constraints, tags via `getQuestionDetail` | M2 | R2, survey_2 |
| 6 | Content Transformation | Convert HTML problem description to clean Markdown using `markdownify` | M2 | R2, survey_2 |
| 7 | Error & Exception Handling | Custom exceptions for `ProblemNotFoundError`, `PremiumProblemError`, and network errors | M2 | R2, survey_2 |
| 8 | MCP Server & Tool Definition | Modern `MCPServer` (`mcp>=2.2.0`) exposing strongly-typed `get_problem` tool | M2 | R2, survey_2 |
| 9 | MCP Server Test Suite | Comprehensive unit and integration test suite with `pytest` and `pytest-asyncio` | M2 | R2, survey_2 |
| 10 | Shared C++ Data Structures | Canonical `include/lc_dsa/list_node.hpp` with RAII `ScopedLinkedList` to prevent ASan leaks | M1 | R3, survey_1, survey_3 |
| 11 | LeetCode Setup Skill Specification | `SKILL.md` with complete 7-step autonomous workflow and triggers | M3 | R3, survey_3 |
| 12 | C++23 Header Generation | Generate Doxygen header with modern C++23 interface (`[[nodiscard]]`, `std::span`, monadic types) & LeetCode wrapper | M3 | R3, survey_1, survey_3 |
| 13 | Barebones Source Generation | Generate stub `.cpp` returning dummy value with `static_cast<void>` parameter suppression under `-Werror` | M3 | R3, survey_1, survey_3 |
| 14 | 4-Tier Google Test Suite Generation | Generate 4-Tier Google Test TDD suite with non-crashing assertions (`ASSERT_NE`) | M3 | R3, survey_1, survey_3 |
| 15 | Target-based CMake Integration | Update `src/CMakeLists.txt` and `tests/CMakeLists.txt` adhering to `cmake-pro` | M3 | R4, survey_1, survey_3 |
| 16 | LeetCode #2 Acceptance Verification | Scaffold LeetCode #2, compile with `dev-debug`, run GTest to verify clean failure (TDD Red) | M4 | AC, survey_3 |
| 17 | Opaque-Box E2E Test Suite | 4-Tier requirement-driven E2E test suite published with `TEST_READY.md` | E2E | Dual Track |

## Milestones

| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| E2E | E2E Testing Track | Requirement-driven test suite for plugin manifest, MCP server, skill, and build integration | None | DONE |
| M1 | Shared C++ Data Structures | Create `include/lc_dsa/list_node.hpp` with RAII `ScopedLinkedList` and conversion helpers | None | DONE |
| M2 | Python LeetCode MCP Server & Plugin | Create `.agents/plugins/leetcode/` with manifest, packaging, GraphQL client, MCP server, and tests | None | DONE |
| M3 | LeetCode Setup Skill | Create `.agents/plugins/leetcode/skills/leetcode-setup/SKILL.md` adhering to `cpp-pro` and `cmake-pro` | M1, M2 | DONE |
| M4 | Final Milestone: LeetCode #2 & E2E Pass | Scaffold LeetCode #2, update CMake, build, verify GTest clean failure; pass 100% E2E tests + Tier 5 | E2E, M3 | DONE |

## Interface Contracts

### 1. MCP Server Tool Contract (`leetcode_mcp` -> Agent/Skill)

- Tool Name: `get_problem`
- Input schema:

  ```json
  {
    "problem_query": "string (numeric ID, slug, URL, or title)"
  }
  ```

- Output schema (Pydantic `ProblemDetails` serialized to JSON):

  ```json
  {
    "frontend_id": 2,
    "question_id": 2,
    "title": "Add Two Numbers",
    "slug": "add-two-numbers",
    "difficulty": "Medium",
    "is_paid_only": false,
    "topic_tags": ["Linked List", "Math", "Recursion"],
    "hints": [],
    "description_markdown": "...",
    "cpp_snippet": "/** ... */ class Solution ...",
    "sample_test_case": "[2,4,3]\n[5,6,4]",
    "example_test_cases": ["[2,4,3]\n[5,6,4]", "[0]\n[0]", "[9,9,9,9,9,9,9]\n[9,9,9,9]"],
    "constraints": ["The number of nodes in each linked list is in the range [1, 100].", "0 <= Node.val <= 9", "..."]
  }
  ```

### 2. Shared Data Structure Contract (`lc_dsa::ListNode`)

- Header: `include/lc_dsa/list_node.hpp`
- Types:
  - `struct ListNode { int val; ListNode* next; ... };`
  - `class ScopedLinkedList;` (RAII container managing heap-allocated `ListNode*`)
  - `auto create_linked_list(std::span<const int> values) -> ListNode*;`
  - `auto linked_list_to_vector(const ListNode* head) -> std::vector<int>;`
  - `void free_linked_list(ListNode* head) noexcept;`

### 3. Generated Problem Files Contract

- Header: `include/lc_dsa/<slug_snake>.hpp`
  - Include guard: `#pragma once`
  - Namespace: `namespace lc_dsa`
  - Modern C++23 function: `[[nodiscard]] auto <slug_snake>(...) -> ...;`
  - LeetCode wrapper: `class Solution { public: auto <camelCase>(...) -> ...; };`
- Source: `src/<slug_snake>.cpp`
  - Includes `lc_dsa/<slug_snake>.hpp`
  - Parameters suppressed with `static_cast<void>(param);`
  - Returns stub dummy value (`nullptr`, `{}`, `std::unexpected`)
- Tests: `tests/<slug_snake>_test.cpp`
  - 4-Tier test architecture with Google Test
  - Non-crashing assertions (`ASSERT_NE(res, nullptr)`)

### 4. CMake Registration Contract

- `src/CMakeLists.txt`:

  ```cmake
  add_library(<slug_snake> STATIC <slug_snake>.cpp)
  add_library(lc_dsa::<slug_snake> ALIAS <slug_snake>)
  target_include_directories(<slug_snake> PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
  target_compile_features(<slug_snake> PUBLIC cxx_std_23)
  target_link_libraries(<slug_snake> PRIVATE project_options project_warnings project_sanitizers project_architecture)
  ```

- `tests/CMakeLists.txt`:

  ```cmake
  add_project_test(<slug_snake>_test SOURCES <slug_snake>_test.cpp LIBS lc_dsa::<slug_snake>)
  ```

## Code Layout

- Existing C++ Source & Headers:
  - `include/lc_dsa/`
  - `src/`
  - `tests/`
- Plugin & MCP Server:
  - `.agents/plugins/leetcode/plugin.json`
  - `.agents/plugins/leetcode/mcp_config.json`
  - `.agents/plugins/leetcode/pyproject.toml`
  - `.agents/plugins/leetcode/README.md`
  - `.agents/plugins/leetcode/src/leetcode_mcp/`
  - `.agents/plugins/leetcode/tests/`
  - `.agents/plugins/leetcode/skills/leetcode-setup/SKILL.md`
- Multi-Agent Metadata:
  - `.agents/teamwork/`
