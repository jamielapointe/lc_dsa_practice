---
name: systems-setup
description: Scaffolds a low-level / embedded-style C++23 practice component in the systems track from a free-form problem statement (no website lookup). Produces an ASPICE-style requirements spec with traceable IDs, a documented header and stub implementation that outline the work (no solution), CMake registration, and a GoogleTest suite (functional, boundary, resource, multi-thread, multi-process, fault-injection tiers) that starts in the TDD red state. Applies automotive / robotics constraints (ISO 26262 mindset, MISRA C++:2023 and AUTOSAR C++14 guidance, no steady-state heap, no exceptions or RTTI) targeting POSIX (Linux, embedded Linux, QNX).
---

# Systems Setup Skill (safety-oriented C++23)

## 🔗 Related Skills

- **cpp-pro**: Modern C++23, RAII, concurrency, sanitizer-clean code.
- **cmake-pro**: Target-based CMake 4.4.3+, presets, CTest labels, gersemi.
- **python-pro**: Only for the traceability checker (`scripts/ci/check_systems_traceability.py`).

---

## Use this skill when

- The user asks to "set up", "scaffold", or "start" a **systems**, embedded, real-time, robotics, or automotive-style C++ problem and describes it in prose,
  e.g. "Set up a systems problem: a pub/sub interface over a shared-memory ring buffer, safe across processes and threads".
- A developer wants to begin the TDD cycle for a component that lives in `src/include/systems/` and `src/cpp/systems/`.

## Do not use this skill when

- The problem is a LeetCode problem (use `leetcode-setup`).
- The user asks you to *implement* the component. This skill produces an outline, stubs, a spec, and red tests; the user writes the solution.
- Editing existing systems components (`BoundedFifo`, test support) or project configuration.

---

## Inputs

There is no lookup service: **the user's prompt is the problem statement.** Only C++ is supported (no language question). Extract:

| Item | Example (shared-memory pub/sub) |
| --- | --- |
| Component purpose | Publish fixed-size blocks to subscribers through shared memory |
| Concurrency scope | Threads, processes, or both; number of producers / consumers |
| Blocking semantics | Non-blocking writer, try / timed / blocking reader, `close()` |
| Overflow policy | Overwrite oldest vs back-pressure vs reject |
| Sizing | Slot count / slot size, compile-time or fixed at initialization |
| Failure model | What happens when a process dies mid-operation |
| Timing | Latency or wake-up bounds, if any |

### Step 0: Resolve ambiguity (ask only what changes the API or the test oracle)

- If an item above has several reasonable answers **and** the choice changes the public API or what a test would assert, ask the user (use `ask_question` with
  concrete options and a recommended default). Ask at most once, batching all questions.
- Everything else: choose the conventional automotive / robotics default, and record it in the spec's **Assumptions** section so the user can push back.
- Derive the component name `<name>` (snake_case, e.g. `shm_pubsub`), the type name `<Name>` (CapWords), and the requirement prefix `<PREFIX>` (3-6 upper-case
  letters, e.g. `SHMPS`).

## Repository layout

| Purpose | Path |
| --- | --- |
| Requirements spec | `docs/systems/<name>.md` |
| Public API (+ outline) | `src/include/systems/<name>.hpp` (included as `<systems/<name>.hpp>` or `"systems/<name>.hpp"`) |
| Portable logic stub | `src/cpp/systems/<name>.cpp` |
| OS adapter stub (only if the component calls OS APIs) | `src/cpp/systems/<name>_posix.cpp` |
| Tests | `test/cpp/systems/<name>_test.cpp` |
| Build registration | `src/cpp/systems/CMakeLists.txt` (`systems_add_library`), `test/cpp/systems/CMakeLists.txt` (`add_project_test`) |
| Shared test harness (reuse, do not copy) | `test/cpp/systems/support/systems_test/*.hpp` (`systems::test_support`) |
| OS capability flags (reuse) | `src/include/systems/platform.hpp` (`systems::platform`) |

Read every reference before generating files:

- [references/spec.md](references/spec.md): requirements spec template, ID scheme, traceability.
- [references/safety.md](references/safety.md): coding constraints, standards guidance, portability and the OS-adapter rule.
- [references/cpp.md](references/cpp.md): header outline, stub rules, CMake registration.
- [references/testing.md](references/testing.md): test tiers, harness API, concurrency / multi-process / fault-injection patterns, red-state safety.

---

## Autonomous Execution Workflow

### Step 1: Verify a clean Git workspace

Run `git status`. If it is not clean, stop and ask the user to clean it first.

### Step 2: Standard GitHub workflow

All GitHub interactions MUST use the `github-mcp-server` tools (never `gh` or `git push`):

1. Update main locally: `git checkout main && git fetch --all --prune && git pull`.
2. Create a GitHub issue titled `Systems: <Name>` whose body contains the user's problem statement verbatim and the chosen assumptions.
3. Create a remote branch `<issue-number>-systems-<name>` from `main`, then `git fetch` and check it out locally.

### Step 3: Write the requirements spec

Create `docs/systems/<name>.md` following [references/spec.md](references/spec.md). Every requirement gets a stable ID `<PREFIX>-REQ-NNN`, a verification
method, and (for safety-relevant ones) a rationale. This document is the *problem statement* the user works against.

### Step 4: Generate the header, stubs, and CMake registration

Follow [references/cpp.md](references/cpp.md) and [references/safety.md](references/safety.md):

1. The header declares the **complete public API** (types, error enum, functions, documented contracts, thread-safety, blocking behavior) so tests compile,
   plus an `Implementation outline` section that describes *what* the user must build and the pitfalls, **without** giving the solution.
2. Stubs compile cleanly under `-Werror` and clang-tidy, never block, never crash, never allocate, and return `std::unexpected(<Error>::kNotImplemented)` (or
   the documented "nothing happened" value). Each stub body carries a numbered `TODO(<PREFIX>-REQ-NNN)` checklist.
3. Register the library with `systems_add_library` and the test with `add_project_test(... LABELS ...)`.

### Step 5: Generate the test suite

Follow [references/testing.md](references/testing.md). Tests assert the **correct** behavior from the spec (never the stub's dummy value), tag requirement IDs
with `RecordProperty("requirements", "...")`, reuse `systems::test_support`, and are bounded in time so the red state is clean: failed assertions, no hangs, no
crashes, no sanitizer reports.

### Step 6: Verify (Pixi and presets only)

```bash
pixi run cmake --preset dev-debug
pixi run cmake --build --preset dev-debug --target <name>_test        # zero warnings, clang-tidy clean
pixi run ctest --preset dev-debug -R '^<Name>' --output-on-failure    # FAILS cleanly (red), finishes well under the 60 s ctest timeout
pixi run cmake --workflow --preset tsan                               # only <Name> tests may fail; no ThreadSanitizer reports
pixi run check-traceability                                           # every requirement has a test and vice versa
pixi run format-cpp && pixi run format-cmake && pixi run lint-md      # clang-format, gersemi, rumdl
```

Confirm the existing suites (including `test_support_test` and `bounded_fifo_test`) still pass. Expected red: every `<Name>` test fails on an assertion or a
`GTEST_SKIP` with a platform reason; none time out, segfault, or hang.

### Step 7: Report

Summarize the generated files, the assumptions you made, the requirement → test traceability table, platform skips (for example robust-mutex tests on macOS),
and the verification output. Remind the user that the tests are intentionally red. Commit locally on the feature branch only if the user asks; do not
implement the component and do not open a PR unless asked.
