/// @file allocation_counter.cpp
/// @brief `AllocationScope` plus replacement global `operator new/delete`.
///
/// The replacement global operators live in `global_new_delete.cpp` and call
/// the `detail` hooks defined here.

#include "systems_test/allocation_counter.hpp"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace systems_test {
namespace {

std::atomic<bool> g_armed{false};           // NOLINT(*-avoid-non-const-global*)
std::atomic<std::size_t> g_allocations{0};  // NOLINT(*-avoid-non-const-global*)

}  // namespace

namespace detail {

void note_allocation() noexcept {
  if (g_armed.load(std::memory_order_relaxed)) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
  }
}

auto checked_malloc(std::size_t bytes) noexcept -> void* {
  note_allocation();
  // NOLINTNEXTLINE(*-no-malloc,*-owning-memory): this IS the allocator.
  return std::malloc(bytes == 0 ? 1 : bytes);
}

auto checked_aligned(std::size_t bytes, std::align_val_t align) noexcept
    -> void* {
  note_allocation();
  const auto alignment = static_cast<std::size_t>(align);
  const std::size_t rounded = ((bytes + alignment - 1) / alignment) * alignment;
  // NOLINTNEXTLINE(*-no-malloc,*-owning-memory): this IS the allocator.
  return std::aligned_alloc(alignment, rounded == 0 ? alignment : rounded);
}

auto throwing(void* ptr) -> void* {
  if (ptr == nullptr) {
    throw std::bad_alloc{};
  }
  return ptr;
}

void release(void* ptr) noexcept {
  // NOLINTNEXTLINE(*-no-malloc,*-owning-memory): this IS the allocator.
  std::free(ptr);
}

}  // namespace detail

AllocationScope::AllocationScope() noexcept {
  bool expected = false;
  owner_ = g_armed.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel);
  if (owner_) {
    g_allocations.store(0, std::memory_order_relaxed);
  }
}

AllocationScope::~AllocationScope() {
  if (owner_) {
    g_armed.store(false, std::memory_order_release);
  }
}

auto AllocationScope::allocations() const noexcept -> std::size_t {
  if (!owner_) {
    return kNestedScope;
  }
  return g_allocations.load(std::memory_order_relaxed);
}

}  // namespace systems_test
