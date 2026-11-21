---
trigger: glob
description: Python 3.15 coding standards, Ruff 23-family linting, basedpyright/ty typing, LeetCode track contracts, pytest/coverage gates, and Marimo notebook rules.
globs: "src/python/**/*.py, test/python/**/*.py, scripts/**/*.py, notebooks/**/*.py, .agents/plugins/leetcode/**/*.py, pyproject.toml"
---

# Python 3.15 & Marimo Rules

## Language, Style & Static Analysis

- Target **Python 3.15** (`requires-python = ">=3.15"`). Rely on `PEP 649` native deferred annotations; `TID251` bans `from __future__ import annotations`.
  Modern 3.14/3.15 features (`PEP 810` `lazy import`, `PEP 814` `frozendict`, `PEP 661`, `PEP 750`) are available.
- **Ruff** (`preview = true`, 23 rule families: `E, F, W, I, N, D, DOC, UP, B, SIM, C4, DTZ, PT, RUF, PERF, FURB, S, ANN, ASYNC, PTH, TID, TC, PL`):
  - **RUF201**: always use full rule names in `# noqa:` directives and `pyproject.toml` ignore lists (never raw rule codes).
  - Double quotes, 88 columns (`ruff format`), grouped absolute imports (`ban-relative-imports = "all"`, `lines-after-imports = 2`), typing-only imports under
    `if TYPE_CHECKING:`.
  - Google-style docstrings (`D`, `DOC`), no `print` statements, no bare `except`.
- **Dual strict typecheck**: both `basedpyright` (`typeCheckingMode = "strict"`, `reportImplicitOverride = true`) and `ty check` must pass via
  `pixi run typecheck`.
- **Dependency & vulnerability audit**: `pixi run audit` (`deptry . && pip-audit --skip-editable`) must stay clean.

## Track: Python LeetCode (`leet_code`)

- Modules live in `src/python/leet_code/<slug_snake>.py`; tests in `test/python/leet_code/test_<slug_snake>.py`. No registration is required because Pixi
  activation sets `PYTHONPATH` (`src/python`, `.agents/plugins/leetcode/src`, repo root).
- Reuse shared helpers in `src/python/leet_code/utility.py` (such as `is_sorted`) rather than duplicating logic across modules.
- Public contract: expose an idiomatic, fully typed `snake_case` API (Google docstrings, `ValueError` on invalid input) **and** the LeetCode compatibility
  wrapper `class Solution` with the official method name (suppress `invalid-function-name` on that method line via `# noqa: invalid-function-name`). Design
  problems expose a `CapWords` class plus an official-name alias (see `two_sum.py` and `lru_cache.py` as references).
- Never raise `NotImplementedError` in TDD-red stubs; the red state comes from failing test assertions on clean dummy returns.
- 4 test tiers under `pytest 9+` (`strict = true`, `--doctest-modules`, `filterwarnings = ["error"]`, `pytest-randomly` shuffling):
  - Tier 1: canonical examples.
  - Tier 2: boundary & corner cases.
  - Tier 3: `hypothesis` property-based invariants.
  - Tier 4: `@pytest.mark.stress` with `time.perf_counter()` bounds.

## LeetCode MCP Server, Coverage & Marimo Notebooks

- The `leetcode` MCP server tests in `.agents/plugins/leetcode/tests/` run under the root `pytest` configuration (`pixi run test-python`). Live-network tests
  are marked `@pytest.mark.live` and deselected by default (`-m "not live"`).
- **Coverage gate**: `pixi run cov-python` enforces >= 85% branch-enabled coverage across **both** `src/python/leet_code` and
  `.agents/plugins/leetcode/src/leetcode_mcp`.
- **Marimo notebooks** (`notebooks/`, Marimo `0.25.2+`): validate DAG strictness with `pixi run notebook-check` (`marimo check --strict notebooks/`), headless
  execution (`pixi run python notebooks/benchmark_telemetry_explorer.py`), and embedded pytest (`pixi run pytest notebooks/benchmark_telemetry_explorer.py -v`).
