# Contributing to lc_dsa_practice

This guide covers the development workflow, security policies, and dependency maintenance.

## Development workflow

1. **Environment setup**: install [Pixi](https://pixi.sh), then:

   ```bash
   pixi install
   pixi run test
   ```

2. **Pre-commit verification**: hooks enforce formatting, static analysis, secret scanning, and workflow security:

   ```bash
   pixi run pre-commit
   ```

3. **Targeted checks**:

   ```bash
   pixi run test-cpp        # GoogleTest via the dev-debug workflow preset (ASan, UBSan, clang-tidy)
   pixi run test-python     # pytest
   pixi run cov-python      # Python coverage, >= 85%
   pixi run cov-cpp         # C++ coverage, >= 85%
   pixi run cpp-tsan        # ThreadSanitizer, required for anything in src/cpp/systems
   pixi run cpp-benchmark   # Google Benchmark suite workflow (local only, never on CI)
   pixi run cpp-benchmark-build # build benchmark executable
   pixi run shm-pubsub-example  # run multi-process SPMC shared-memory camera example + verifier
   pixi run check-traceability  # systems specs (docs/systems) <-> GoogleTest RecordProperty IDs
   pixi run typecheck
   pixi run lint
   pixi run format-check
   ```

4. **Configuration is only edited in `pyproject.toml`** (Pixi, ruff, mypy, pytest, coverage, rumdl). After editing, run
   `pixi run reorder-pyproject`; CI and pre-commit fail if the file is not sorted.

## Code conventions

- C++23, Google style (`.clang-format`), strict `.clang-tidy`. Run CMake only through presets (`cmake --workflow --preset
  <name>`), never an ad hoc configure.
- Python 3.14, single quotes, 120 columns, Google-style docstrings, fully typed (`mypy` strict).
- C++ LeetCode code lives in namespace `leet_code`; low-level code lives in namespace `systems`.
- New problems start in a TDD-red state: stubs compile and tests assert the correct answers.
- Systems code follows `.agents/plugins/systems/skills/systems-setup/references/safety.md`: no heap after initialization,
  `noexcept` APIs returning `std::expected`, no RTTI, OS calls isolated in an adapter, POSIX portability.
- Benchmarks in `benchmarks/cpp/systems` are local-only and must never run in CI. Benchmark telemetry in `data/` is strictly
  gitignored and must never be committed. Performance improvements require Bayesian BEST verification ($P(\text{better}) \ge 0.95$ with ROPE)
  with zero unit test regressions.

## Commits and pull requests

- Create a feature branch and open a pull request against `main`; CI must be green on every matrix job.
- Keep commits focused. Update `README.md`, `PROJECT.md`, and `AGENTS.md` whenever layout, tasks, or conventions change.

## GitHub Actions and security pinning

1. **Immutable commit SHAs**: every `uses:` directive specifies a 40-character commit SHA followed by a version comment:

   ```yaml
   uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
   ```

2. **Least privilege**: workflows declare the minimum `permissions:`.
3. **Pre-commit freezing**: remote hooks in `.pre-commit-config.yaml` are pinned to commit SHAs with `# frozen: vX.Y.Z`.
4. **Security linters**: workflows are validated by `actionlint` and `zizmor` locally and in CI.

## Automated dependency maintenance

Renovate (`renovate.jsonc`) opens grouped pull requests before 6:00 AM on Mondays (Pacific): `github-actions`,
`pre-commit-hooks`, and `pixi-dependencies`. A weekly `pixi-update` workflow also refreshes `pixi.lock`. Check the
Dependency Dashboard issue for pending updates and verify all CI matrix jobs before merging.
