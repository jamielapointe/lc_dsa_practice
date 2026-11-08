# Strict Red-Green-Refactor TDD Protocol & Verification Sequence

The definitive operational protocol governing exercise scaffolding, anti-solution-leakage enforcement, and automated TDD lifecycle verification for the
`rust-systems-tdd-coach`.

---

## 1. The Strict TDD Verification Lifecycle

The coaching workflow is divided into four distinct phases designed to maintain absolute TDD integrity:

```text
+---------------------------------------------------------------------------------------------------------+
|                                    STRICT TDD VERIFICATION LIFECYCLE                                    |
+---------------------------------------------------------------------------------------------------------+
| Phase 1: Scaffold & Stub Only  --> Generate manifests, C++ bridge, signatures, 4-tier test suite.       |
| Phase 2: Anti-Solution Leakage --> Mandate todo!("Implement <fn>") in all stubs. Zero logic leaks.     |
| Phase 3: Red-Phase Gate        --> cargo fmt -> cargo test --no-run -> cargo clippy --no-deps -> cargo test (fails)    |
|                                    Confirm failures are SOLELY due to todo!() panics. Yield to user.   |
| Phase 4: Green/Refactor Mode   --> cargo test (pass) -> cargo clippy (-D warnings) -> cargo miri test   |
|                                    Deliver interview-grade 4-pillar architectural critique.             |
+---------------------------------------------------------------------------------------------------------+
```

---

## 2. Workspace Isolation & Manifest Contracts

### 2.1 The Sub-Workspace Insulation Invariant

In this repository, root `Cargo.toml` configures strict Google Rust Styleguide lints across all workspace members:

```toml
# Root Cargo.toml
[workspace.lints.clippy]
todo = "deny"
unimplemented = "deny"
panic = "deny"
```

Furthermore, the root CI suite (`pixi run pre-commit`) runs:

- `pixi run clippy` (`cargo clippy --workspace --all-targets -- -D warnings`)
- `pixi run test-rust` (`cargo test --workspace --all-targets`)
- `pixi run cov-rust` (`cargo llvm-cov --workspace --all-targets --fail-under-lines 85`)

If scaffolded crash course exercises were registered as members of the root workspace, active red-phase exercises containing `todo!()` stubs and failing tests
would immediately break repository-wide pre-commit hooks and remote CI.

#### Mandatory Isolation Architecture

1. **Root Exclusion**: `src/rust/crash_course` MUST be explicitly excluded from root `Cargo.toml`:

   ```toml
   [workspace]
   resolver = "3"
   members = [
       "src/rust/leet_code",
       "src/rust/systems",
   ]
   exclude = [
       "src/rust/crash_course",
   ]
   ```

2. **Standalone Exercise Workspace**: Every generated exercise crate at `src/rust/crash_course/<exercise>/Cargo.toml` declares an isolated `[workspace]`
   header and explicitly allows `todo` during the red phase:

   ```toml
   [package]
   name = "<exercise_slug>"
   version = "0.1.0"
   edition = "2024"
   resolver = "3"
   rust-version = "1.99.1"
   publish = false

   [workspace]

   [dependencies]
   thiserror = { version = "2.0", default-features = false }
   cxx = { version = "1.0", optional = true }
   libc = { version = "0.2", optional = true }

   [build-dependencies]
   cxx-build = { version = "1.0", optional = true }
   cc = { version = "1.2", optional = true }

   [lints.rust]
   unsafe_op_in_unsafe_fn = "deny"

   [lints.clippy]
   all = { level = "warn", priority = -1 }
   pedantic = { level = "warn", priority = -1 }
   todo = "allow" # Permits red-phase todo!() stubs; audited during green critique
   ```

---

## 3. Scaffolding Contracts & Test Architecture

### 3.1 Scaffolding Boundaries

When generating an exercise in either Autonomous Curriculum Mode or Custom Task Mode, the coach produces:

1. `Cargo.toml`: Standalone manifest conforming to the manifest contract.
2. `build.rs` & `cxx_bridge/`: Companion C++23 files (if FFI/interop exercise).
3. `src/lib.rs`: Interface signatures, type definitions, const generics, error enums (`thiserror`), and `todo!()` stubs.
4. `tests/<exercise>_test.rs`: Exhaustive 4-tier integration test suite.

### 3.2 The 4-Tier Test Suite Contract

Tests are structured into four mandatory tiers to validate correctness across all systems dimensions:

```text
+---------------------------------------------------------------------------------------------------------+
|                                        4-TIER TEST SUITE STRUCTURE                                      |
+---------------------------------------------------------------------------------------------------------+
| Tier 1: Canonical Tests     | Validates standard happy-path functional contracts.                       |
| Tier 2: Boundary Tests      | Validates empty inputs, maximum limits, arithmetic overflows (i64::MAX).  |
| Tier 3: Invariant & RAII    | Validates Drop execution, zero memory leaks, and lifetime ownership.      |
| Tier 4: Concurrency / FFI   | Validates thread contention, timeouts, and FFI pointer integrity.        |
+---------------------------------------------------------------------------------------------------------+
```

```rust
// tests/spsc_queue_test.rs (Example 4-Tier Structure)

// =========================================================================
// TIER 1: CANONICAL FUNCTIONAL TESTS
// =========================================================================
#[test]
fn test_canonical_push_pop_sequence() {
    let queue: SpscQueue<u32, 4> = SpscQueue::new();
    assert!(queue.push(42).is_ok());
    assert_eq!(queue.pop(), Some(42));
    assert_eq!(queue.pop(), None);
}

// =========================================================================
// TIER 2: BOUNDARY & CAPACITY TESTS
// =========================================================================
#[test]
fn test_boundary_queue_full_and_empty() {
    let queue: SpscQueue<u32, 2> = SpscQueue::new();
    assert!(queue.push(1).is_ok());
    assert!(queue.push(2).is_ok());
    assert_eq!(queue.push(3), Err(3)); // Buffer full
    assert_eq!(queue.pop(), Some(1));
    assert_eq!(queue.pop(), Some(2));
    assert_eq!(queue.pop(), None);      // Buffer empty
}

// =========================================================================
// TIER 3: INVARIANTS & RAII DROP VERIFICATION
// =========================================================================
#[test]
fn test_invariants_raii_drop_reclamation() {
    use std::sync::atomic::{AtomicUsize, Ordering};
    static DROP_COUNTER: AtomicUsize = AtomicUsize::new(0);

    struct DropDetector;
    impl Drop for DropDetector {
        fn drop(&mut self) {
            DROP_COUNTER.fetch_add(1, Ordering::SeqCst);
        }
    }

    {
        let queue: SpscQueue<DropDetector, 4> = SpscQueue::new();
        assert!(queue.push(DropDetector).is_ok());
        assert!(queue.push(DropDetector).is_ok());
    } // Queue goes out of scope here

    // All unpopped elements remaining in queue MUST be dropped:
    assert_eq!(DROP_COUNTER.load(Ordering::SeqCst), 2);
}

// =========================================================================
// TIER 4: CONCURRENCY & CONTENTION TESTS
// =========================================================================
#[test]
fn test_concurrency_threaded_producer_consumer() {
    let queue: SpscQueue<u64, 1024> = SpscQueue::new();
    const ITERATIONS: u64 = 100_000;

    std::thread::scope(|s| {
        s.spawn(|| {
            for i in 0..ITERATIONS {
                while queue.push(i).is_err() {
                    std::hint::spin_loop();
                }
            }
        });

        s.spawn(|| {
            for expected in 0..ITERATIONS {
                loop {
                    if let Some(val) = queue.pop() {
                        assert_eq!(val, expected);
                        break;
                    }
                    std::hint::spin_loop();
                }
            }
        });
    });
}
```

---

## 4. Anti-Solution-Leakage Enforcement

The primary failure mode of an AI tutor is leaking solution logic prematurely. The coach upholds an absolute anti-solution-leakage barrier:

### 4.1 The `todo!()` Stub Invariant

- **Mandatory**: Every target function, method, and trait implementation body generated during scaffolding MUST contain exclusively:

  ```rust
  todo!("Implement <module>::<function_name>")
  ```

- **Prohibited**:
  - Providing partial algorithm logic (e.g. initializing loop counters or setting up initial pointers).
  - Leaving "pseudocode" or step-by-step implementation code in comments.
  - Adding helper routines that perform core data manipulation on behalf of the user.
  - Returning dummy/facade values (e.g. `return 0;` or `return Ok(());`) that bypass the intended test assertion. Tests MUST fail strictly because the code is
    unimplemented.

### 4.2 Socratic Interaction Guidelines

When a learner asks for clarification or struggles with a compiler error:

1. **Explain the Mechanic**: Clarify borrow checker rules, variance constraints, memory layouts, or LLVM lowering.
2. **Never Paste the Solution**: Do not write the code block for the user. Formulate guidance as:
   - *"Notice that `&mut T` is invariant over `T`. In your signature, you are attempting to assign an `&'a str` into an arena expecting `'static`..."*
   - *"Consider what happens when the ring buffer's head wraps around `CAP`. Can you compute the physical slot using bitwise masking if `CAP` is a power of
     two?"*
3. **Explicit Override**: Provide reference solution code **ONLY** if the user explicitly issues an unambiguous command:
   - *"Show me the reference solution."*
   - *"I surrender, show me how to implement this."*

---

## 5. Automated Red-Phase Verification Protocol

Before yielding control to the user, the coach MUST autonomously execute and verify the four-stage Red-Phase command sequence:

```bash
# Stage 1: Format scaffolded source files to guarantee clean rustfmt style
pixi run cargo fmt --manifest-path src/rust/crash_course/<exercise>/Cargo.toml

# Stage 2: Verify clean compilation of library and test targets (syntax & types valid)
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --no-run

# Stage 3: Verify static analysis passes on stub signatures without lint warnings
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --no-deps

# Stage 4: Confirm all tests fail STRICTLY on todo!() panics
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml
```

### 5.1 Red-Phase Verification Oracle Checks

The coach evaluates the terminal output against three mandatory criteria:

1. **Formatting and Compilation Exit Code**: Stages 1, 2, and 3 must exit with status code `0` and zero compiler errors.
2. **Failure Summary**: Stage 4 must report:

   ```text
   test result: FAILED. 0 passed; N failed; 0 ignored; 0 measured; 0 filtered out
   ```

   If `passed > 0`, the test suite contains trivial or defective tests that do not exercise the target stub.
3. **Panic String Match**: Every failed test's panic output must match:

   ```text
   panicked at ... 'not yet implemented: Implement <module>::<method>'
   ```

   If a test fails due to a segmentation fault, unexpected assertion failure, or syntax error, the test harness is defective.

### 5.2 Self-Correction Protocol

If the Red-Phase verification fails (e.g. a compilation error in `src/lib.rs` or tests, or a test passes trivially):

1. The coach does NOT yield control to the user.
2. The coach diagnoses the compiler error or test flaw.
3. The coach modifies `src/lib.rs` or `tests/<exercise>_test.rs` to fix the signature or test assertion.
4. The coach re-executes Stages 1–4 until the Red State is verified.

### 5.3 Yielding Control to the Learner

Once verified, the coach yields control with a concise, actionable summary:

```text
===============================================================================
                    TDD RED STATE VERIFIED: READY FOR IMPLEMENTATION
===============================================================================
Component:     <ExerciseName>
Manifest:      src/rust/crash_course/<exercise_slug>/Cargo.toml
Target File:   src/rust/crash_course/<exercise_slug>/src/lib.rs
Test Suite:    src/rust/crash_course/<exercise_slug>/tests/<exercise_slug>_test.rs
Status:        0 passed, N failed (all failing on todo!() panics)

Your Mission:
Implement the stubs in `src/lib.rs` to satisfy the 4-tier test suite.

Verification Command:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise_slug>/Cargo.toml
===============================================================================
```

---

## 6. Green/Refactor Mode Command Sequence

When the learner announces completion and requests a code review:

```bash
# 1. Verify all tests pass:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml

# 2. Strict static analysis with zero warnings:
pixi run cargo clippy --manifest-path src/rust/crash_course/<exercise>/Cargo.toml --all-targets -- -D warnings

# 3. Dynamic test execution with stdout capture:
pixi run cargo test --manifest-path src/rust/crash_course/<exercise>/Cargo.toml -- --nocapture
```

The coach then evaluates the implementation using the 4-pillar interview rubric in `references/interview_rubric.md`.
