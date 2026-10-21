# Project: LeetCode DSA Practice (C++23, Python 3.14, Systems C++) with an Antigravity Plugin

## Architecture

The project delivers an Antigravity Plugin in `.agents/plugins/leetcode` that automates the setup of LeetCode DSA practice problems in modern C++23 (CMake +
GoogleTest) or Python 3.14 (pytest); the user must name the language. The repository also hosts a `systems` C++ tree for low-level, embedded-style components,
with a second plugin in `.agents/plugins/systems` (skill only, no MCP server) that scaffolds safety-oriented systems problems from a free-form prompt. The whole
workspace (toolchain, dependencies, tasks, CI) is a single Pixi workspace defined in `pyproject.toml`.

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
        | C++23 or Python practice files|
        | C++:                          |
        | - src/include/leet_code/*.hpp |
        | - src/cpp/leet_code/*.cpp     |
        | - test/cpp/leet_code/*_test   |
        | Python:                       |
        | - src/python/leet_code/*.py   |
        | - test/python/leet_code/test_*|
        +-------------------------------+
```

## Feature Inventory

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Plugin Manifest & Config | `plugin.json` and `mcp_config.json` exposing stdio transport via `pixi run leetcode-mcp` | M2 | R1, survey_2 |
| 2 | Python Packaging | Root Pixi workspace in `pyproject.toml` (ruff, mypy, pytest, coverage, rumdl) adhering to `python-pro`; no plugin-local environment | M2 | R1, survey_2 |
| 3 | LeetCode GraphQL Client | Persistent `httpx.AsyncClient` with browser headers and exponential backoff retry | M2 | R2, survey_2 |
| 4 | Problem Identification & Slug Resolution | Resolve numeric ID, `#ID`, slug, title, or URL via `problemsetQuestionList` | M2 | R2, survey_2 |
| 5 | Problem Detail Extraction | Fetch description, C++ and Python starter snippets, test cases, constraints, tags via `getQuestionDetail` | M2 | R2, survey_2 |
| 6 | Content Transformation | Convert HTML problem description to clean Markdown using `markdownify` | M2 | R2, survey_2 |
| 7 | Error & Exception Handling | Custom exceptions for `ProblemNotFoundError`, `PremiumProblemError`, and network errors | M2 | R2, survey_2 |
| 8 | MCP Server & Tool Definition | Modern `MCPServer` (`mcp>=2.2.0`) exposing strongly-typed `get_problem` tool | M2 | R2, survey_2 |
| 9 | MCP Server Test Suite | Comprehensive unit and integration test suite with `pytest` and `pytest-asyncio` | M2 | R2, survey_2 |
| 10 | Shared C++ Data Structures | Canonical `src/include/leet_code/list_node.hpp` with RAII `ScopedLinkedList` to prevent ASan leaks | M1 | R3, survey_1, survey_3 |
| 11 | LeetCode Setup Skill Specification | `SKILL.md` with complete 7-step autonomous workflow and triggers | M3 | R3, survey_3 |
| 12 | C++23 Header Generation | Generate Doxygen header with modern C++23 interface (`[[nodiscard]]`, `std::span`, monadic types) & LeetCode wrapper | M3 | R3, survey_1, survey_3 |
| 13 | Barebones Source Generation | Generate stub `.cpp` returning dummy value with `static_cast<void>` parameter suppression under `-Werror` | M3 | R3, survey_1, survey_3 |
| 14 | 4-Tier Google Test Suite Generation | Generate 4-Tier Google Test TDD suite with non-crashing assertions (`ASSERT_NE`) | M3 | R3, survey_1, survey_3 |
| 15 | Target-based CMake Integration | Update `src/cpp/leet_code/CMakeLists.txt` and `test/cpp/leet_code/CMakeLists.txt` adhering to `cmake-pro` | M3 | R4, survey_1, survey_3 |
| 16 | LeetCode #2 Acceptance Verification | Scaffold LeetCode #2, compile with the `dev-debug` workflow preset, run GTest to verify clean failure (TDD Red) | M4 | AC, survey_3 |
| 17 | Opaque-Box E2E Test Suite | 4-Tier requirement-driven E2E test suite published with `TEST_READY.md` | E2E | Dual Track |
| 18 | Python LeetCode Track | `src/python/leet_code` with idiomatic API + `Solution` wrapper, 4-tier pytest suites (reference: `two_sum`, `lru_cache`) | M5 | Refactor |
| 19 | Systems C++ Track | `src/cpp/systems` + `src/include/systems`: header-only `BoundedFifo` (no heap, no STL containers) with GoogleTest and TSan | M5 | Refactor |
| 21 | Systems Setup Plugin | `.agents/plugins/systems`: prompt-driven `systems-setup` skill (spec, outline header, stubs, 6-tier red tests), `systems/platform.hpp`, `systems::test_support` (fork harness, shared regions, shm names, allocation counter), traceability checker | M6 | #28 |
| 20 | Pixi Infrastructure & CI | Pixi tasks, CMake presets (debug/release/ASan/UBSan/TSan/coverage), pre-commit, GitHub Actions, Renovate, Scorecard, coverage gates | M5 | Refactor |
| 22 | Systems Google Benchmark Infrastructure | `benchmarks/cpp/systems/` with `cmake/Benchmarking.cmake`, `BUILD_BENCHMARKS=OFF` default, Google Benchmark suite | M7 | R1 |
| 23 | Baseline Function & Unit Tests | `systems::flip_bitmap_unaligned_optimized` (`std::span` and pointer overloads) with unit tests and ASPICE traceability | M7 | R2 |
| 24 | Bayesian BEST + ROPE Engine & Host Tuning | SciPy/NumPy Student-t posterior analysis (`P(better)`, `P(same)`, `P(worse)`), host tuning (Darwin/Linux), Plotly reports | M7 | R3 |
| 25 | Agentic Optimization Loop Skill | `.agents/skills/optimization-loop/SKILL.md` (preloop, two-stage fast-screen/full BEST benchmark, git rollbacks, Academic Scribe) | M7 | R4 |

## Milestones

| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| E2E | E2E Testing Track | Requirement-driven test suite for plugin manifest, MCP server, skill, and build integration | None | DONE |
| M1 | Shared C++ Data Structures | Create `src/include/leet_code/list_node.hpp` with RAII `ScopedLinkedList` and conversion helpers | None | DONE |
| M2 | Python LeetCode MCP Server & Plugin | Create `.agents/plugins/leetcode/` with manifest, packaging, GraphQL client, MCP server, and tests | None | DONE |
| M3 | LeetCode Setup Skill | Create `.agents/plugins/leetcode/skills/leetcode-setup/SKILL.md` adhering to `cpp-pro` and `cmake-pro` | M1, M2 | DONE |
| M4 | Final Milestone: LeetCode #2 & E2E Pass | Scaffold LeetCode #2, update CMake, build, verify GTest clean failure; pass 100% E2E tests + Tier 5 | E2E, M3 | DONE |
| M6 | Systems Setup Plugin | Systems plugin + skill, platform header, test-support library, traceability check | M5 | IN PROGRESS |
| M5 | Multi-Language & Pixi Refactor | `src/`+`test/` split by language, Python and systems tracks, Pixi workspace, CI, plugin supports C++ and Python | M4 | DONE |
| M7 | Systems Benchmark & Bayesian Optimization | Benchmark infrastructure, baseline function, host tuner, Bayesian BEST engine, optimization loop skill | M6 | DONE |

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
    "python_snippet": "class Solution:\n    def addTwoNumbers(...)",
    "sample_test_case": "[2,4,3]\n[5,6,4]",
    "example_test_cases": ["[2,4,3]\n[5,6,4]", "[0]\n[0]", "[9,9,9,9,9,9,9]\n[9,9,9,9]"],
    "constraints": ["The number of nodes in each linked list is in the range [1, 100].", "0 <= Node.val <= 9", "..."]
  }
  ```

### 2. Shared Data Structure Contract (`leet_code::ListNode`)

- Header: `src/include/leet_code/list_node.hpp`
- Types:
  - `struct ListNode { int val; ListNode* next; ... };`
  - `class ScopedLinkedList;` (RAII container managing heap-allocated `ListNode*`)
  - `auto create_linked_list(std::span<const int> values) -> ListNode*;`
  - `auto linked_list_to_vector(const ListNode* head) -> std::vector<int>;`
  - `void free_linked_list(ListNode* head) noexcept;`

### 3. Generated Problem Files Contract

- Header: `src/include/leet_code/<slug_snake>.hpp`
  - Include guard: `#pragma once`
  - Namespace: `namespace leet_code`
  - Modern C++23 function: `[[nodiscard]] auto <slug_snake>(...) -> ...;`
  - LeetCode wrapper: `class Solution { public: auto <camelCase>(...) -> ...; };`
- Source: `src/cpp/leet_code/<slug_snake>.cpp`
  - Includes `leet_code/<slug_snake>.hpp`
  - Parameters suppressed with `static_cast<void>(param);`
  - Returns stub dummy value (`nullptr`, `{}`, `std::unexpected`)
- Tests: `test/cpp/leet_code/<slug_snake>_test.cpp`
  - 4-Tier test architecture with Google Test
  - Non-crashing assertions (`ASSERT_NE(res, nullptr)`)

### 4. CMake Registration Contract

- `src/cpp/leet_code/CMakeLists.txt`:

  ```cmake
  add_library(<slug_snake> STATIC <slug_snake>.cpp)
  add_library(leet_code::<slug_snake> ALIAS <slug_snake>)
  target_include_directories(<slug_snake> PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>)
  target_compile_features(<slug_snake> PUBLIC cxx_std_23)
  target_link_libraries(<slug_snake> PRIVATE project_options project_warnings project_sanitizers project_architecture)
  ```

- `test/cpp/leet_code/CMakeLists.txt`:

  ```cmake
  add_project_test(<slug_snake>_test SOURCES <slug_snake>_test.cpp LIBS leet_code::<slug_snake>)
  ```

### 6. Systems Problem Files Contract (`systems-setup`)

- Spec: `docs/systems/<name>.md` with headings `### <PREFIX>-REQ-NNN: Title` and a `- **Verification:** Test|Analysis|Review` line per requirement
- Header: `src/include/systems/<name>.hpp` (`#pragma once`, `namespace systems`, Doxygen file block with implementation outline, `enum class <Name>Error`
  including `kNotImplemented`, `noexcept` + `[[nodiscard]]` API returning `std::expected`, factories for fallible init)
- Stubs: `src/cpp/systems/<name>.cpp` (portable logic) and optional `<name>_posix.cpp` (the only TU allowed to include POSIX headers); stubs never block,
  allocate, or crash and return `kNotImplemented`
- Tests: `test/cpp/systems/<name>_test.cpp`, tiers functional / boundary / resource / multi-thread / multi-process / fault injection, each test starting with
  `RecordProperty("requirements", "<IDs>")`, linking `systems::test_support`
- CMake: `systems_add_library(<name> SOURCES ... LIBS systems::platform ...)` and `add_project_test(<name>_test ... LABELS "systems[;multiprocess][;fault]")`
- Check: `pixi run check-traceability`

### 5. Python Problem Files Contract

- Module: `src/python/leet_code/<slug_snake>.py` (module docstring, typed idiomatic function or class, plus `class Solution` wrapper with
  the official LeetCode method name; stubs return a clean dummy value and never raise `NotImplementedError`)
- Tests: `test/python/leet_code/test_<slug_snake>.py` (4 tiers: canonical, boundary, invariants, stress marked `@pytest.mark.stress`)
- No registration step is needed: the package is on `PYTHONPATH` through the Pixi activation environment.

### 7. Google Benchmark & Bayesian Telemetry Contract

- Benchmark target: `benchmarks/cpp/systems/bitmap_flip_benchmark.cpp` (registered via `systems_add_benchmark`)
- Registered benchmark names: `BM_FlipBitmap_Baseline/<WxH>`, `BM_FlipBitmap_Candidate/<WxH>`
- Telemetry outputs in `data/latest/<experiment>/`:
  - `benchmark_raw.json` (Google Benchmark JSON output)
  - `telemetry.json` (host metadata, git SHA, paired benchmark timing samples)
  - `analysis.json` (Bayesian BEST Student-t posterior parameters, P(better), P(same), P(worse), effect size, 95% HDI, verdict)
  - `report.md` (Markdown summary table)
  - `report.html` (interactive Plotly standalone HTML report)
- Baseline directory: `data/baseline/<experiment>/` (promoted on $P(\text{better}) \ge 0.95$ and zero test regressions)

### 8. Agentic Optimization Loop Protocol Contract

- Location: `.agents/skills/optimization-loop/SKILL.md` (with `references/` for Bayesian BEST, hardware tuning, subagents, and scribe guidelines)
- Execution phases:
  - **Preloop**: Deterministic random seed (`0x5EED`), host variance mitigation, baseline run (`data/baseline/<experiment>`), Gemini 4 Argon hypothesis queue.
  - **Iterative Loop**: Clean git check (`git status --porcelain`), base hash capture (`git rev-parse HEAD`), hypothesis implementation, static gate
    (`clang-format`, `clang-tidy`, `pre-commit`), Stage-1 Fast Screen (3 reps, abort if >5% regression), Stage-2 Full BEST benchmark (30 reps), unit test
    validation (`pixi run test-cpp`), atomic git commit + baseline promotion or clean git revert.
  - **Post Loop**: Clickable browser link (`file://.../report.html`), multi-regime anomaly verification, workspace cleanup, Academic Scribe report synthesis.

## Code Layout

- C++ LeetCode: `src/include/leet_code/`, `src/cpp/leet_code/`, `test/cpp/leet_code/`
- Systems C++: `src/include/systems/`, `src/cpp/systems/`, `test/cpp/systems/`, `examples/cpp/systems/`
- Systems Benchmarks: `benchmarks/cpp/systems/` (Google Benchmark targets, `CMakeLists.txt`)
- Python LeetCode: `src/python/leet_code/`, `test/python/leet_code/`
- Systems specs: `docs/systems/`; systems test support: `test/cpp/systems/support/`
- Repo scripts:
  - `scripts/ci/` (C++ coverage gate, pyproject sorter, systems traceability check)
  - `scripts/benchmarks/` (`run_benchmark.py`, `bayesian_compare.py`, `host_tuner.py`)
  - `scripts/examples/` (`run_shm_pubsub_example.py`)
- Python tests: `test/python/scripts/` (traceability, coverage, benchmark runner, and Bayesian tests)
- Skills & Plugins:
  - `.agents/plugins/leetcode/` (LeetCode MCP server + `leetcode-setup` skill)
  - `.agents/plugins/systems/` (`systems-setup` skill plus references)
  - `.agents/skills/optimization-loop/` (`SKILL.md` plus `references/` for Bayesian BEST, hardware tuning, subagent coordination, and scribe guidelines)
- Benchmark Telemetry: `data/` (local telemetry and reports, strictly gitignored)
- Infrastructure: `pyproject.toml` (Pixi), `CMakePresets.json`, `cmake/`, `.pre-commit-config.yaml`, `.github/workflows/`, `renovate.jsonc`
- Multi-Agent Metadata:
  - `.agents/teamwork/`
