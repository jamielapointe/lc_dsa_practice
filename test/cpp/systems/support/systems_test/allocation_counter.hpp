#pragma once

/// @file allocation_counter.hpp
/// @brief Counts global `operator new` calls so tests can prove that
/// steady-state operations never touch the heap.
///
/// Linking `systems::test_support` *and* using `AllocationScope` pulls in
/// replacement definitions of every global `operator new` / `operator delete`
/// overload (forwarding to `malloc` / `free`, which sanitizers still track).
/// While an `AllocationScope` is alive, each allocation from **any** thread
/// increments a counter.
///
/// Typical use (initialization outside the scope, steady state inside):
///
/// @code
///   auto channel = Channel::create(name, config);   // may allocate (init)
///   ASSERT_TRUE(channel.has_value());
///   const systems_test::AllocationScope scope;
///   for (int i = 0; i < 1000; ++i) {
///     static_cast<void>(channel->publish(payload));
///   }
///   EXPECT_EQ(scope.allocations(), 0U);
/// @endcode
///
/// Limitations: C `malloc`/`calloc`/`realloc` calls are not counted (ban them
/// in systems code instead), and gtest itself may allocate when an assertion
/// fails inside the scope, so read the count before asserting on anything
/// else.

#include <cstddef>
#include <new>

namespace systems_test {

namespace detail {
// Internal hooks used by the replacement global operators
// (global_new_delete.cpp). Not part of the test-facing API.
void note_allocation() noexcept;
[[nodiscard]] auto checked_malloc(std::size_t bytes) noexcept -> void*;
[[nodiscard]] auto checked_aligned(std::size_t bytes,
                                   std::align_val_t align) noexcept -> void*;
[[nodiscard]] auto throwing(void* ptr) -> void*;
void release(void* ptr) noexcept;
}  // namespace detail

/// @brief Counts heap allocations made while the scope is alive.
///
/// Scopes do not nest; creating a second live scope is a test bug (it is
/// detected and reported as `allocations() == kNestedScope`).
class AllocationScope {
 public:
  /// @brief Sentinel returned by `allocations()` for an invalid nested scope.
  static constexpr std::size_t kNestedScope = static_cast<std::size_t>(-1);

  AllocationScope() noexcept;
  ~AllocationScope();

  AllocationScope(const AllocationScope&) = delete;
  auto operator=(const AllocationScope&) -> AllocationScope& = delete;
  AllocationScope(AllocationScope&&) = delete;
  auto operator=(AllocationScope&&) -> AllocationScope& = delete;

  /// @brief Number of `operator new` calls since the scope began.
  [[nodiscard]] auto allocations() const noexcept -> std::size_t;

 private:
  bool owner_{false};
};

}  // namespace systems_test
