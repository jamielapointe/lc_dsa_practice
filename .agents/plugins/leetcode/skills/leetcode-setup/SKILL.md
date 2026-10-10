---
name: leetcode-setup
description: Sets up LeetCode DSA practice problems in either modern C++23 (CMake + Google Test) or Python 3.14 (pytest) in this repository. The user MUST specify the language (cpp or python); ask if it is missing. Fetches problem data via the leetcode MCP server (tool get_problem), then generates documented headers/modules, stub implementations that build cleanly, and comprehensive 4-tier test suites that start in the TDD red state.
---

# LeetCode Setup Skill (C++23 and Python)

## 🔗 Related Skills

- **cpp-pro**: Modern C++23, RAII, memory safety, strict compiler conformance (C++ path).
- **cmake-pro**: Target-based CMake 4.4.3+, presets, modular architecture, CTest (C++ path).
- **python-pro**: Python 3.14, Pixi, ruff, mypy, pytest (Python path and MCP server).

---

## Use this skill when

- The user asks to "set up", "initialize", "add", or "scaffold" a LeetCode problem, e.g. "Set up LeetCode #2 in C++" or "Scaffold LeetCode two-sum in Python".
- A developer wants to start the Test-Driven Development (TDD) cycle for a LeetCode problem in C++23 or Python.

## Do not use this skill when

- Implementing the actual algorithm (this skill produces stubs that return dummy values to establish the TDD red state).
- Working on low-level / systems C++ (`src/cpp/systems`), the plugin itself, or general project configuration.

---

## Step 0: Determine the language (REQUIRED)

The user MUST specify the language: **`cpp`** (also "C++", "c++23") or **`python`** (also "py", "python3").

- If the language is missing or ambiguous, STOP and ask the user which one they want. Never guess or default.
- If the user wants both, run the whole workflow once per language, reusing the same issue/branch.

Language-specific templates and conventions live in the reference files; read the one that matches before generating files:

- C++23: [references/cpp.md](references/cpp.md)
- Python: [references/python.md](references/python.md)

## Repository layout (both languages)

| Purpose | C++ | Python |
| --- | --- | --- |
| Public API | `src/include/leet_code/<slug_snake>.hpp` | `src/python/leet_code/<slug_snake>.py` |
| Implementation | `src/cpp/leet_code/<slug_snake>.cpp` | (same module) |
| Tests | `test/cpp/leet_code/<slug_snake>_test.cpp` | `test/python/leet_code/test_<slug_snake>.py` |
| Build registration | `src/cpp/leet_code/CMakeLists.txt`, `test/cpp/leet_code/CMakeLists.txt` | none (package auto-discovered via `PYTHONPATH`) |

`slug_snake` is the LeetCode slug with hyphens replaced by underscores (`add-two-numbers` -> `add_two_numbers`).

---

## Autonomous Execution Workflow

### Step 1: Verify clean Git workspace

Before doing anything, verify the git workspace is clean (`git status`). If it is not clean, stop immediately and tell the user to clean it first.

### Step 2: Standard GitHub workflow

All GitHub interactions MUST use the `github-mcp-server` tools. Before making code changes:

1. Update main locally: `git checkout main && git fetch --all --prune && git pull`.
2. Use `github-mcp-server` to create a GitHub issue describing the problem (include the language).
3. Use `github-mcp-server` to create a remote feature branch associated with the issue.
4. `git fetch` and check out that branch locally.

### Step 3: Query problem data via the MCP server

Invoke the `leetcode` MCP server tool `get_problem`:

```json
{ "tool": "get_problem", "arguments": { "problem_query": "<problem_number_or_slug>" } }
```

The tool accepts a numeric ID (`2`, `#2`), URL slug (`add-two-numbers`), title, or full URL, and returns a `ProblemDetails` object:

- `frontend_id`, `question_id`, `title`, `slug`, `difficulty` (`Easy` | `Medium` | `Hard`), `is_paid_only`
- `description_markdown`: clean Markdown description
- `cpp_snippet`: official C++ starter code (use for `cpp`)
- `python_snippet`: official Python 3 starter code (use for `python`; may be empty, then derive the signature from the C++ snippet and the description)
- `sample_test_case`, `example_test_cases`, `constraints` (math notation preserved, e.g. `10^4`), `topic_tags`, `hints`

If the problem is premium-only the tool returns an error; report it and stop.

### Step 4: Generate files for the chosen language

Follow [references/cpp.md](references/cpp.md) or [references/python.md](references/python.md) exactly. Both produce:

1. A fully documented public API with an idiomatic modern interface **and** a LeetCode compatibility wrapper that matches the official signature.
2. A stub implementation that returns clean dummy values (never crashes).
3. A 4-tier test suite (canonical, boundary, invariants, stress) whose assertions check the **actual correct answers**, so tests FAIL until the user implements
   the algorithm (TDD red).

### Step 5: Verify (via Pixi only)

Always run tools through `pixi run ...` so the hermetic toolchain is used. The exact commands are in the language reference. Verify that:

- everything builds / imports and static checks pass with zero warnings,
- the new tests fail cleanly on assertions (no segfaults, sanitizer reports, or collection errors).

### Step 6: Report

Summarize the generated files, the verification output, and remind the user the tests are intentionally red. Do not implement the algorithm and do not open a PR
unless asked.
