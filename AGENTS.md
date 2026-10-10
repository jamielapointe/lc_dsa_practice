# AGENTS.md

Guidance for AI agents working in `lc_dsa_practice`. Humans: see [README.md](README.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Project overview

A personal practice repository with three independent tracks that share one Pixi workspace, one CMake build, and one CI:

| Track | Purpose | Sources | Tests | Namespace |
| :--- | :--- | :--- | :--- | :--- |
| C++ LeetCode | Data-structure and algorithm problems in modern C++23 | `src/cpp/leet_code/`, `src/include/leet_code/` | `test/cpp/leet_code/` | `leet_code` |
| Python LeetCode | The same problems in typed Python 3.14 | `src/python/leet_code/` | `test/python/leet_code/` | package `leet_code` |
| Systems C++ | Reusable low-level, system-level, and embedded-style components (fixed capacity, no heap, no STL containers, explicit concurrency) | `src/cpp/systems/`, `src/include/systems/` | `test/cpp/systems/` | `systems` |

`systems::BoundedFifo<T, Capacity>` (header-only, mutex + condition variables, closable, MPMC) is the first systems component and
the style reference for new ones.

## Directory layout

```text
.
├── AGENTS.md, README.md, CONTRIBUTING.md, PROJECT.md
├── CMakeLists.txt, CMakePresets.json, cmake/      # options: BUILD_LEET_CODE, BUILD_SYSTEMS
├── pyproject.toml, pixi.lock                      # Pixi workspace and ALL Python tool config
├── src/
│   ├── include/{leet_code,systems}/               # public C++ headers, included as <leet_code/x.hpp>
│   ├── cpp/{leet_code,systems}/                   # .cpp files and the track's CMakeLists.txt
│   └── python/leet_code/                          # __init__.py, py.typed, one module per problem
├── test/
│   ├── cpp/{leet_code,systems}/                   # GoogleTest, one <name>_test.cpp per target
│   └── python/{leet_code,scripts}/                # pytest, test_<name>.py
├── scripts/ci/                                    # cpp_coverage.py (coverage gate), reorder_pyproject.py
├── .agents/plugins/leetcode/                      # MCP server (leetcode_mcp) + leetcode-setup skill
└── .github/workflows/                             # ci.yml, pixi-update.yml, scorecard.yml
```

Do not recreate the old top-level `include/`, `src/*.cpp`, or `tests/` paths, and do not use the old `lc_dsa` namespace.
`PROJECT.md` holds the plugin architecture and file contracts; read it before changing the plugin or generated-file formats.

## Toolchain rules (Pixi only)

- Run every tool through `pixi run`. Never `pip install`, `uv`, a global `cmake`, or a system compiler. The conda-forge
  Clang 23 toolchain, CMake 4.4, Ninja, GoogleTest, ruff, mypy, pytest, gersemi, and rumdl are pinned in `pixi.lock`.
- CMake is driven only by presets: `pixi run cmake --workflow --preset <name>`. Do not hand-configure build directories.
  Configuring with a non-Pixi compiler fails by design (`ALLOW_NON_PIXI_TOOLCHAIN` overrides; avoid it).
- Add Python or conda dependencies in `pyproject.toml` (`pixi add ...` or edit), then run `pixi run reorder-pyproject`.
  Never hand-edit `pixi.lock`.
- `pyproject.toml` is the single config file for Pixi, ruff, mypy, pytest, coverage, and rumdl. It is auto-sorted, so
  order-sensitive values must be strings, not lists (see the pytest `addopts`).

### Common tasks

```bash
pixi run test            # C++ (dev-debug workflow) + Python
pixi run test-cpp        # dev-debug: ASan + UBSan + clang-tidy, build and ctest
pixi run test-python     # pytest, live-network tests deselected
pixi run cpp-release     # IPO build + tests
pixi run cpp-asan | cpp-ubsan | cpp-tsan
pixi run cov-cpp         # llvm-cov, gate >= 85%
pixi run cov-python      # pytest-cov, gate >= 85%
pixi run lint | lint-fix | format | format-check | format-cmake | format-cpp | typecheck | lint-md
pixi run pre-commit      # the full gate; run before every commit
```

CMake presets: `dev-debug`, `dev-release`, `asan`, `ubsan`, `tsan`, `coverage` (each has configure, build, test, and
workflow variants; build trees go to `build/<preset>`).

## Track 1: C++ LeetCode

- Each problem `<slug_snake>` has `src/include/leet_code/<slug_snake>.hpp`, `src/cpp/leet_code/<slug_snake>.cpp`, and
  `test/cpp/leet_code/<slug_snake>_test.cpp`, registered in the two track `CMakeLists.txt` files.
- Header: `#pragma once`, `namespace leet_code`, Doxygen `@file`/`@brief`, a modern interface
  (`[[nodiscard]] auto f(...) -> ...`, `std::span`, `std::optional`/`std::expected`) and a LeetCode-compatible
  `class Solution` wrapper delegating to it.
- CMake registration: `add_library(<slug> STATIC <slug>.cpp)`, alias `leet_code::<slug>`, include dir
  `${PROJECT_SOURCE_DIR}/src/include`, link `project_options project_warnings project_sanitizers project_architecture`.
  Tests use `add_project_test(<slug>_test SOURCES <slug>_test.cpp LIBS leet_code::<slug>)`.
- Shared structures live in `src/include/leet_code/list_node.hpp` (`ListNode`, RAII `ScopedLinkedList`); reuse them.
- Tests have four tiers: canonical examples, boundary cases, invariants, and a max-constraint stress test. New stubs
  compile cleanly and return dummy values, so tests start red without crashing (guard pointers with `ASSERT_NE`).
- Warnings are errors and clang-tidy is strict; suppress unused parameters with `static_cast<void>(x)`.
- Wall-clock assertions are noisy under sanitizers: keep bounds generous. `NumberOfIslandsTest.MaxConstraintsGrid` and
  `LruCacheTest.StressTest` are single-threaded and excluded from the `tsan` test preset (TSan slows them past their time
  bounds); keep new wall-clock tests out of the threading signal.

## Track 2: Python LeetCode

- Modules go in `src/python/leet_code/<slug_snake>.py`; tests in `test/python/leet_code/test_<slug_snake>.py`. No
  registration is needed: `PYTHONPATH` is set by the Pixi activation (`src/python`, plugin source, repo root).
- Each module exposes an idiomatic, fully typed API (`snake_case`, Google docstrings, `ValueError` on invalid input) and the
  LeetCode wrapper `class Solution` with the official method name (suppress `invalid-function-name` on that line). Design
  problems use a `CapWords` class plus an official-name alias (see `lru_cache.py`).
- References: `two_sum.py` (function) and `lru_cache.py` (class). Tests use four tiers; the stress tier carries
  `@pytest.mark.stress` and a `time.perf_counter()` guard.
- Style: Python 3.14, single quotes, 120 columns, `from __future__ import annotations`, typing-only imports under
  `TYPE_CHECKING`, force-single-line imports, mypy strict, ruff with preview rules, no `print`, no bare `except`.
- Coverage gate: 85% on `src/python/leet_code`. Never raise `NotImplementedError` in stubs (the red state must come from
  assertions).

## Track 3: Systems / low-level / embedded-style C++

Intent: portable building blocks that could move to a small target without redesign.

- Headers in `src/include/systems/<name>.hpp` (included as `<systems/<name>.hpp>`), implementation or explicit
  instantiations in `src/cpp/systems/<name>.cpp`, tests in `test/cpp/systems/<name>_test.cpp`.
- Register with `systems_add_library(<name> SOURCES <name>.cpp [LIBS ...])` (alias `systems::<name>`); register tests with
  `add_project_test`. clang-tidy also analyzes `src/include/systems/` headers for this track.
- Style rules:
  - Compile-time capacity and storage inside the object; no `new`/`malloc`; no STL containers
    (`std::vector`, `std::queue`, `std::deque`, ...). Small value types like `std::optional`, `std::array`, `std::span` and
    `<atomic>`/`<mutex>`/`<condition_variable>` primitives are fine.
  - Prefer `constexpr`, `noexcept`, `[[nodiscard]]`, and `static_assert` for invariants; document thread-safety and blocking
    behavior in Doxygen.
  - Provide try/blocking/timed variants for blocking APIs and a `close()` path that wakes every waiter.
  - Bit-twiddling or register-style code should use fixed-width integers and `std::bit_cast`/`<bit>`, never type punning.
- Every systems component needs a four-tier test suite, including a multi-threaded stress test. Run `pixi run cpp-tsan`
  in addition to `test-cpp` before finishing.
- Deliberate NOLINTs (for example a C array backing the FIFO) must carry a one-line justification.

## LeetCode plugin (`.agents/plugins/leetcode`)

- One MCP server (`leetcode`, started with `pixi run --quiet leetcode-mcp`) exposes `get_problem(problem_query)`, returning
  statement, constraints, examples, and the starter snippets (`cpp_snippet`, `python_snippet`).
- The `leetcode-setup` skill scaffolds one problem. **The user must specify the language (`cpp` or `python`); ask if it is
  missing.** Language details are in `skills/leetcode-setup/references/cpp.md` and `references/python.md`.
- The server's own tests are in `.agents/plugins/leetcode/tests/` and run under the root pytest config
  (`pixi run test-python`). Live-network tests are marked `live` and skipped by default.
- Layout changes must be mirrored in `SKILL.md`, the reference files, the plugin README, and `PROJECT.md`.

## Quality gates

| Area | Gate |
| :--- | :--- |
| C++ format and lint | clang-format (Google), clang-tidy with `WarningsAsErrors: "*"` (enabled in `dev-debug`) |
| C++ runtime | ASan, UBSan, TSan presets; 85% line coverage (`cov-cpp`) |
| Python | `ruff check`, `ruff format`, `mypy` strict, pytest, 85% coverage |
| CMake | `gersemi` (config `.gersemirc`: 100 columns, 4-space indent); run `pixi run format-cmake` |
| Markdown | `rumdl` (160 columns) |
| Repo hygiene | gitleaks, actionlint, zizmor, Renovate validator, sorted `pyproject.toml` |

Done means `pixi run pre-commit` and `pixi run test` pass. Run `pixi run cpp-tsan` for threading changes. Fix root causes
instead of suppressing; keep existing comments and docstrings that your change does not touch.

## Git and GitHub rules

- Use the `github-mcp-server` MCP tools for **all** GitHub interactions (branches, files, issues, pull requests, reviews,
  Actions status and logs). Do not use the `gh` CLI or `git push`.
- Local `git` is for working-tree operations only: status, diff, add, mv, commit, local branches.
- Work on a feature branch, never on `main`. Use `git mv` for moves so history is preserved.
- CI (`.github/workflows/ci.yml`) runs lint/hygiene, Python tests on Linux x86-64, Linux arm64, and macOS, C++ release tests on
  the same matrix, C++ coverage, and the ASan/UBSan/TSan presets.
- Workflow `uses:` entries and remote pre-commit hooks must be pinned to full commit SHAs with a version comment. Look up
  SHAs with the GitHub MCP, never from memory.

## Checklist when changing structure

1. Update CMake registration, `pyproject.toml` path settings (pytest `testpaths`/`pythonpath`, mypy `files`, coverage
   `source`), and CI if paths change.
2. Update `README.md`, `PROJECT.md`, `CONTRIBUTING.md`, the plugin docs and skill, and this file.
3. Keep this file under 24 KB (Antigravity truncates larger rule files); check with `wc -c AGENTS.md`.
