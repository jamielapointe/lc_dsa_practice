# Python LeetCode Scaffolding Reference

Used by the `leetcode-setup` skill when the user chose **`python`**. Follow `python-pro` (Python 3.15 with PEP 649 native annotations, Google style,
double quotes, 88 columns, ruff 23-family preview rules with `RUF201` full rule names, `basedpyright` strict + `ty`).

## Naming conventions

- `slug_snake`: slug with hyphens replaced by underscores (`add_two_numbers`).
- Module: `src/python/leet_code/<slug_snake>.py` (imported as `leet_code.<slug_snake>`)
- Test: `test/python/leet_code/test_<slug_snake>.py`
- The package `leet_code` already exists (`src/python/leet_code/__init__.py`, `py.typed`); `PYTHONPATH` is set by Pixi (`pixi.toml`), so nothing needs
  registering.

Reference examples to imitate: `src/python/leet_code/two_sum.py`, `src/python/leet_code/lru_cache.py` and their tests.

## Generate the module (`src/python/leet_code/<slug_snake>.py`)

1. Module docstring: `LeetCode #<frontend_id>: <title> (<difficulty>).`, URL, the problem statement (from `description_markdown`), verbatim constraints, and
   target complexity.
2. Rely on Python 3.15 PEP 649 built-in deferred annotations (`from __future__ import annotations` is banned by `TID251`); put typing-only imports (for
   example `collections.abc.Sequence`) under `if TYPE_CHECKING:`. Group imports with two blank lines after the import block (`lines-after-imports = 2`). Use
   PEP 695 `type` aliases where helpful.
3. Declare two interfaces:
   - **Idiomatic Python**: a fully typed function or class with `snake_case` names, Google-style docstrings (`Args`/`Returns`/`Raises`/`Examples`), and
     `ValueError` for invalid input.
   - **LeetCode wrapper**: `class Solution:` with the official method name and signature taken from `python_snippet` (for example
     `def twoSum(self, nums: list[int], target: int) -> list[int]:  # ruff: ignore[invalid-function-name, no-self-use]`), delegating to the idiomatic
     function. For design problems (a class like `LRUCache`) provide one idiomatic `CapWords` class and a compatibility alias with the official name, as in
     `lru_cache.py`.
4. Stub body returns a clean dummy of the declared type (`0`, `False`, `[]`, `None`, ...). Suppress unused parameters with `del a, b` so `ruff`'s `ARG` rules
   pass. Never raise `NotImplementedError` (the red state must come from assertions, not errors).

## Generate the 4-tier pytest suite (`test/python/leet_code/test_<slug_snake>.py`)

1. Do NOT add `from __future__ import annotations`; import `pytest`, `hypothesis`, then grouped imports from `leet_code.<slug_snake>`.
2. Tiers (section comments `# Tier N: ...`):
   - **Tier 1, canonical**: every official example (use `@pytest.mark.parametrize`) against both the idiomatic API and `Solution`.
     **Assert the ACTUAL CORRECT ANSWER**, never the stub's dummy value. Tests SHOULD fail until implemented.
   - **Tier 2, boundary / edge**: minimum sizes, empty inputs, duplicates, negative values, extremes, invalid input raising `ValueError`.
   - **Tier 3, invariants**: `hypothesis` `@given` property-based tests (no input mutation, bounds, differential checks against a trivially correct reference
     model) alongside seeded deterministic checks (`pytest-randomly` shuffles test order automatically).
   - **Tier 4, stress / OJ simulation**: maximum-constraint inputs generated programmatically (never huge literals), marked `@pytest.mark.stress`, with a
     `time.perf_counter()` guard to catch asymptotically slow solutions.
3. Every test function has a one-line docstring (ruff `D`); `assert` is allowed in tests.

## Verify (always through Pixi)

```bash
pixi run pytest test/python/leet_code/test_<slug_snake>.py   # must FAIL cleanly on assertions (TDD red), no collection/import errors
pixi run ruff format src/python test/python
pixi run ruff check src/python test/python                   # zero findings (23 rule families)
pixi run typecheck                                           # basedpyright (strict) && ty check, zero findings
```
