# lc_dsa_practice

[![CI][ci-badge]][ci]
[![OpenSSF Scorecard][scorecard-badge]][scorecard]
[![C++23][cpp-badge]][cpp]
[![Python 3.15][python-badge]][python]
[![Pixi][pixi-badge]][pixi]
[![Ruff][ruff-badge]][ruff]
[![basedpyright: strict][basedpyright-badge]][basedpyright]
[![ty][ty-badge]][ty]
[![Code style: clang-format (Google)][clang-format-badge]][clang-format]
[![Coverage gate: 85%][coverage-badge]][ci]
[![Platforms][platform-badge]][ci]
[![prek][prek-badge]][prek]

LeetCode data-structure and algorithm practice in **C++23**, **ISO C23**, **Python 3.15**, and **Rust 1.100+** (Edition 2024), plus a **systems /
embedded-style C++23, ISO C23, and Rust** tree for low-level components and **Crubit interop**. Everything (CMake 4.5+, LLVM/Clang 23.1.5+,
`CMakePresets.json` v11, dependencies, tasks, CI) is managed with [Pixi](https://pixi.sh) (`>= 0.84.2`) and conda-forge.

## Tracks

| Track | Sources | Tests | Namespace / package |
| :--- | :--- | :--- | :--- |
| C++ LeetCode | `src/cpp/leet_code`, `src/include/leet_code` | `test/cpp/leet_code` | `leet_code` (`leet_code::<slug>` CMake aliases) |
| C LeetCode | `src/c/leet_code`, `src/include/leet_code` | `test/c/leet_code` | `leet_code_c` (`leet_code_c::<slug>` CMake aliases) |
| Python LeetCode | `src/python/leet_code` | `test/python/leet_code` | `leet_code` |
| Rust LeetCode | `src/rust/leet_code` | `test/rust/leet_code` | crate `leet_code` |
| Systems C++, C23 & Rust | `src/{cpp,c,rust}/systems`, `src/include/systems`, `examples/{cpp,c}/systems`, `benchmarks/{cpp,c}/systems` | `test/{cpp,c,rust}/systems` | `systems` (`systems::<name>`) / `systems_c` (`systems_c::<name>`) / `systems` |
| Rust Systems Crash Course | `src/rust/crash_course` (isolated sub-workspaces) | per-exercise test suites | standalone crates |
| Crubit C++23 <-> Rust 2024 Interop | `src/{include,cpp,rust}/interop/crubit`, `examples/interop/crubit`, `docs/interop/crubit.md`, `scripts/crubit/` | `test/{cpp,rust}/interop/crubit` | `interop::crubit` / `rs_*` / `cc_*` |

The systems track holds low-level building blocks written in an embedded-friendly style across modern C++23, strict ISO C23, and idiomatic Rust 1.100+ (fixed
capacity, no steady-state heap, no STL containers, explicit concurrency). It includes `BoundedFifo` / `bounded_fifo`, `ShmPubSub` (`ShmChannel` / `shm_channel`,
wait-free SPMS shared-memory pub/sub), `ProcessRendezvous` / `process_rendezvous`, `bitmap_flip`, `bit_pattern`, `huffman_tree`, and `priority_scheduler`, plus
`systems/platform.{hpp,h}` (OS capability flags) and a GoogleTest support library for multi-thread, multi-process, and fault-injection tests.

## Quickstart

Prerequisites: Linux (x86-64 or aarch64) or macOS (Apple Silicon) and [Pixi](https://pixi.sh) `>= 0.84.2`.

```bash
git clone git@github.com:jamielapointe/lc_dsa_practice.git
cd lc_dsa_practice
pixi install
pixi run test          # C++ (dev-debug preset) + Python + Rust tests
pixi run pre-commit    # every formatter, linter, and security hook
```

## Task catalog

All operations go through `pixi run <task>`; nothing needs a global install.

```bash
# Tests
pixi run test                 # C++, Python, and Rust suites
pixi run test-cpp             # cmake --workflow --preset dev-debug (ASan + UBSan + clang-tidy)
pixi run test-python          # pytest 9+ with hypothesis and pytest-randomly (live-network tests deselected)
pixi run test-rust            # cargo test --workspace --all-targets
pixi run clippy               # cargo clippy --all-targets -- -D warnings
pixi run cov-rust             # cargo-llvm-cov line coverage, gate >= 85%

# C++ presets (CMake 4.5+, Schema v11)
pixi run cpp-debug            # configure, build, test (dev-debug: ASan + UBSan + clang-tidy)
pixi run cpp-ci-debug         # CI debug workflow (ci-debug: ASan + UBSan, clang-tidy off)
pixi run cpp-release          # dev-release (IPO + SPDX 3.0.1 SBOM)
pixi run cpp-asan             # AddressSanitizer
pixi run cpp-ubsan            # UndefinedBehaviorSanitizer
pixi run cpp-tsan             # ThreadSanitizer
pixi run cpp-benchmark        # Google Benchmark suite workflow
pixi run cpp-benchmark-build  # build benchmark executable
pixi run shm-pubsub-example   # multi-process SPMC shared-memory camera example + CRC-32 log verifier
pixi run cov-cpp              # llvm-cov line coverage, gate >= 85%
pixi run cov-python           # pytest-cov, gate >= 85%
# Air-gapped & cross presets (inherit `base`): `sysroot-release`, `cross-aarch64`, `cross-riscv64`
# via `pixi run cmake --preset <name>` / `cmake --install build/dev-release` (CPS `PACKAGE_INFO` + SPDX 3.0.1 `SBOM`)

# Crubit C++23 <-> Rust 2024 Interop
pixi run crubit-bootstrap     # clone pinned Crubit, verify Rust 1.100.0 sysroot, build generators
pixi run test-interop         # configure, build, and test interop suite (interop-debug)
pixi run interop-release      # configure, build, and test interop suite with LTO (interop-release)
pixi run examples-interop     # build C++23 and Rust 2024 interop showcase demos
pixi run clippy-interop       # cargo clippy (-D warnings) across interop Rust crates

# Quality
pixi run lint                 # ruff check (23 rule families, preview rules)
pixi run lint-fix             # ruff check --fix
pixi run format               # ruff format
pixi run format-check         # ruff format --check
pixi run format-toml          # tombi format (pixi.toml, pyproject.toml)
pixi run format-toml-check    # tombi format --check
pixi run format-cmake         # gersemi -i
pixi run format-cpp           # clang-format via the CMake `format` target
pixi run typecheck            # basedpyright (strict) && ty check
pixi run audit                # deptry && pip-audit
pixi run notebook-check       # marimo check --strict on notebooks/
pixi run lint-md              # rumdl
pixi run pre-commit           # prek over the whole tree
```

## Project layout

```text
.
├── AGENTS.md                      # Agent guide (read this first when automating)
├── CMakeLists.txt, CMakePresets.json, cmake/ # CMake 4.5+ (CPS PACKAGE_INFO + SPDX 3.0.1 SBOM), Schema v11 presets, toolchains/
├── pixi.toml, pyproject.toml, pixi.lock # Standalone Pixi workspace + PEP 621/735 pyproject + single lockfile
├── notebooks/
│   └── benchmark_telemetry_explorer.py # Reactive Marimo (0.25.2+) Bayesian BEST telemetry explorer
├── src/
│   ├── include/{leet_code,systems,interop/crubit}/ # public C (.h) and C++ (.hpp) headers
│   ├── cpp/{leet_code,systems,interop/crubit}/     # C++23 sources and per-track CMakeLists.txt
│   ├── c/{leet_code,systems}/                      # ISO C23 sources and per-track CMakeLists.txt
│   ├── rust/{crash_course,leet_code,systems,interop/crubit}/ # Rust crates, crash_course, and Crubit crates
│   └── python/leet_code/                           # Python package (typed)
├── test/
│   ├── cpp/{leet_code,systems,interop/crubit}/     # C++ GoogleTest and Crubit interop suites
│   ├── c/{leet_code,systems}/                      # C23 GoogleTest (*_c_test.cpp) + pure C23 (*_c23_test.c) suites
│   ├── rust/{leet_code,systems,interop/crubit}/    # Rust integration test suites
│   └── python/{leet_code,scripts}/                 # pytest suites
├── examples/
│   ├── cpp/systems/                                # standalone multi-process C++ systems examples
│   ├── c/systems/                                  # standalone multi-process C23 systems examples
│   └── interop/crubit/                             # C++23 and Rust 2024 Crubit showcase demos
├── benchmarks/
│   ├── cpp/systems/                                # Google Benchmark suites for C++ systems components
│   └── c/systems/                                  # Google Benchmark suites for C23 systems components
├── docs/
│   ├── interop/crubit.md                           # Google Crubit architecture, type matrix, and showcase guide
│   └── systems/                                    # requirements specs for systems problems
├── scripts/
│   ├── benchmarks/                                 # cyclopts + structlog + polars + arviz-stats Bayesian BEST scripts
│   ├── ci/                                         # coverage gate and systems traceability check
│   ├── crubit/                                     # hermetic Crubit bootstrapper and rs_bindings_from_cc wrapper
│   └── examples/                                   # multi-process and Crubit example runners and log verifiers
├── .agents/
│   ├── plugins/{leetcode,systems}/    # track plugins and skills
│   └── skills/{optimization-loop,rust-systems-tdd-coach}/ # workspace skills & references
└── .github/workflows/             # ci, pixi-update, scorecard
```

## Adding a LeetCode problem

Use the `leetcode` agent plugin and **always name the language**, for example "Set up LeetCode #1 in Python" or "Set up
LeetCode #2 in C++". It fetches the problem, generates documented stubs and a 4-tier test suite that starts red, and
verifies the build. See [`.agents/plugins/leetcode/README.md`](.agents/plugins/leetcode/README.md) and
[`AGENTS.md`](AGENTS.md) for the file contracts.

## Adding a systems problem

Use the `systems` agent plugin and describe the problem in prose, for example "Set up a systems problem: a pub/sub interface
over a shared-memory ring buffer, safe across processes and threads". There is no lookup: the agent asks about choices that
change the API, then writes a requirements spec (`docs/systems/<name>.md`, traceable IDs), a documented header with an
implementation outline, stubs, and a red GoogleTest suite (threads, processes, SIGKILL fault injection). Constraints follow
automotive / robotics practice: no heap after initialization, no exceptions or RTTI, POSIX portability (Linux, QNX). See
[`.agents/plugins/systems/README.md`](.agents/plugins/systems/README.md).

## Optimizing a systems component

Use the workspace `optimization-loop` skill (`.agents/skills/optimization-loop/SKILL.md`) to systematically optimize
a systems kernel (such as `systems::flip_bitmap_unaligned_optimized`). The agentic workflow uses Gemini 4 Argon (High Thinking)
in Google Antigravity 2.24.0:

1. **Preloop**: Establishes a deterministic baseline (`data/baseline/`) with reproducible random seeding (`0x5EED`) and host stabilization.
2. **Two-Stage Performance Testing**: Evaluates candidate hypotheses through a fast 3-repetition screen (aborting if >5% slower),
   followed by a high-confidence 30-repetition Google Benchmark run analyzed via Kruschke's Bayesian Estimation Supersedes the
   t-test (BEST) with a Region of Practical Equivalence (ROPE = 2%).
3. **Verification & Promotion**: Promotes candidate to baseline only when $P(\text{better}) \ge 0.95$, unit tests pass 100%,
   and all pre-commit hooks pass cleanly.
4. **Academic & Reactive Reporting**: Generates interactive Plotly HTML reports (`report.html`), publication-grade empirical analysis
   via the Academic Scribe Agent, and reactive telemetry exploration via `pixi run marimo edit notebooks/benchmark_telemetry_explorer.py`
   (`/marimo-pro`, `/marimo-pair`).

## Practicing Rust systems engineering

Use the workspace `rust-systems-tdd-coach` skill (`.agents/skills/rust-systems-tdd-coach/SKILL.md`) for an intensive, TDD-driven
Rust crash course and senior interview preparation workflow tailored for veteran C/C++ systems architects:

- **Dual Operating Modes**: Proposes and scaffolds progressive micro-projects across 10 curriculum modules (from destructive
  moves to lock-free atomics and C++ FFI) in Autonomous Curriculum Mode, or scaffolds custom data structures in Custom Task Mode.
- **Sub-Workspace Isolation**: Generates exercises under `src/rust/crash_course/<exercise_slug>/` as isolated sub-workspaces
  excluded from the root `Cargo.toml`, ensuring active red-phase `todo!()` stubs never break root CI gates.
- **Strict Anti-Solution-Leakage Gate**: Stubs methods exclusively with `todo!()` and verifies the red state (`cargo test --no-run` ->
  `cargo clippy --no-deps` -> `cargo test` confirming failure strictly on `todo!()` panics) before yielding control to the learner.
- **Interview-Grade Critique**: Evaluates completed implementations against a 4-pillar rubric (idiomatic Rust/Edition 2024,
  zero-cost abstractions, memory safety/Miri, algorithmic complexity).

## Quality gates

- **C++23 & ISO C23**: CMake 4.5+ (`CMD_NON_TARGET_DIRECTIVE`, `FILE_SET HEADERS`/`SOURCES`, CPS `PACKAGE_INFO`, SPDX 3.0.1 `SBOM`), LLVM/Clang 23.1.5+,
  clang-format (Google), clang-tidy (`WarningsAsErrors: "*"`), pure `-std=c23` ABI/feature checks (`systems_c23_test`), ASan, UBSan, TSan, 85% line coverage.
- **Rust**: Rust 1.100+ (Edition 2024, `resolver = "3"`), `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings` (`unsafe_op_in_unsafe_fn` and
  `undocumented_unsafe_blocks` denied), `cargo test --workspace`, 85% line coverage (`cov-rust`), and bidirectional Crubit interop (`test-interop`,
  `interop-release`, `clippy-interop`).
- **Python**: Python 3.15 (`PEP 649` native annotations, `TID251` bans `from __future__ import annotations`), ruff (23 families, 88 cols, double quotes,
  `RUF201` full rule names), `basedpyright` strict + `ty`, pytest 9+ (`hypothesis`, `pytest-randomly`) with 85% coverage on `src/python/leet_code`,
  `deptry` + `pip-audit` (`audit`), and reactive Marimo (`notebook-check`).
- **Repo**: gersemi, tombi (`format-toml-check`), prek, rumdl, gitleaks, actionlint, zizmor, Renovate config validation.

Contributor workflow and supply-chain rules are in [`CONTRIBUTING.md`](CONTRIBUTING.md).

[ci]: https://github.com/jamielapointe/lc_dsa_practice/actions/workflows/ci.yml
[ci-badge]: https://github.com/jamielapointe/lc_dsa_practice/actions/workflows/ci.yml/badge.svg?branch=main
[scorecard]: https://scorecard.dev/viewer/?uri=github.com/jamielapointe/lc_dsa_practice
[scorecard-badge]: https://api.securityscorecards.dev/projects/github.com/jamielapointe/lc_dsa_practice/badge
[cpp]: https://en.cppreference.com/w/cpp/23
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-23-00599C?logo=c%2B%2B&logoColor=white
[python]: https://docs.python.org/3.15/
[python-badge]: https://img.shields.io/badge/Python-3.15-3776AB?logo=python&logoColor=white
[pixi]: https://pixi.sh
[pixi-badge]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/prefix-dev/pixi/main/assets/badge/v0.json
[ruff]: https://github.com/astral-sh/ruff
[ruff-badge]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/astral-sh/ruff/main/assets/badge/v2.json
[basedpyright]: https://docs.basedpyright.com/
[basedpyright-badge]: https://img.shields.io/badge/basedpyright-strict-2A6DB2?logo=python&logoColor=white
[ty]: https://github.com/astral-sh/ty
[ty-badge]: https://img.shields.io/badge/ty-checked-4B8BBE?logo=python&logoColor=white
[clang-format]: https://clang.llvm.org/docs/ClangFormat.html
[clang-format-badge]: https://img.shields.io/badge/clang--format-Google-262D3A?logo=llvm&logoColor=white
[coverage-badge]: https://img.shields.io/badge/coverage%20gate-%E2%89%A585%25-brightgreen
[platform-badge]: https://img.shields.io/badge/platforms-linux--64%20%7C%20linux--aarch64%20%7C%20osx--arm64-lightgrey
[prek]: https://github.com/j178/prek
[prek-badge]: https://img.shields.io/badge/prek-enabled-brightgreen
