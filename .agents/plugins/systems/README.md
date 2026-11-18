# Systems Practice Antigravity Plugin (safety-oriented C++23, C23 & Rust)

Scaffolds low-level / embedded-style C++23, ISO C23, and Rust 1.100+ practice components for the **systems** track from a free-form problem statement.
LeetCode rarely covers this kind of problem, so there is no lookup service: the prompt *is* the problem, and the skill turns it into a requirements spec, a
documented API outline with stubs, and a red test suite.

Constraints mirror robotics and automotive practice: ASPICE-style requirements with traceability, an ISO 26262 mindset, MISRA C++:2023 / AUTOSAR C++14 / CERT C
/ Google Rust guidance, no heap after initialization, no exceptions, panics, or RTTI in runtime paths, and POSIX portability (Linux, embedded Linux, QNX; macOS
as a development host).

## Layout

```text
.agents/plugins/systems/
├── plugin.json
├── README.md
├── assets/logo.svg
└── skills/systems-setup/
    ├── SKILL.md              # Inputs, ambiguity handling, 7-step workflow
    └── references/
        ├── spec.md           # Requirements spec template and ID / traceability rules
        ├── safety.md         # Memory, error handling, concurrency, portability, standards pointers
        ├── cpp.md            # C++23 / C23 header outline, stub rules, CMake registration
        ├── rust.md           # Rust 1.100+ module layout, const generics, scoped unsafe, test tiers
        └── testing.md        # Test tiers, harness API, thread / process / fault-injection patterns
```

The plugin has no MCP server. It relies on repository infrastructure shared by every systems component:

| Piece | Path | Purpose |
| --- | --- | --- |
| Capability flags | `src/include/systems/platform.{hpp,h}`, `src/rust/systems/src/platform.rs` | `kHasRobustMutex` / `SYSTEMS_HAS_ROBUST_MUTEX`, `kMaxShmNameLength`, `CACHE_LINE_SIZE`, ... |
| Test support | `test/cpp/systems/support/` (`systems::test_support`) | Fork-based `ChildProcess` with timeouts and SIGKILL, `SharedRegion<T>` rendezvous memory, unique shm names with RAII unlink, `AllocationScope` heap counter |
| Support self-tests | `test/cpp/systems/test_support_test.cpp` | Keeps the harness trustworthy (always green) |
| Traceability check | `scripts/ci/check_systems_traceability.py` (`pixi run check-traceability`) | Every `Test` requirement in `docs/systems/*.md` has a C++/C/Rust test with traceability tags, and every referenced ID exists |

## Usage

```text
Set up a systems problem: Create a pub/sub interface to some shared memory. This needs to be safe to access across processes
and threads. The shared memory should be set up like a ring buffer with a finite number of blocks that a publisher writes to
and a subscriber reads from.
```

The agent asks only about choices that change the API or the test oracle (for example overwrite-oldest versus back-pressure), records the rest as
assumptions, and generates:

| Output | C++23 / ISO C23 | Rust 1.100+ |
| --- | --- | --- |
| Requirements spec | `docs/systems/<name>.md` | `docs/systems/<name>.md` |
| API + implementation outline | `src/include/systems/<name>.{hpp,h}` | `src/rust/systems/src/<name>.rs` |
| Stubs (portable logic, OS adapter) | `src/{cpp,c}/systems/<name>.{cpp,c}`, `<name>_posix.{cpp,c}` | `src/rust/systems/src/<name>.rs` (or `posix` submodule) |
| Tests (red) | `test/cpp/systems/<name>_test.cpp` / `test/c/systems/<name>_c_test.cpp` | `test/rust/systems/<name>_test.rs` |
| Registration | `src/{cpp,c}/systems/CMakeLists.txt`, `test/{cpp,c}/systems/CMakeLists.txt` | `src/rust/systems/src/lib.rs`, `src/rust/systems/Cargo.toml` |

## Test tiers

Tiers: 1. functional, 2. boundary / error, 3. resource and invariants (no heap after init), 4. multi-thread stress (TSan),
5. multi-process (`fork` / `Command`), 6. fault injection (`SIGKILL` at arbitrary points). Tiers 5 and 6 apply when the
component crosses processes or has a failure-model requirement. Requirements that need capabilities missing on the host
(robust mutexes on macOS) `GTEST_SKIP` with the capability named and run in Linux CI.

## Verification

```bash
pixi run cmake --workflow --preset dev-debug   # ASan + UBSan + clang-tidy; new C++/C23 tests fail cleanly
pixi run cmake --workflow --preset tsan        # no ThreadSanitizer reports
pixi run test-rust                             # cargo test --workspace --all-targets
pixi run clippy                                # cargo clippy --all-targets -- -D warnings
pixi run check-traceability
pixi run pre-commit
```
