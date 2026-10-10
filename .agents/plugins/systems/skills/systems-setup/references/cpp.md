# C++23 Header, Stub, and CMake Reference

Used by `systems-setup` for Step 4. Follow `cpp-pro`, `cmake-pro`, and [safety.md](safety.md). Style reference for finished code:
`src/include/systems/bounded_fifo.hpp`.

## Naming

| Item | Convention | Example |
| --- | --- | --- |
| Component / files | `<name>` snake_case | `shm_pubsub` |
| Main type | `<Name>` CapWords | `ShmPubSub` |
| Error enum | `<Name>Error` | `ShmPubSubError` |
| Library target | `<name>`, alias `systems::<name>` | `systems::shm_pubsub` |
| Test target / suites | `<name>_test`; suites `<Name>Test`, `<Name>ConcurrencyTest`, `<Name>MultiProcessTest`, `<Name>FaultTest` | `ShmPubSubFaultTest` |
| Namespace | `systems` (internal helpers in `systems::detail`) | |

## Header (`src/include/systems/<name>.hpp`)

The header is the contract. It must be complete enough that the tests compile and the user never has to change a public signature, and it must not contain
the solution.

1. `#pragma once`, then a `///` Doxygen file block (never `/* */`, the text may contain `*/`):
   - `@file`, `@brief` one line.
   - **Problem**: the user's statement in a sentence or two and a pointer to `docs/systems/<name>.md` (requirement prefix).
   - **Design summary**: semantics chosen in the spec (overflow policy, scope, blocking model, failure model).
   - **Thread safety / process safety**, **Blocking**, **Memory** (what allocates and when), **Error handling** (`std::expected`, no exceptions),
     **Portability** (which requirements depend on `systems/platform.hpp` capabilities).
   - **Implementation outline**: a numbered list of what the user must build (for example "1. Define the shared layout: header (magic, version, config),
     per-slot sequence counters, payload area. 2. ..."), plus **Pitfalls** (torn reads, ABA, cache-line false sharing, `EOWNERDEAD`, `EINTR`, name length).
     Describe *what* and *why*, never paste the algorithm.
2. Includes: only what the declarations need (`<cstddef>`, `<cstdint>`, `<expected>`, `<span>`, `<chrono>`, `<string_view>`, `systems/platform.hpp`). No POSIX
   headers in the public header unless a POSIX type is part of the API (avoid that).
3. Error enum: `enum class <Name>Error : std::uint8_t { kNotImplemented, ... };` with a `///` line per enumerator, plus
   `[[nodiscard]] constexpr auto to_string(<Name>Error) noexcept -> std::string_view;` (implemented in the header, fully, since tests print it).
4. Configuration and value types: small aggregates (`struct <Name>Config { std::uint32_t slot_count; ... };`) with documented valid ranges and
   `inline constexpr` limits (`kMaxSlotCount`, ...).
5. Public classes:
   - Factory functions for anything that can fail during init: `[[nodiscard]] static auto create(...) noexcept -> std::expected<<Name>, <Name>Error>;`
     (constructors cannot report errors without exceptions).
   - Explicit Rule of Five: usually move-only (`= delete` copy, `noexcept` move) or non-movable when addresses must stay stable.
   - Every operation `[[nodiscard]] ... noexcept`, documented with `@param`, `@return`, `@pre`, error list, thread-safety, blocking, and the requirement IDs
     it satisfies (`@par Requirements` followed by `<PREFIX>-REQ-003`).
   - Try / timed / blocking variants for anything that can wait; `close()` for anything with waiters.
   - Private members: a minimal placeholder state the stubs need (for example a pointer to the mapped region and its size), with a comment
     `// TODO(<PREFIX>): add the state your design needs.` The user may change private members freely.
6. OS adapter (only if OS calls are needed): a small move-only class in `namespace systems::detail` (for example `SharedMemoryRegion` with `create`, `open`,
   `unlink`, `data`, `size`), declared in the same header, implemented in `<name>_posix.cpp`.

## Stub implementation (`src/cpp/systems/<name>.cpp` and optional `<name>_posix.cpp`)

1. Include the component header first, then what you use.
2. Each function body:
   - starts with a `// TODO(<PREFIX>-REQ-NNN): ...` numbered checklist of the steps the user must implement (one line each);
   - silences parameters with `static_cast<void>(param);`;
   - returns the "not done" value: `std::unexpected(<Name>Error::kNotImplemented)`, `false`, `0`, or a default-constructed result;
   - never blocks, never sleeps, never spins, never allocates, never calls `std::abort`.
3. Destructors and move operations of stubs must be correct for the placeholder state (release nothing they did not acquire). Tests may construct and
   destroy objects even in the red state, so these must be ASan-clean.
4. `<name>_posix.cpp` is the only file allowed to include POSIX headers. In the stub, its functions also just return `kNotImplemented`.
5. Header-only templates: still add `<name>.cpp` with an explicit instantiation (as `bounded_fifo.cpp` does) so clang-tidy analyzes the template.

## CMake registration (target-based, gersemi-formatted)

`src/cpp/systems/CMakeLists.txt` (append; keep a two-line comment describing the component):

```cmake
# Shared-memory pub/sub ring buffer, safe across threads and processes (spec: docs/systems/shm_pubsub.md).
systems_add_library(
    shm_pubsub
    SOURCES shm_pubsub.cpp shm_pubsub_posix.cpp
    LIBS systems::platform Threads::Threads
)
```

Add `rt` to `LIBS` only if `shm_open` fails to link on an old glibc (glibc 2.34+ does not need it; macOS and QNX never do). Guard with `if(CMAKE_SYSTEM_NAME
STREQUAL "Linux")` when used.

`test/cpp/systems/CMakeLists.txt`:

```cmake
add_project_test(
    shm_pubsub_test
    SOURCES shm_pubsub_test.cpp
    LIBS systems::shm_pubsub systems::test_support
    LABELS "systems;multiprocess"
)
```

Labels: always `systems`; add `multiprocess` when the suite forks; add `fault` when it SIGKILLs processes. They allow `ctest -L multiprocess`.

Run `pixi run format-cmake` afterwards.

## Formatting and linting checklist

- `pixi run clang-format -i <files>` (Google style).
- The `dev-debug` build runs clang-tidy with `WarningsAsErrors: "*"`, including `misc-include-cleaner` and analysis of `src/include/systems/` headers.
  Common fixes: include the public POSIX header for POSIX symbols, `std::forward` forwarding references (or take callables by value), avoid chrono literals,
  no redundant `()` in lambdas without parameters (`[] -> int {}` is valid C++23).
