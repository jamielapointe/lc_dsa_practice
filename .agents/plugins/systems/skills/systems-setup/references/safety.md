# Safety, Coding Constraints, and Portability Reference

Systems components are written as if they were going into a robotics or automotive ECU developed under ASPICE with ISO 26262 functional-safety goals.
Every generated header, stub, spec, and test must respect these rules; the user's implementation will be held to them by the tests and by review.

## 1. Memory

| Rule | Detail |
| --- | --- |
| No steady-state heap | `new`, `delete`, `malloc`, `free`, `std::make_unique`, `std::make_shared`, and allocating library types are forbidden in every operation except an explicit initialization phase (`create()`, `open()`, `init()`, or a constructor documented as init-only). |
| Init-phase allocation is bulk | If initialization allocates, it allocates one or a few large blocks (or maps shared memory) and carves them up; never per-element. |
| No STL containers | No `std::vector`, `std::string`, `std::map`, `std::deque`, `std::queue`, `std::function` (may allocate). Allowed: `std::array`, `std::span`, `std::string_view`, `std::optional`, `std::expected`, `std::variant`, `std::atomic`, `std::mutex`, `std::condition_variable`, `std::chrono`, `<bit>`, `<algorithm>` on fixed storage. |
| Compile-time capacity when possible | Prefer template parameters or `constexpr` sizes with `static_assert` invariants; runtime sizing is allowed only at initialization and must be validated. |
| Bounded stack | No recursion, no VLAs, no `alloca`; large buffers live in the object or in shared memory, not on the stack. |
| Verified, not trusted | Each component gets a "no heap after init" requirement verified with `systems_test::AllocationScope`. |

## 2. Error handling and language subset

| Rule | Detail |
| --- | --- |
| No exceptions in runtime paths | Every public operation is `noexcept` and reports failure with `std::expected<T, <Name>Error>` (or `std::expected<void, ...>`). The error type is a scoped `enum class <Name>Error : std::uint8_t` that always includes `kNotImplemented` (used by stubs) and one enumerator per distinct failure. |
| No RTTI | No `dynamic_cast`, no `typeid`; use static polymorphism (templates, concepts) or explicit tagged types. |
| `[[nodiscard]]` everywhere | All functions returning values, and the error enum type itself. |
| Fixed-width integers | `std::uint32_t`, `std::int64_t`, ... for anything stored in shared memory, on the wire, or in registers. |
| No type punning | Use `std::bit_cast`, `std::memcpy` into properly aligned storage, or `std::start_lifetime_as` (if available); never `reinterpret_cast` between unrelated object types to read data. |
| No implicit narrowing | Warnings are errors; cast explicitly with `static_cast` and check ranges first. |
| Deterministic timing | Every blocking operation has try / timed / blocking variants; the timed variant takes a `std::chrono` duration and uses `steady_clock`. Spin loops are bounded and back off. |
| Shutdown | Components with blocking waits expose `close()` (or `request_stop()`) that wakes every waiter, including waiters in other processes. |
| Justified NOLINTs | Every `NOLINT` carries a one-line justification (for example a C array that must live in shared memory). |

## 3. Concurrency

- Document thread-safety and process-safety for every public function in Doxygen (`Thread safety:` / `Blocking:` paragraphs).
- Anything placed in shared memory must be **address-free**: trivially copyable data, `std::atomic<T>` with `is_always_lock_free == true`
  (`static_assert` it), and pthread objects initialized with `PTHREAD_PROCESS_SHARED`. Never place pointers, `std::mutex`, `std::condition_variable`, or objects
  with vtables in shared memory; use offsets / indices instead.
- Shared layouts begin with a header containing a magic number, a layout version, and the configuration, so `open()` can reject an incompatible segment.
- Separate data written by different parties onto different cache lines (`alignas(systems::platform::kCacheLineSize)`) to avoid false sharing.
- Memory orders are chosen deliberately and commented (`acquire` / `release` pairs named in comments); `seq_cst` is acceptable when in doubt.
- A process that dies at any instruction must not leave others deadlocked or reading corrupt data, unless the spec explicitly says recovery is out of scope.
  On Linux / QNX use robust mutexes (`EOWNERDEAD` → repair → `pthread_mutex_consistent`); lock-free designs must tolerate a writer that never finishes.
- Priority inversion: when a mutex is shared with real-time threads, document whether `PTHREAD_PRIO_INHERIT` is used (available on Linux and QNX).

## 4. Portability and the OS-adapter rule

Production targets are Linux, embedded Linux, and QNX (all POSIX). macOS is a development host.

- Confine every OS call (`shm_open`, `mmap`, `ftruncate`, `pthread_*attr*`, `clock_nanosleep`, signals) to an **OS adapter**: a small class declared in the
  component header and implemented in `src/cpp/systems/<name>_posix.cpp`. Portable logic in `<name>.cpp` must not include POSIX headers. Porting to another RTOS
  then means rewriting one file.
- Branch on capabilities with `systems/platform.hpp` (`kHasRobustMutex`, `kHasProcessSharedCondvar`, `kMaxShmNameLength`, `kCacheLineSize`, ...), using
  `if constexpr`. Use a narrow `#if` only where a symbol does not exist on a platform (for example `pthread_mutexattr_setrobust` on macOS).
- Known macOS gaps: no robust mutexes; shared-memory names limited to 31 characters (use at most `kMaxShmNameLength` = 30); no `clock_nanosleep`;
  `sem_timedwait` missing; unnamed POSIX semaphores unsupported. Requirements that depend on these are verified on Linux (CI) and `GTEST_SKIP` on macOS with
  the missing capability named in the skip message.
- Include public POSIX headers (`<fcntl.h>`, `<sys/mman.h>`, `<pthread.h>`, `<unistd.h>`, `<csignal>`). The repo's clang-tidy ignores platform-private
  sub-headers (`sys/_types/*`, `bits/*`) for `misc-include-cleaner`.
- Avoid `std::chrono` user-defined literals (`10ms`) in this repo: `misc-include-cleaner` cannot attribute them to a header. Write
  `std::chrono::milliseconds{10}`.

## 5. Standards guidance (informative)

Cite these in the spec's "Standards guidance" table where they apply. They are pointers for learning, not a compliance claim; rule numbers should be confirmed
against licensed copies.

| Topic | Pointer |
| --- | --- |
| Unit design principles (no dynamic objects, no recursion, single entry / exit, limited pointers, no implicit conversions, no hidden data flow) | ISO 26262-6:2018 Table 6 |
| Language subset, strong typing, defensive implementation | ISO 26262-6:2018 Table 1 |
| Dynamic memory | MISRA C++:2023 Rule 21.6.1; AUTOSAR C++14 A18-5-1 |
| Casts | MISRA C++:2023 Rule 8.2.2 (no C-style casts); AUTOSAR C++14 A5-2-1 (`dynamic_cast`) |
| C arrays | AUTOSAR C++14 A18-1-1 (prefer `std::array`; justified exceptions for shared-memory layouts) |
| Process | ASPICE SWE.1 (requirements), SWE.3 (detailed design), SWE.4 (unit verification), bidirectional traceability |

Exceptions are banned here by **project policy** (common on ASIL projects for timing determinism and tool-qualification reasons); note that AUTOSAR C++14 itself
permits exceptions with restrictions.
