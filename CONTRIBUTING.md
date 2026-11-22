# Contributing to lc_dsa_practice

This guide covers the development workflow, security policies, and dependency maintenance.

## Development workflow

1. **Environment setup**: install [Pixi](https://pixi.sh), then:

   ```bash
   pixi install
   pixi run test
   ```

2. **Pre-commit verification**: `prek` hooks enforce formatting, static analysis, secret scanning, and workflow security:

   ```bash
   pixi run pre-commit      # runs prek over the full repository
   ```

3. **Targeted checks**:

   ```bash
   pixi run test-cpp        # GoogleTest via the dev-debug workflow preset (ASan, UBSan, clang-tidy)
   pixi run cpp-ci-debug    # GoogleTest via ci-debug workflow preset (ASan, UBSan, clang-tidy)
   pixi run test-python     # pytest 9+ (hypothesis, pytest-randomly)
   pixi run test-rust       # cargo test --workspace --all-targets
   pixi run clippy          # cargo clippy --workspace --all-targets -- -D warnings
   pixi run cov-python      # Python coverage, >= 85%
   pixi run cov-cpp         # C++ coverage, >= 85%
   pixi run cov-rust        # Rust line coverage (cargo-llvm-cov), >= 85%
   pixi run cpp-tsan        # ThreadSanitizer, required for anything in src/cpp/systems
   pixi run cpp-benchmark   # Google Benchmark suite workflow (local only, never on CI)
   pixi run cpp-benchmark-build # build benchmark executable
   pixi run shm-pubsub-example  # run multi-process SPMC shared-memory camera example + verifier
   pixi run crubit-bootstrap    # clone pinned Crubit, verify Rust 1.100.0 sysroot, build generators
   pixi run test-interop        # build and test Crubit C++23 <-> Rust 2024 interop suite (Debug)
   pixi run interop-release     # build and test Crubit C++23 <-> Rust 2024 interop suite (Release/LTO)
   pixi run examples-interop    # build C++23 and Rust 2024 Crubit showcase demo executables
   pixi run clippy-interop      # run cargo clippy (-D warnings) across interop Rust crates
   pixi run check-traceability  # systems specs (docs/systems) <-> GoogleTest RecordProperty / Rust // requirements: IDs
   pixi run typecheck           # basedpyright (strict) && ty check
   pixi run audit               # deptry && pip-audit
   pixi run notebook-check      # marimo check --strict notebooks/
   pixi run format-toml-check   # tombi format --check
   pixi run lint
   pixi run format-check
   ```

4. **Split TOML configuration**: Pixi workspace, dependencies, and tasks live in `pixi.toml` (`requires-pixi = ">=0.84.2"`); PEP 621/735 package
   metadata (`uv_build`) and tool config (`ruff`, `basedpyright`, `ty`, `pytest`, `deptry`, `coverage`, `rumdl`) live in `pyproject.toml`. Run
   `pixi run format-toml` (`tombi`) after edits; CI and `prek` fail if TOML files are unformatted.

## Code conventions

- C++23 and ISO C23 (`-std=c23`) with CMake 4.5+ (`CMD_NON_TARGET_DIRECTIVE SEND_ERROR`, `FILE_SET HEADERS` and `FILE_SET sources TYPE SOURCES`,
  CPS `PACKAGE_INFO`, SPDX 3.0.1 `SBOM`), LLVM/Clang 23.1.5+, Google style (`.clang-format`), and strict `.clang-tidy`. Drive CMake only via
  `CMakePresets.json` Schema v11 (`pixi run cmake --workflow --preset <name>` for `dev-debug`, `ci-debug`, `dev-release`, `asan`, `ubsan`, `tsan`,
  `coverage`, `benchmark`, `interop-*`; plus `sysroot-release`, `cross-aarch64`, and `cross-riscv64` configure/build presets), never an ad hoc configure.
- Rust crates target `1.100+` (`edition = "2024"`, `resolver = "3"`, `rust-version = "1.100"`; Crubit's hermetic sysroot pins `1.100.0`).
- Python 3.15 (`PEP 649` native annotations; `TID251` bans `from __future__ import annotations`; `PEP 810` `lazy import`, `PEP 814` `frozendict`,
  `PEP 661`, `PEP 750`), double quotes, 88 columns, grouped imports, Google-style docstrings, 23 Ruff families (`RUF201` full rule names), and strict
  typing via `basedpyright` + `ty`.
- Shared C++23 header-only utilities live in `src/include/common/*.hpp` (`namespace common`, CMake interface target `common::common`, linked across all C++
  tracks); C++ LeetCode code lives in namespace `leet_code`; C++ systems code lives in namespace `systems`; ISO C23 systems code lives in `src/c/systems` with
  public headers in `src/include/systems/*.h`.
- Crubit C++23 <-> Rust 2024 interop code lives under `src/{include,cpp,rust}/interop/crubit`, `examples/interop/crubit`, and `test/{cpp,rust}/interop/crubit`
  (see [`docs/interop/crubit.md`](docs/interop/crubit.md) for `-stdlib=libc++` isolation and public-header purity rules).
- New problems start in a TDD-red state: stubs compile and tests assert the correct answers.
- Systems code follows `.agents/plugins/systems/skills/systems-setup/references/safety.md`: no heap after initialization,
  `noexcept` APIs returning `std::expected` (or `[[nodiscard]] enum <name>_error` in C23), no RTTI, OS calls isolated in an adapter, POSIX portability.
- Benchmarks in `benchmarks/{cpp,c}/systems` are local-only and must never run in CI. Benchmark telemetry in `data/` is strictly
  gitignored and must never be committed. Performance improvements require Bayesian BEST verification ($P(\text{better}) \ge 0.95$ with ROPE)
  with zero unit test regressions; explore runs interactively via `pixi run marimo edit notebooks/benchmark_telemetry_explorer.py`.

## Commits and pull requests

- Create a feature branch and open a pull request against `main`; CI must be green on every matrix job.
- Keep commits focused. Update `README.md`, `CONTRIBUTING.md`, `AGENTS.md`, and `.agents/rules/*.md` whenever layout, tasks, or conventions change.

## GitHub Actions and security pinning

1. **Immutable commit SHAs**: every `uses:` directive specifies a 40-character commit SHA followed by a version comment:

   ```yaml
   uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
   ```

2. **Least privilege**: workflows declare the minimum `permissions:`.

3. **Pre-commit freezing**: remote hooks in `.pre-commit-config.yaml` (run via `prek`) are pinned to commit SHAs with `# frozen: vX.Y.Z`.

4. **Security linters**: workflows are validated by `actionlint` and `zizmor` locally and in CI.

## Automated dependency maintenance

Renovate (`renovate.jsonc`) opens grouped pull requests before 6:00 AM on Mondays (Pacific): `github-actions`,
`pre-commit-hooks`, and `pixi-dependencies`. A weekly `pixi-update` workflow also refreshes `pixi.lock`. Check the
Dependency Dashboard issue for pending updates and verify all CI matrix jobs before merging.
