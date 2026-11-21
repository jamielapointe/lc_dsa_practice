---
trigger: model_decision
description: Apply when creating, modifying, or running Google Benchmark suites, Bayesian BEST + ROPE analysis scripts, benchmark telemetry notebooks, or performance optimization experiments.
---

# Systems Benchmarks & Bayesian Optimization Rules

## Local-Only & Git Hygiene Guardrails

- **Strict CI exclusion**: benchmarks are local-only and MUST NEVER run on GitHub Actions CI (`.github/workflows/ci.yml`). Keep `BUILD_BENCHMARKS` defaulted to
  `OFF` in `CMakeLists.txt`.
- **Telemetry exclusion**: all benchmark outputs, raw JSONs, and reports live in `data/{latest,baseline}/<experiment>/`. `data/` is gitignored and must NEVER be
  staged or committed.

## Benchmark Suite & Presets

- Suites live in `benchmarks/cpp/systems/<name>_benchmark.cpp` and `benchmarks/c/systems/<name>_benchmark.cpp`, registered in their `CMakeLists.txt` via
  `add_project_benchmark` (never registered with CTest).
- Build and run through CMake presets: `pixi run cpp-benchmark`, `pixi run cpp-benchmark-build` (`benchmark` preset), or `dev-release` with IPO.
- Use deterministic random seeding (`0x5EED`) and Google Benchmark's `benchmark::DoNotOptimize` / memory clobbers to prevent dead-code elimination.

## Bayesian BEST + ROPE Analysis & Reporting

- Runner and statistical scripts in `scripts/benchmarks/` (`run_benchmark.py`, `bayesian_compare.py`, `host_tuner.py`) use `cyclopts`, `structlog`, `polars`,
  `arviz-stats`, and Plotly HTML/Markdown reporting.
- Interactive telemetry exploration lives in `notebooks/benchmark_telemetry_explorer.py` (Marimo `0.25.2+`; validate via `pixi run notebook-check`,
  `/marimo-pro`, `/marimo-pair`).
- **Baseline promotion gate**: promote a candidate optimization only when Kruschke's Bayesian BEST achieves $P(\text{better}) \ge 0.95$ with ROPE (default 2%),
  100% of unit tests pass with zero regressions, and `pixi run pre-commit` passes cleanly.
- For multi-iteration optimization loops (preloop, 3-rep fast screen, 30-rep full BEST run, atomic commits, Academic Scribe report), invoke the
  `.agents/skills/optimization-loop/SKILL.md` skill.
