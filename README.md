# lc_dsa_practice

[![CI][ci-badge]][ci]
[![OpenSSF Scorecard][scorecard-badge]][scorecard]
[![C++23][cpp-badge]][cpp]
[![Python 3.14][python-badge]][python]
[![Pixi][pixi-badge]][pixi]
[![Ruff][ruff-badge]][ruff]
[![mypy: strict][mypy-badge]][mypy]
[![Code style: clang-format (Google)][clang-format-badge]][clang-format]
[![Coverage gate: 85%][coverage-badge]][ci]
[![Platforms][platform-badge]][ci]
[![pre-commit][pre-commit-badge]][pre-commit]

LeetCode data-structure and algorithm practice in **C++23** and **Python 3.14**, plus a small **systems / embedded-style
C++** tree for low-level components. Everything (toolchain, dependencies, tasks, CI) is managed with
[Pixi](https://pixi.sh) and conda-forge.

## Tracks

| Track | Sources | Tests | Namespace / package |
| :--- | :--- | :--- | :--- |
| C++ LeetCode | `src/cpp/leet_code`, `src/include/leet_code` | `test/cpp/leet_code` | `leet_code` (`leet_code::<slug>` CMake aliases) |
| Python LeetCode | `src/python/leet_code` | `test/python/leet_code` | `leet_code` |
| Systems C++ | `src/cpp/systems`, `src/include/systems` | `test/cpp/systems` | `systems` (`systems::<name>` CMake aliases) |

The systems track holds low-level building blocks written in an embedded-friendly style (fixed capacity, no heap, no STL
containers, explicit concurrency). It currently contains `systems::BoundedFifo`, a thread-safe bounded MPMC FIFO.

## Quickstart

Prerequisites: Linux (x86-64 or aarch64) or macOS (Apple Silicon) and [Pixi](https://pixi.sh) `>= 0.81`.

```bash
git clone git@github.com:jamielapointe/lc_dsa_practice.git
cd lc_dsa_practice
pixi install
pixi run test          # C++ (dev-debug preset) + Python tests
pixi run pre-commit    # every formatter, linter, and security hook
```

## Task catalog

All operations go through `pixi run <task>`; nothing needs a global install.

```bash
# Tests
pixi run test                 # C++ and Python suites
pixi run test-cpp             # cmake --workflow --preset dev-debug (ASan + UBSan + clang-tidy)
pixi run test-python          # pytest (live-network tests are deselected)

# C++ presets
pixi run cpp-debug            # configure, build, test (dev-debug)
pixi run cpp-release          # dev-release (IPO)
pixi run cpp-asan             # AddressSanitizer
pixi run cpp-ubsan            # UndefinedBehaviorSanitizer
pixi run cpp-tsan             # ThreadSanitizer
pixi run cov-cpp              # llvm-cov line coverage, gate >= 85%
pixi run cov-python           # pytest-cov, gate >= 85%

# Quality
pixi run lint                 # ruff check
pixi run lint-fix             # ruff check --fix
pixi run format               # ruff format
pixi run format-check         # ruff format --check
pixi run format-cmake         # gersemi -i
pixi run format-cpp           # clang-format via the CMake `format` target
pixi run typecheck            # mypy (strict)
pixi run lint-md              # rumdl
pixi run pre-commit           # all hooks over the whole tree
```

## Project layout

```text
.
├── AGENTS.md                      # Agent guide (read this first when automating)
├── CMakeLists.txt, CMakePresets.json, cmake/
├── pyproject.toml, pixi.lock      # Pixi workspace + ruff/mypy/pytest/coverage/rumdl config
├── src/
│   ├── include/{leet_code,systems}/   # public C++ headers
│   ├── cpp/{leet_code,systems}/       # C++ sources and per-track CMakeLists.txt
│   └── python/leet_code/              # Python package (typed)
├── test/
│   ├── cpp/{leet_code,systems}/       # GoogleTest suites
│   └── python/{leet_code,scripts}/    # pytest suites
├── scripts/ci/                    # coverage gate and pyproject sorter
├── .agents/plugins/leetcode/      # LeetCode MCP server + `leetcode-setup` skill
└── .github/workflows/             # ci, pixi-update, scorecard
```

## Adding a LeetCode problem

Use the `leetcode` agent plugin and **always name the language**, for example "Set up LeetCode #1 in Python" or "Set up
LeetCode #2 in C++". It fetches the problem, generates documented stubs and a 4-tier test suite that starts red, and
verifies the build. See [`.agents/plugins/leetcode/README.md`](.agents/plugins/leetcode/README.md) and
[`PROJECT.md`](PROJECT.md) for the file contracts.

## Quality gates

- **C++**: clang-format (Google), clang-tidy (`WarningsAsErrors: "*"`), ASan, UBSan, TSan, 85% line coverage.
- **Python**: ruff, mypy strict, pytest with an 85% coverage gate on `src/python/leet_code`.
- **Repo**: gersemi, rumdl, gitleaks, actionlint, zizmor, Renovate config validation.

Contributor workflow and supply-chain rules are in [`CONTRIBUTING.md`](CONTRIBUTING.md).

[ci]: https://github.com/jamielapointe/lc_dsa_practice/actions/workflows/ci.yml
[ci-badge]: https://github.com/jamielapointe/lc_dsa_practice/actions/workflows/ci.yml/badge.svg?branch=main
[scorecard]: https://scorecard.dev/viewer/?uri=github.com/jamielapointe/lc_dsa_practice
[scorecard-badge]: https://api.securityscorecards.dev/projects/github.com/jamielapointe/lc_dsa_practice/badge
[cpp]: https://en.cppreference.com/w/cpp/23
[cpp-badge]: https://img.shields.io/badge/C%2B%2B-23-00599C?logo=c%2B%2B&logoColor=white
[python]: https://docs.python.org/3.14/
[python-badge]: https://img.shields.io/badge/Python-3.14-3776AB?logo=python&logoColor=white
[pixi]: https://pixi.sh
[pixi-badge]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/prefix-dev/pixi/main/assets/badge/v0.json
[ruff]: https://github.com/astral-sh/ruff
[ruff-badge]: https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/astral-sh/ruff/main/assets/badge/v2.json
[mypy]: https://mypy-lang.org
[mypy-badge]: https://img.shields.io/badge/mypy-strict-2A6DB2?logo=python&logoColor=white
[clang-format]: https://clang.llvm.org/docs/ClangFormat.html
[clang-format-badge]: https://img.shields.io/badge/clang--format-Google-262D3A?logo=llvm&logoColor=white
[coverage-badge]: https://img.shields.io/badge/coverage%20gate-%E2%89%A585%25-brightgreen
[platform-badge]: https://img.shields.io/badge/platforms-linux--64%20%7C%20linux--aarch64%20%7C%20osx--arm64-lightgrey
[pre-commit]: https://github.com/pre-commit/pre-commit
[pre-commit-badge]: https://img.shields.io/badge/pre--commit-enabled-brightgreen?logo=pre-commit
