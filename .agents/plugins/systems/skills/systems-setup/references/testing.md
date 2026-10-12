# Testing Reference (GoogleTest + `systems::test_support`)

Used by `systems-setup` for Step 5. Tests are the executable form of the spec: they assert the behavior the spec requires, so they fail now (stubs) and pass
once the user's implementation is correct.

## Tiers

| Tier | Suite | Required | What it proves |
| --- | --- | --- | --- |
| 1 Functional | `<Name>Test` | Always | Each functional requirement at nominal inputs, one behavior per test |
| 2 Boundary / error | `<Name>Test` | Always | Minimum and maximum sizes, capacity 1, full / empty, wrap-around, every error enumerator, timeouts, use after `close()` |
| 3 Resource / invariants | `<Name>Test` | Always | No heap after init (`AllocationScope`), `noexcept` / trait `static_assert`s, RAII cleanup (no leaked shm names, fds), ASan / UBSan clean |
| 4 Multi-thread stress | `<Name>ConcurrencyTest` | Always | Many threads, start gate, invariants checked after join: nothing lost or duplicated, ordering, no torn data; TSan clean |
| 5 Multi-process | `<Name>MultiProcessTest` | If the component is shared across processes | Separate address spaces via `fork()`; same invariants as tier 4 |
| 6 Fault injection | `<Name>FaultTest` | If the spec has a failure-model requirement | SIGKILL a participant at arbitrary points (repeated), then verify survivors make progress and never observe corrupt data |

## Traceability

The first statement of every test declares the requirement IDs it verifies (comma-separated, no spaces):

```cpp
TEST(ShmPubSubTest, PublishAssignsIncreasingSequenceNumbers) {
  RecordProperty("requirements", "SHMPS-REQ-005");
  ...
}
```

`RecordProperty` lands in gtest XML output (`--gtest_output=xml`), and `pixi run check-traceability` checks every `Test` requirement has a test and every
referenced ID exists. Do not use `TEST_F` fixtures' `SetUp` to record properties; keep the call in each test body.

## Red-state safety (mandatory)

Stubs return `kNotImplemented`. Tests must turn that into clean assertion failures:

- `ASSERT_TRUE(result.has_value()) << to_string(result.error());` before dereferencing any `std::expected` / `std::optional`.
- **Every loop is bounded** by an iteration cap *and* a `steady_clock` deadline. A producer that retries "until published" spins forever against a stub;
  write `for (attempt = 0; attempt < kMaxAttempts && !done && now < deadline; ++attempt)` and assert on counts afterwards.
- Never call a *blocking* API (`receive()`, `pop()`) from the test thread unless something is guaranteed to unblock it within the test's deadline; prefer the
  timed variant with a short timeout, or run the blocking call in a thread / child that the test can unblock with `close()` and a bounded join.
- Threads: use `std::jthread` (auto-join) and a `std::latch` start gate; make sure each thread exits on its own deadline even if the component never
  responds, so joins cannot hang.
- Children: always `wait_for(timeout)`; a stub results in `kTimedOut` or a non-zero exit code, never a hung ctest. Keep each test under ~10 s and the suite
  under the 60 s ctest timeout, including under TSan.
- No wall-clock performance assertions tighter than ~10x the expected time (sanitizers slow code); keep timing tests single-threaded or out of the TSan preset
  (see the `tsan` test preset filter in `CMakePresets.json`).

## Shared test harness (`#include "systems_test/<header>.hpp"`, link `systems::test_support`)

| Header | API | Use |
| --- | --- | --- |
| `process.hpp` | `ChildProcess::spawn(body)`, `wait_for(timeout)`, `kill(sig)`, `send_signal(sig)` (no reap; `SIGSTOP` / `SIGCONT`), `ChildResult{status, code}`, `succeeded()`, `killed_by(sig)`, `kChildOk`, `wait_until(pred, timeout)` | Multi-process and fault-injection tiers. The child returns an exit code (0 = pass, distinct small integers per failed check). RAII kills + reaps on scope exit. |
| `shared_region.hpp` | `SharedRegion<T>` (anonymous `MAP_SHARED`, inherited by children) | Rendezvous flags, counters, and result tallies between parent and children. `T` must be trivially destructible; atomics must be always lock-free. |
| `shm_name.hpp` | `unique_shm_name(prefix)`, `ScopedShmUnlink`, `shm_exists(name)` | Parallel-safe, portable (<= 30 chars) names; cleanup even when assertions fail; verifying unlink / leak requirements. |
| `allocation_counter.hpp` | `AllocationScope`, `allocations()` | "No heap after init" requirements; counts `operator new` from all threads while alive. |
| `systems/platform.hpp` | `kHasRobustMutex`, `kHasProcessSharedCondvar`, `kIsMacos`, ... | `GTEST_SKIP()` requirements the host cannot satisfy. |

Never copy these helpers into a test file; extend the support library (with self-tests in `test_support_test.cpp`) if something is missing.

## Patterns

### Start gate and bounded workers (tier 4)

```cpp
constexpr int kThreads = 8;
constexpr auto kBudget = std::chrono::seconds{3};
std::latch start{kThreads + 1};
std::atomic<std::uint64_t> published{0};
{
  std::vector<std::jthread> workers;  // tests may allocate; the component may not
  workers.reserve(kThreads);          // clang-tidy: performance-inefficient-vector-operation
  for (int t = 0; t < kThreads; ++t) {
    workers.emplace_back([&, t] {
      start.arrive_and_wait();
      const auto deadline = std::chrono::steady_clock::now() + kBudget;
      for (int i = 0; i < kPerThread && std::chrono::steady_clock::now() < deadline; ++i) {
        if (channel->publish(make_payload(t, i)).has_value()) { published.fetch_add(1); }
      }
    });
  }
  start.arrive_and_wait();
}  // jthreads join here
EXPECT_EQ(published.load(), kThreads * kPerThread);
```

Encode the producer id and a per-producer counter in each payload, plus a checksum over the whole payload, so the consumer side can check "no loss, no
duplication, per-producer order, no torn data" with simple arithmetic after the join.

### Multi-process (tier 5)

```cpp
const auto name = systems_test::unique_shm_name("shmps");
const systems_test::ScopedShmUnlink cleanup{name};
auto created = ShmPubSub::create(name, config);
ASSERT_TRUE(created.has_value()) << to_string(created.error());

systems_test::SharedRegion<Tally> tally;   // results reported back by the child
ASSERT_TRUE(tally.valid());

auto child = systems_test::ChildProcess::spawn([&] {
  auto opened = ShmPubSub::open(name);     // the child maps the segment itself
  if (!opened) { return 1; }
  ... // do work, record into tally->..., return 2, 3, ... on distinct failures
  return systems_test::kChildOk;
});
... // parent side work with its own deadline
const auto result = child.wait_for(std::chrono::seconds{10});
EXPECT_TRUE(result.succeeded()) << result;
```

Rules: create shared objects in the parent *before* forking when the requirement is about "another process opens an existing segment"; let each child call
`open()` itself (do not rely on inherited mappings for the component under test). Fork only while the parent is single-threaded.

### Fault injection (tier 6)

Kill at many arbitrary points instead of one hand-picked point:

```cpp
if constexpr (!systems::platform::kHasRobustMutex) {
  GTEST_SKIP() << "requires robust mutexes (Linux / QNX); unavailable on this host";
}
for (int round = 0; round < kRounds; ++round) {
  auto victim = systems_test::ChildProcess::spawn([&] { /* publish in a tight loop forever */ return 0; });
  ASSERT_TRUE(systems_test::wait_until([&] { return progress_seen(); }, std::chrono::seconds{2}));
  std::this_thread::sleep_for(std::chrono::microseconds{jitter(round)});   // vary the kill point
  EXPECT_TRUE(victim.kill(SIGKILL).killed_by(SIGKILL));
  // Survivors: a fresh publisher must succeed within a deadline and a subscriber must never see a corrupt sample.
}
```

Gate a fault test with `GTEST_SKIP` only when the *mechanism* the spec requires is missing on the host, and name the capability in the message. Lock-free
designs that do not need robust mutexes should not skip.

### No heap after initialization (tier 3)

Initialize outside the scope, exercise every steady-state operation inside it, read the count before any other assertion (gtest may allocate on failure):

```cpp
std::size_t allocations = 0;
{
  const systems_test::AllocationScope scope;
  for (int i = 0; i < 1000; ++i) { static_cast<void>(channel->publish(payload)); static_cast<void>(sub.try_receive(buffer)); }
  allocations = scope.allocations();
}
EXPECT_EQ(allocations, 0U);
```

In the red state this test may pass trivially (stubs allocate nothing); pair it with an assertion that the operations actually succeeded so it is red too.

### Compile-time invariants

Put `static_assert`s for API properties at namespace scope in the test file (`std::is_nothrow_move_constructible_v`, `!std::is_copy_constructible_v`, that each
operation is `noexcept` via `noexcept(expr)`). These hold for stubs too, which is fine: they pin the contract. Build the arguments of a `noexcept(expr)` check
from non-throwing expressions: `std::string_view{}` rather than a string literal (the `const char*` constructor is not `noexcept`).

## Sanitizer and preset matrix

| Command | Expectation in the red state |
| --- | --- |
| `pixi run ctest --preset dev-debug -R '^<Name>'` | All `<Name>` tests fail or skip; no ASan / UBSan report, no timeout |
| `pixi run cmake --workflow --preset tsan` | Same, and no ThreadSanitizer report from any test |
| `pixi run test-cpp` | Only `<Name>` tests fail |

TSan and `fork()`: TSan supports forking a single-threaded process; children may start threads. Do not fork after starting threads in the same test.
