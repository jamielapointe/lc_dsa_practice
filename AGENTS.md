# AGENTS.md

Guidance for AI agents working in `lc_dsa_practice`. Humans: see [README.md](README.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Project overview

A personal practice repository with independent tracks that share one Pixi workspace, unified toolchain, and CI:

| Track | Purpose | Sources | Tests | Namespace / Prefix / Crate |
| :--- | :--- | :--- | :--- | :--- |
| C++ LeetCode | Data-structure and algorithm problems in modern C++23 | `src/cpp/leet_code/`, `src/include/leet_code/` | `test/cpp/leet_code/` | `leet_code` |
| C LeetCode | Data-structure and algorithm problems in strict ISO C23 | `src/c/leet_code/`, `src/include/leet_code/` | `test/c/leet_code/` | `leet_code_c` |
| Python LeetCode | The same problems in typed Python 3.14 | `src/python/leet_code/` | `test/python/leet_code/` | package `leet_code` |
| Rust LeetCode | The same problems in idiomatic Rust 1.99.1+ | `src/rust/leet_code/` | `test/rust/leet_code/` | crate `leet_code` |
| Systems C++, C23 & Rust | Reusable low-level, system-level, and embedded-style components (fixed capacity, no heap, explicit concurrency) | `src/{cpp,c,rust}/systems/`, `src/include/systems/` | `test/{cpp,c,rust}/systems/` | `systems` / `systems_c` / `systems` |

`systems::BoundedFifo<T, Capacity>` (and its C23 and Rust counterparts) is the primary systems component and the style reference for new ones.

## Directory layout

```text
.
├── AGENTS.md, README.md, CONTRIBUTING.md
├── Cargo.toml, rustfmt.toml, clippy.toml, .cargo/config.toml # Cargo workspace config
├── CMakeLists.txt, CMakePresets.json, cmake/      # options: BUILD_LEET_CODE, BUILD_SYSTEMS, BUILD_BENCHMARKS
├── pyproject.toml, pixi.lock                      # Pixi workspace and ALL tool config
├── src/
│   ├── include/{leet_code,systems}/               # public C (.h) and C++ (.hpp) headers
│   ├── cpp/{leet_code,systems}/                   # .cpp files and the track's CMakeLists.txt
│   ├── c/{leet_code,systems}/                     # ISO C23 .c files and the track's CMakeLists.txt
│   ├── rust/{crash_course,leet_code,systems}/     # Rust crates and isolated crash_course sub-workspace
│   └── python/leet_code/                          # __init__.py, py.typed, one module per problem
├── test/
│   ├── cpp/{leet_code,systems}/                   # GoogleTest, one <name>_test.cpp per target
│   ├── c/{leet_code,systems}/                     # GoogleTest (<name>_c_test.cpp) + pure C23 (*_c23_test.c)
│   ├── rust/{leet_code,systems}/                  # Rust integration test suites (*_test.rs)
│   └── python/{leet_code,scripts}/                # pytest, test_<name>.py
├── examples/
│   ├── cpp/systems/                               # standalone multi-process C++ systems examples
│   └── c/systems/                                 # standalone multi-process C23 systems examples
├── benchmarks/
│   ├── cpp/systems/                               # Google Benchmark suites for C++ systems components
│   └── c/systems/                                 # Google Benchmark suites for C23 systems components
├── docs/systems/                                  # requirements specs for systems problems (<name>.md)
├── scripts/
│   ├── benchmarks/                                # runner and Bayesian BEST + ROPE analysis scripts
│   ├── ci/                                        # cpp_coverage.py, reorder_pyproject.py, check_systems_traceability.py
│   └── examples/                                  # run_shm_pubsub_example.py runner and log verifier
├── .agents/
│   ├── plugins/{leetcode,systems}/                # track plugins and skills
│   └── skills/{optimization-loop,rust-systems-tdd-coach}/ # workspace skills & references
└── .github/workflows/                             # ci.yml, pixi-update.yml, scorecard.yml
```

Do not recreate the old top-level `include/`, `src/*.cpp`, or `tests/` paths, and do not use the old `lc_dsa` namespace.

## Toolchain rules (Pixi only)

- Run every tool through `pixi run`. Never `pip install`, `uv`, a global `cmake`, a global `cargo`, or a system compiler. The conda-forge
  Clang 23 toolchain, CMake 4.4, Ninja, GoogleTest, rustc/cargo 1.99.1, rustfmt, clippy, ruff, mypy, pytest, gersemi, and rumdl are pinned in `pixi.lock`.
- CMake is driven only by presets: `pixi run cmake --workflow --preset <name>`. Do not hand-configure build directories.
  Configuring with a non-Pixi compiler fails by design (`ALLOW_NON_PIXI_TOOLCHAIN` overrides; avoid it).
- Cargo build artifacts are isolated to `build/cargo/` via `.cargo/config.toml` and `CARGO_TARGET_DIR`.
- Add Python or conda dependencies in `pyproject.toml` (`pixi add ...` or edit), then run `pixi run reorder-pyproject`.
  Never hand-edit `pixi.lock`.
- `pyproject.toml` is the single config file for Pixi, ruff, mypy, pytest, coverage, and rumdl. It is auto-sorted, so
  order-sensitive values must be strings, not lists (see the pytest `addopts`).

### Common tasks

```bash
pixi run test            # C++ (dev-debug workflow) + Python + Rust
pixi run test-cpp        # dev-debug: ASan + UBSan + clang-tidy, build and ctest
pixi run test-python     # pytest, live-network tests deselected
pixi run test-rust       # cargo test --workspace (all 4 tiers)
pixi run clippy | clippy-fix # cargo clippy --all-targets -- -D warnings
pixi run format-rust | format-rust-check # cargo fmt
pixi run cov-rust        # cargo-llvm-cov, gate >= 85%
pixi run cpp-release     # IPO build + tests
pixi run cpp-asan | cpp-ubsan | cpp-tsan
pixi run cpp-benchmark   # Google Benchmark suite workflow
pixi run cpp-benchmark-build # build benchmark executable
pixi run cov-cpp         # llvm-cov, gate >= 85%
pixi run cov-python      # pytest-cov, gate >= 85%
pixi run lint | lint-fix | format | format-check | format-cmake | format-cpp | typecheck | lint-md
pixi run pre-commit      # the full gate; run before every commit
```

CMake presets: `dev-debug`, `dev-release`, `asan`, `ubsan`, `tsan`, `coverage`, `benchmark` (each has configure, build, test, and
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

## Track 3: Systems / low-level / embedded-style C++23 and ISO C23

Intent: portable building blocks that could move to a small target without redesign, implemented in both modern C++23 and strict ISO C23 (`-std=c23`).

- C++23 headers in `src/include/systems/<name>.hpp`, sources in `src/cpp/systems/<name>.cpp`, tests in `test/cpp/systems/<name>_test.cpp`
  (`systems_add_library`, alias `systems::<name>`).
- ISO C23 headers in `src/include/systems/<name>.h`, sources in `src/c/systems/<name>.c`, tests in `test/c/systems/<name>_c_test.cpp` (GoogleTest via
  `extern "C"`) and `test/c/systems/systems_c23_test.c` (pure `-std=c23` CTest target) (`systems_c_add_library`, alias `systems_c::<name>`).
- clang-tidy analyzes `src/include/systems/` headers and both `src/cpp/systems/` and `src/c/systems/` translation units.
- Style rules:
  - Compile-time capacity and storage inside the object; no `new`/`malloc`; no STL containers
    (`std::vector`, `std::queue`, `std::deque`, ...). Small value types like `std::optional`, `std::array`, `std::span` and
    `<atomic>`/`<mutex>`/`<condition_variable>` (or C23 `<stdatomic.h>`, `<stdckdint.h>`, `pthread_*`) primitives are fine.
  - Prefer `constexpr`, `noexcept`, `[[nodiscard]]`, and `static_assert` for invariants; document thread-safety and blocking
    behavior in Doxygen.
  - Provide try/blocking/timed variants for blocking APIs and a `close()` path that wakes every waiter.
  - Bit-twiddling or register-style code should use fixed-width integers and `std::bit_cast`/`<bit>` (or C23 `<stdbit.h>`/`memcpy`), never type punning.
  - Public operations are `noexcept` and report errors via `std::expected<T, <Name>Error>` in C++ or `[[nodiscard]] enum <name>_error` in C23; no exceptions or
    RTTI in runtime paths. Heap is allowed only in an explicit init phase (`create()`/`open()`), never in steady-state operations.
  - OS calls (`shm_open`, `mmap`, `pthread_*attr*`, ...) live only in an adapter TU (`<name>_posix.cpp` / `<name>_posix.c`); branch on
    `systems/platform.hpp` or `systems/platform.h` capabilities (`kHasRobustMutex`, ...), not raw `#ifdef`s. Targets: Linux, QNX; macOS is a dev host.
- Every systems component needs a test suite with functional, boundary, resource (no heap after init), and multi-threaded
  stress tiers, plus multi-process and SIGKILL fault-injection tiers when it crosses processes. Reuse
  `systems::test_support` (`test/cpp/systems/support/systems_test/`: `ChildProcess`, `SharedRegion`, `unique_shm_name`,
  `AllocationScope`); never copy it. Its self-test `test_support_test` must stay green. Run `pixi run cpp-tsan` in addition
  to `test-cpp` before finishing.
- Deliberate NOLINTs (for example a C array backing the FIFO) must carry a one-line justification.
- Avoid `std::chrono` literals (`10ms`); `misc-include-cleaner` cannot attribute them. Include public POSIX headers.

## Track 4: Rust LeetCode

- Crates live in `src/rust/leet_code/` with `Cargo.toml`, `src/lib.rs`, and one module per problem `src/<slug_snake>.rs`.
- Integration tests live in `test/rust/leet_code/<slug_snake>_test.rs`.
- Each module exposes:
  - An idiomatic, safe function (`&[i32]`, `Result<T, <Name>Error>` or `Option<T>`, `#[must_use]`, arithmetic overflow protection).
  - The LeetCode compatibility wrapper `pub struct Solution;` with `impl Solution { pub fn <method>(...) -> ... }`.
- Shared structures live in `src/rust/leet_code/src/list_node.rs` (`ListNode`, `create_linked_list`, `linked_list_to_vec`).
- Tests follow the 4-tier model: canonical examples, boundary & corner cases, invariants & ownership, and stress/simulation.

## Track 5: Systems Rust

Intent: portable, safe, and deterministic low-level building blocks implemented in idiomatic Rust (edition 2024, rust-version 1.99.1).

- Crates live in `src/rust/systems/` with `Cargo.toml`, `src/lib.rs`, and components in `src/<name>.rs`.
- Integration tests live in `test/rust/systems/<name>_test.rs`.
- Style and safety rules:
  - Compile-time capacity and storage inline inside the struct using const generics (e.g., `BoundedFifo<T, const CAPACITY: usize>`).
  - Zero heap allocation in steady-state operations; no dynamic collections (`Vec`, `VecDeque`) after initialization.
  - Public operations are safe, deterministic, and report errors via `Result<T, <Name>Error>`.
  - Provide try, timed (`push_timeout`, `pop_timeout`), and blocking variants, with a `close()` path that unblocks all waiters.
  - Concurrency uses standard synchronization (`std::sync::{Mutex, Condvar}`) without `unsafe` blocks.
- Tests cover 4 tiers (canonical FIFO, boundary/timeouts/drainage, RAII/drop invariants, multi-threaded MPMC stress).
- Requirements traceability: tests declare traceability tags (`// requirements: <PREFIX>-REQ-NNN`), verified by `pixi run check-traceability`.

## Systems benchmarks and Bayesian optimization

Intent: empirical microbenchmarking and statistical performance evaluation for systems components.

- Benchmarks live in `benchmarks/{cpp,c}/systems/<name>_benchmark.cpp`, registered in `benchmarks/{cpp,c}/systems/CMakeLists.txt`.
- Built via CMake presets (`benchmark` preset or `dev-release` with IPO). CMake option `BUILD_BENCHMARKS` defaults to `OFF`.
- **Exclusion from CI**: Benchmarks are local-only and MUST NEVER run on GitHub Actions CI (`.github/workflows/ci.yml`).
- Runner and Bayesian BEST analysis scripts live in `scripts/benchmarks/` (`run_benchmark.py`, `bayesian_compare.py`, `host_tuner.py`).
- Telemetry and reports live in `data/latest/<experiment>` and `data/baseline/<experiment>`. `data/` is strictly
  gitignored and must never be committed.
- Workspace agentic optimization skill: `.agents/skills/optimization-loop/SKILL.md` (orchestrating preloop, two-stage fast-screen
  then full 30-rep BEST benchmark, unit test gate, atomic git commits, and Academic Scribe report).
- Baseline promotion requires Bayesian BEST $P(\text{better}) \ge 0.95$ with ROPE (e.g. 2%), zero unit-test regressions,
  and clean pre-commit.

## Rust systems TDD coach (`.agents/skills/rust-systems-tdd-coach`)

Intent: intensive, TDD-driven Rust crash course and senior interview preparation for veteran systems architects.

- Exercises live in `src/rust/crash_course/<exercise_slug>/` as isolated sub-workspaces.
- **Root workspace exclusion**: `src/rust/crash_course` is excluded from root `Cargo.toml` so active red-phase `todo!()`
  stubs and failing tests do not break root CI gates (`clippy`, `test-rust`, `cov-rust`).
- Enforces Rust 1.99.1 and Edition 2024 (`edition = "2024"`, `resolver = "3"` in `Cargo.toml`),
  `#![deny(unsafe_op_in_unsafe_fn)]`, and bidirectional C++/Rust interop (`cxx`, raw C ABI, `Pin<&mut T>`).
- Strict red-green-refactor TDD protocol: target methods stubbed solely with `todo!()` (anti-solution leakage); automated
  red-phase verification (`cargo test --no-run` -> `cargo clippy --no-deps` -> `cargo test` confirming failure strictly on
  `todo!()` panics); green/refactor critique mode evaluated against a 4-pillar interview rubric.

## Systems plugin (`.agents/plugins/systems`)

- No MCP server: the user's prompt is the problem statement. The `systems-setup` skill asks only about choices that change
  the API or the test oracle (including target language: `cpp`, `c`, or `rust`), records other decisions as assumptions, and generates
  `docs/systems/<name>.md` (requirements `### <PREFIX>-REQ-NNN: Title` with a `- **Verification:**` line), an outline header or module,
  stubs, build registration, and red tests.
- Each test starts with `RecordProperty("requirements", "<IDs>")` (C++) or `// requirements: <PREFIX>-REQ-NNN` (Rust);
  `pixi run check-traceability` (also a pre-commit hook) enforces requirement <-> test traceability in both directions.
- References: `skills/systems-setup/references/{spec,safety,cpp,testing,rust}.md`. Layout changes must be mirrored there, in the
  plugin README, and in this file.

## LeetCode plugin (`.agents/plugins/leetcode`)

- One MCP server (`leetcode`, started with `pixi run --quiet leetcode-mcp`) exposes `get_problem(problem_query)`, returning
  statement, constraints, examples, and starter snippets (`cpp_snippet`, `python_snippet`, `rust_snippet`).
- The `leetcode-setup` skill scaffolds one problem. **The user must specify the language (`cpp`, `python`, or `rust`); ask if it is
  missing.** Language details are in `skills/leetcode-setup/references/cpp.md`, `references/python.md`, and `references/rust.md`.
- The server's own tests are in `.agents/plugins/leetcode/tests/` and run under the root pytest config
  (`pixi run test-python`). Live-network tests are marked `live` and skipped by default.
- Layout changes must be mirrored in `SKILL.md`, the reference files, the plugin README, and this file.

## Quality gates

| Area | Gate |
| :--- | :--- |
| C++ format and lint | clang-format (Google), clang-tidy with `WarningsAsErrors: "*"` (enabled in `dev-debug`) |
| C++ runtime | ASan, UBSan, TSan presets; 85% line coverage (`cov-cpp`) |
| Python | `ruff check`, `ruff format`, `mypy` strict, pytest, 85% coverage |
| Rust format and lint | `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings` |
| Rust runtime | `cargo test --workspace` (all 4 tiers), coverage >= 85% (`cov-rust`) |
| CMake | `gersemi` (config `.gersemirc`: 100 columns, 4-space indent); run `pixi run format-cmake` |
| Markdown | `rumdl` (160 columns) |
| Repo hygiene | gitleaks, actionlint, zizmor, Renovate validator, sorted `pyproject.toml` |

Done means `pixi run pre-commit` and `pixi run test` pass. Run `pixi run cpp-tsan` for threading changes. Fix root causes
instead of suppressing; keep existing comments and docstrings that your change does not touch.

## Git and GitHub rules

- Use the `github-mcp-server` MCP tools for **all** GitHub platform interactions (branches, issues, pull requests, reviews,
  Actions status and logs). Do not use the `gh` CLI.
- Local `git` is for working-tree and branch operations: `status`, `diff`, `add`, `mv`, `commit`, local branches, and
  `push` on feature branches (note: `/leetcode-setup` and `/systems-setup` explicitly forbid `git push` so TDD-red stubs stay local).
- Work on a feature branch, never on `main`. Use `git mv` for moves so history is preserved.
- CI (`.github/workflows/ci.yml`) runs lint/hygiene, Python tests on Linux x86-64, Linux arm64, and macOS, C++ release tests on
  the same matrix, C++ coverage, the ASan/UBSan/TSan presets, Rust tests on the same matrix, and Rust coverage.
- Workflow `uses:` entries and remote pre-commit hooks must be pinned to full commit SHAs with a version comment. Look up
  SHAs with the GitHub MCP, never from memory.

## Checklist when changing structure

1. Update CMake registration, Cargo workspace members, `pyproject.toml` path settings (pytest `testpaths`/`pythonpath`, mypy `files`, coverage
   `source`), and CI if paths change.
2. Update `README.md`, `CONTRIBUTING.md`, the plugin docs and skill, and this file.
3. Keep this file under 24 KB (Antigravity truncates larger rule files); check with `wc -c AGENTS.md`.
