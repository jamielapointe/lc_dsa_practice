# AGENTS.md

Guidance for AI agents in `lc_dsa_practice`. Humans: see [README.md](README.md) and [CONTRIBUTING.md](CONTRIBUTING.md).

## Project overview & tracks

| Track | Purpose | Sources | Tests | Namespace / Crate |
| :--- | :--- | :--- | :--- | :--- |
| Common C++23 | Shared header-only C++23 utilities (`common::common`) | `src/cpp/common/`, `src/include/common/` | `test/cpp/common/` | `common` |
| C++ LeetCode | DSA problems in modern C++23 | `src/cpp/leet_code/`, `src/include/leet_code/` | `test/cpp/leet_code/` | `leet_code` |
| C LeetCode | ISO C23 LeetCode (scaffolded; 0 problems) | `src/c/leet_code/`, `src/include/leet_code/` | `test/c/leet_code/` | `leet_code_c` |
| Python LeetCode | DSA problems in typed Python 3.15 | `src/python/leet_code/` | `test/python/leet_code/` | `leet_code` |
| Rust LeetCode | DSA problems in Rust 1.100+ (Ed. 2024) | `src/rust/leet_code/` | `test/rust/leet_code/` | `leet_code` |
| Systems C++, C23 & Rust | Portable low-level/embedded components (fixed capacity, zero steady-state heap) | `src/{cpp,c,rust}/systems/`, `src/include/systems/` | `test/{cpp,c,rust}/systems/` | `systems` / `systems_c` / `systems` |
| Rust Crash Course | Isolated TDD sub-workspaces (excluded from root `Cargo.toml`) | `src/rust/crash_course/` | per-exercise | standalone |
| Crubit Interop | Bidirectional C++23 <-> Rust 2024 FFI via Google Crubit | `src/{include,cpp,rust}/interop/crubit/`, `examples/interop/crubit/` | `test/{cpp,rust}/interop/crubit/` | `interop::crubit` / `rs_*` / `cc_*` |

Top-level layout:

- Build & env: `CMakeLists.txt` (`BUILD_LEET_CODE`, `BUILD_SYSTEMS`, `BUILD_INTEROP`, `BUILD_EXAMPLES`, `BUILD_BENCHMARKS`, `INSTALL_LC_DSA`),
  `CMakePresets.json` (Schema v11), `cmake/`, `Cargo.toml` (`resolver = "3"`, Ed. 2024, `rust-version = "1.100"`), `rustfmt.toml`, `clippy.toml`,
  `.cargo/config.toml`, `pixi.toml`, `pyproject.toml`, `pixi.lock`.
- Code, docs & tooling: `src/`, `test/`, `examples/{cpp,c}/systems/`, `examples/interop/crubit/`, `benchmarks/{cpp,c}/systems/`,
  `docs/{systems/,interop/crubit.md}`, `notebooks/benchmark_telemetry_explorer.py` (Marimo `0.25.2+`), `scripts/{benchmarks,ci,crubit,examples}/`.
- Automation: `.agents/{rules/,plugins/{leetcode,systems}/,skills/{optimization-loop,rust-systems-tdd-coach}/}`,
  `.github/workflows/{ci,pixi-update,scorecard}.yml`.

Never recreate legacy `include/`, `src/*.cpp`, or `tests/` paths, and never use the legacy `lc_dsa` namespace.

## Toolchain rules (Pixi only)

- Run every tool via `pixi run` (`pixi.toml`, `requires-pixi = ">=0.84.2"`). Never run `pip install`, `uv`, global `cmake`, `cargo`, or system compilers. All
  tools (Clang 23.1.5+, CMake 4.5+, Ninja, GoogleTest, Rust 1.100+, Python 3.15, ruff, basedpyright, ty, pytest 9+, marimo, tombi, deptry, pip-audit, prek,
  gersemi, rumdl) pin in `pixi.lock`. Never hand-edit `pixi.lock`.
- Drive CMake via presets only (`build/<preset>`): `pixi run cmake --workflow --preset <name>` (avoid `ALLOW_NON_PIXI_TOOLCHAIN`). Cargo artifacts isolate to
  `build/cargo/` via `.cargo/config.toml` and `CARGO_TARGET_DIR`.
- Split TOML: Pixi workspace/deps/tasks live in `pixi.toml`; PEP 621/735 (`uv_build`) and tool configs (`ruff`, `basedpyright`, `ty`, `pytest`, `deptry`,
  `coverage`, `rumdl`, `tombi`) live in `pyproject.toml`. Run `pixi run format-toml` after TOML edits; keep order-sensitive values as strings where required.

```bash
pixi run test                                # C++ (dev-debug) + Python + Rust
pixi run test-cpp | cpp-ci-debug | cpp-release | cpp-asan | cpp-ubsan | cpp-tsan
pixi run test-python                         # pytest 9+ (live-network tests deselected)
pixi run test-rust | clippy | format-rust    # Rust workspace tests, Clippy (-D warnings), rustfmt
pixi run cov-cpp | cov-python | cov-rust     # coverage gates (>= 85%)
pixi run crubit-bootstrap | test-interop | interop-release | examples-interop | clippy-interop
pixi run cpp-benchmark | cpp-benchmark-build # local-only Google Benchmark suite
pixi run check-traceability                  # docs/systems <-> C++/C/Rust test traceability
pixi run typecheck | audit | notebook-check  # basedpyright+ty, deptry+pip-audit, marimo --strict
pixi run format-toml | format-toml-check | format-cmake | format-cpp | lint | format | lint-md
pixi run pre-commit                          # full prek gate; run before every commit
```

## Scoped rules, plugins & cross-cutting guardrails

Detailed per-track rules live in flat `.agents/rules/*.md` files (Antigravity does not auto-load subfolder `AGENTS.md` below workspace root):

| Rule file | Trigger | Scope |
| :--- | :--- | :--- |
| `.agents/rules/cpp-and-c23.md` | `glob` | Common C++23, C++23/C23 LeetCode, CMake 4.5+, Clang 23.1.5+ |
| `.agents/rules/python.md` | `glob` | Python 3.15, Ruff 23 families, basedpyright/ty, LeetCode, Marimo, coverage |
| `.agents/rules/rust.md` | `glob` | Rust 1.100+ Ed. 2024, Clippy denied lints, LeetCode, Systems Rust, exclusions |
| `.agents/rules/systems-engineering.md` | `glob` | Embedded/safety invariants, `BoundedFifo`, OS adapters, `test_support`, ASPICE, TSan |
| `.agents/rules/crubit-interop.md` | `glob` | Hermetic Crubit bootstrap, `-stdlib=libc++`, STL-free headers, 1-staticlib rule |
| `.agents/rules/benchmarks.md` | `model_decision` | Local Google Benchmark suites, Bayesian BEST + ROPE, telemetry hygiene |

Workspace plugins & skills:

- **`.agents/plugins/leetcode`** (`leetcode-setup` + `leetcode` MCP via `pixi run --quiet leetcode-mcp`): **user MUST specify language (`cpp`, `python`, or
  `rust`); ask if missing.**
- **`.agents/plugins/systems`** (`systems-setup`, no MCP): scaffolds traceable specs (`docs/systems/<name>.md`), outline/stubs, and red tests (`cpp`, `c`, or
  `rust`).
- **`.agents/skills/optimization-loop`**: C++23 systems benchmarking + Bayesian BEST + ROPE optimization loop.
- **`.agents/skills/rust-systems-tdd-coach`**: TDD Rust 1.100+ Ed. 2024 crash course under `src/rust/crash_course/`.

Always-enforced cross-cutting guardrails:

- **Local-only benchmarks**: `BUILD_BENCHMARKS` defaults to `OFF`; benchmarks MUST NEVER run on GitHub Actions CI (`.github/workflows/ci.yml`), and `data/`
  telemetry/reports are gitignored and must never be committed.
- **Root Cargo exclusions**: `src/rust/crash_course`, `src/rust/interop`, `examples/interop`, and `test/rust/interop` stay excluded from root `Cargo.toml`.
- **Scaffolding push ban**: `/leetcode-setup` and `/systems-setup` forbid `git push` so TDD-red stubs remain local.

## Quality gates & definition of done

| Area | Gate |
| :--- | :--- |
| C++23 & ISO C23 | `clang-format` (Google), `clang-tidy` (`WarningsAsErrors: "*"`), Google Integer Types, pure `-std=c23`, ASan/UBSan/TSan, >= 85% (`cov-cpp`) |
| Rust 1.100+ | `cargo fmt --check`, `cargo clippy --all-targets -- -D warnings`, `cargo test --workspace`, >= 85% (`cov-rust`) |
| Python 3.15 | `ruff` (23 preview families, `RUF201`), `basedpyright` strict + `ty`, pytest 9+ (`hypothesis`, `pytest-randomly`), `audit`, >= 85% (`src/python/leet_code` + `leetcode_mcp`) |
| Notebooks | Marimo `0.25.2+` (`pixi run notebook-check`: `marimo check --strict`, headless run, embedded pytest) |
| CMake, Docs & Hygiene | `gersemi` (100 cols), `rumdl` (160 cols GFM), `prek`, `tombi`, `gitleaks`, `actionlint`, `zizmor`, Renovate validator |

Done means `pixi run pre-commit` and `pixi run test` pass (plus `pixi run cpp-tsan` for threading/systems changes and `pixi run check-traceability` for systems
specs/tests). Fix root causes instead of suppressing; preserve untouched comments and docstrings.

## Git, GitHub & supply-chain rules

- Use `github-mcp-server` MCP tools for **all** GitHub platform actions (branches, issues, PRs, reviews, Actions). Never use `gh`.
- Local `git` handles working-tree/branch ops (`status`, `diff`, `add`, `mv`, `commit`, feature-branch `push` except scaffolding skills). Work on feature
  branches, never `main`; use `git mv` for moves.
- CI (`.github/workflows/ci.yml`) runs: `lint-and-hygiene` (`prek`, `audit`, Marimo checks/tests), `python-tests` (`cov-python`), `cpp-tests` (`cpp-release`),
  `rust-tests` (all on Linux x86-64/arm64 + macOS), `cpp-coverage`, `sanitizers` (`asan`/`ubsan`/`tsan`), `rust-coverage`, and `crubit-interop` (bootstrap,
  debug/release tests, examples, clippy on Linux x86-64 + macOS).
- Pin workflow `uses:` and remote pre-commit hooks to 40-char commit SHAs with version comments; look up SHAs via GitHub MCP, never from memory.

## Structure & rule maintenance checklist

1. Sync CMake, Cargo workspace members, `pixi.toml`, `pyproject.toml`, and CI on layout changes; run `pixi run format-toml`.
2. Sync `README.md`, `CONTRIBUTING.md`, `.agents/rules/*.md`, plugin docs/skills, and `AGENTS.md`.
3. Enforce Antigravity 2.34.0 limits: keep every rule file (`AGENTS.md` and `.agents/rules/*.md`) strictly under `24,000` bytes (after expanding any inline
   file includes; target root `AGENTS.md` <= 12 KB), keep `.agents/rules/` flat (1 level, valid YAML frontmatter), and stay lean inside the `20,000`-token
   `defaultRulesBudget`. Verify with `wc -c AGENTS.md .agents/rules/*.md` and `pixi run pytest test/python/scripts/test_agent_rules.py`.
