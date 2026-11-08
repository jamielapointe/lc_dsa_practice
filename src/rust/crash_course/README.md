# Rust Systems Crash Course Scratchpad

This directory is the dedicated scratchpad workspace for intensive, test-driven Rust exercises scaffolded by the
[`rust-systems-tdd-coach`](../../../.agents/skills/rust-systems-tdd-coach/SKILL.md) skill.

It supports both **Autonomous Curriculum Mode** (a progressive 10-module systems progression) and **Custom Task Mode** (user-specified algorithms, data
structures, and FFI bridges).

---

## Sub-Workspace Isolation Architecture

Every exercise scaffolded under this directory operates as an independent, isolated Cargo workspace:

1. **Root `Cargo.toml` Exclusion**:
   - `src/rust/crash_course` is explicitly listed in `exclude = [...]` of the root repository `[workspace]` table.
   - Root workspace lints strictly deny `todo`, `unimplemented`, and `panic` (`todo = "deny"`), and root CI executes `pixi run clippy` and `pixi run cov-rust`
     with an 85% coverage gate.
   - Excluding `src/rust/crash_course` guarantees that active red-phase stubs (`todo!()`) and incomplete exercise implementations never break repository-wide
     pre-commit gates or pull request CI workflows.

2. **Standalone Exercise Manifests**:
   - Each scaffolded exercise crate declares an isolated standalone `[workspace]` table in its `Cargo.toml`.
   - Each exercise sets `[lints.clippy] todo = "allow"` during the red phase so that `cargo clippy --no-deps` passes cleanly while stubs remain unimplemented.
   - When the user transitions to the green/refactor phase, full clippy audits (`-D warnings`) and interview rubrics evaluate the final code.

---

## Directory Structure

Scaffolded exercises are placed into individual subdirectories:

```text
src/rust/crash_course/
├── README.md                               # This documentation file
├── <exercise_slug>/                        # Example: shm_packet_transfer, zero_copy_frame_slicer, ...
│   ├── Cargo.toml                          # Standalone manifest (edition 2024, resolver 3, isolated workspace)
│   ├── build.rs                            # Optional CXX build script (if C++/Rust interop exercise)
│   ├── cxx_bridge/                         # Optional C++23 companion headers and sources (if FFI)
│   │   ├── cxx_bridge.hpp
│   │   └── cxx_bridge.cpp
│   ├── src/
│   │   └── lib.rs                          # Scaffolded signatures, docstrings, error enums, and todo!() stubs
│   └── tests/
│       └── <exercise_slug>_test.rs         # 4-tier integration test suite (Canonical, Boundary, RAII, Concurrency)
```

---

## Workflow & Execution Commands

Always run tools through the repository's `pixi run` environment so that pinned Clang 23, CMake 4.4, Ninja, and Rust 1.99.1 toolchains are active:

### 1. Navigating to an Exercise

You can run commands from the repository root using `--manifest-path`, or change directory directly into the exercise:

```bash
# From repository root:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml

# Or change directory:
cd src/rust/crash_course/<exercise_slug>
pixi run cargo test
```

### 2. Red-Phase Verification (Automated by Coach)

Before handing control to the learner, the coach runs:

```bash
# Verify clean compilation without running tests:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml --no-run

# Verify static analysis passes on stub signatures:
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml --no-deps

# Verify all tests fail strictly on todo!() panics:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml
```

### 3. Implementation & Green-Phase Testing

While implementing your solution in `src/lib.rs`:

```bash
# Run unit and integration tests:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml

# Run a specific test with uncaptured stdout:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml -- test_canonical_push_pop --nocapture
```

### 4. Green/Refactor Critique Evaluation

Once all tests pass, evaluate your implementation against the 4-pillar interview rubric:

```bash
# Strict static analysis (zero warnings allowed):
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml --all-targets -- -D warnings

# Format verification:
pixi run cargo fmt --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml --check

# Undefined behavior and Stacked Borrows audit (on pure Rust crates):
pixi run cargo miri test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml
```
