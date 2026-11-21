---
trigger: glob
description: Embedded/safety-critical systems engineering invariants, OS capability isolation, multi-process test support, TSan, and ASPICE requirement traceability for C++23, ISO C23, and Rust systems tracks.
globs: "src/include/systems/**/*, src/cpp/systems/**/*, src/c/systems/**/*, src/rust/systems/**/*, test/cpp/systems/**/*, test/c/systems/**/*, test/rust/systems/**/*, docs/systems/**/*.md, examples/cpp/systems/**/*, examples/c/systems/**/*"
---

# Systems Engineering Rules (C++23, ISO C23 & Rust)

Intent: portable low-level, embedded, and automotive/robotics building blocks that can move to a constrained target without redesign.
`systems::BoundedFifo<T, Capacity>` (and its ISO C23 `bounded_fifo` and Rust `BoundedFifo` counterparts) is the primary style reference.

## Layout & Build Registration

- **C++23 (`systems`)**: headers in `src/include/systems/<name>.hpp`, sources in `src/cpp/systems/<name>.cpp`, tests in `test/cpp/systems/<name>_test.cpp`.
  Register via `systems_add_library` (`FILE_SET HEADERS`/`SOURCES`, alias `systems::<name>`, auto-links `common::common`).
- **ISO C23 (`systems_c`)**: headers in `src/include/systems/<name>.h`, sources in `src/c/systems/<name>.c`, tests in `test/c/systems/<name>_c_test.cpp`
  (GoogleTest via `extern "C"`) and `test/c/systems/systems_c23_test.c` (pure `-std=c23` CTest target). Register via `systems_c_add_library` (alias
  `systems_c::<name>`).
- **Rust (`systems`)**: modules in `src/rust/systems/src/<name>.rs`, integration tests in `test/rust/systems/<name>_test.rs` with `[[test]]` entries in
  `src/rust/systems/Cargo.toml`.
- `clang-tidy` analyzes `src/include/systems/` headers as well as both `src/cpp/systems/` and `src/c/systems/` translation units.

## Embedded & Safety-Critical Invariants

- **Fixed inline storage & zero steady-state heap**: use compile-time capacity (template parameter / const generic) with storage inline inside the object. No
  `new`/`malloc`, no STL dynamic containers (`std::vector`, `std::queue`, `std::deque`, ...), and no Rust `Vec`/`VecDeque` in steady-state paths. Small value
  types (`std::optional`, `std::array`, `std::span`) and synchronization primitives (`<atomic>`, `<mutex>`, `<condition_variable>`, C23 `<stdatomic.h>`,
  `<stdckdint.h>`, `pthread_*`, Rust `std::sync::{Mutex, Condvar}`) are allowed.
- Heap allocation is permitted **only** in an explicit initialization phase (`create()`/`open()`), never in steady-state operations.
- **No exceptions, RTTI, or uncontrolled panics**: public operations are `noexcept` / panic-free and report errors via `std::expected<T, <Name>Error>` (C++23),
  `[[nodiscard]] enum <name>_error` (C23), or `Result<T, <Name>Error>` (Rust).
- Prefer `constexpr`, `noexcept`, `[[nodiscard]]`, and `static_assert` for invariants; document thread-safety and blocking behavior in Doxygen / rustdoc.
- Blocking APIs must provide try, blocking, and timed variants plus a `close()` path that wakes every waiter.
- **Numeric & bit-level safety**: enforce Google Integer Types in C++ (`int`, `<cstdint>`, `size_t`/`ptrdiff_t` — never `short`/`long`/`unsigned`). Pair
  fixed-width integers with `std::bit_cast`/`<bit>` (C++23) or `<stdbit.h>`/`<stdckdint.h>`/`memcpy` (C23) for bit-twiddling or register code; never type-pun.
- Deliberate `NOLINT` annotations must carry a one-line justification. Avoid `std::chrono` literals (`10ms`); include public POSIX headers directly.

## OS Adapter Isolation & Portability

- Isolate all OS calls (`shm_open`, `mmap`, `pthread_*attr*`, etc.) into dedicated adapter translation units (`<name>_posix.cpp`, `<name>_posix.c`, or Rust
  platform/OS modules with item-scoped `#[allow(unsafe_code)]` + multi-point `// SAFETY:` proofs).
- Branch on capability flags exposed by `systems/platform.hpp` (`systems::platform`), `systems/platform.h` (`SYSTEMS_*` / `systems_*`), or Rust `platform.rs`
  (such as `kHasRobustMutex` / `SYSTEMS_HAS_ROBUST_MUTEX`) — never raw `#ifdef` OS checks inside component logic.
- Targets: Linux and QNX; macOS is a development host.

## Testing, Test Support & ASPICE Traceability

- Every systems component requires functional, boundary, resource (no heap after init), and multi-threaded stress tiers, plus multi-process and `SIGKILL`
  fault-injection tiers when crossing process boundaries.
- Reuse `systems::test_support` (`test/cpp/systems/support/systems_test/`: `ChildProcess`, `SharedRegion`, `unique_shm_name`, `AllocationScope`); never copy
  it, and keep its self-test `test_support_test` green.
- Specifications live in `docs/systems/<name>.md` using `### <PREFIX>-REQ-NNN: Title` headings with a `- **Verification:**` line.
- Every test declares requirement traceability: `RecordProperty("requirements", "<IDs>")` in C++/C GoogleTest or `// requirements: <PREFIX>-REQ-NNN` in Rust.
  `pixi run check-traceability` (also a pre-commit hook) enforces bidirectional coverage.
- Always run `pixi run cpp-tsan` in addition to `pixi run test-cpp` / `pixi run test-rust` before finishing concurrency or systems changes.
- See `.agents/plugins/systems/skills/systems-setup/references/{spec,safety,cpp,testing,rust}.md` for full templates.
