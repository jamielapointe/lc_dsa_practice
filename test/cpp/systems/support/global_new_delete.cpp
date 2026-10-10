/// @file global_new_delete.cpp
/// @brief Replacement global allocation functions ([new.delete]) for
/// `systems_test::AllocationScope`.
///
/// Every overload forwards to `malloc` / `aligned_alloc` / `free` so the
/// sanitizers still track each block. This translation unit is excluded from
/// clang-tidy (`SKIP_LINTING` in CMake): libc++ declares these functions with
/// reserved parameter names (`__sz`, `__p`), which
/// `readability-inconsistent-declaration-parameter-name` reports against the
/// library header and cannot be suppressed from here.

#include <cstddef>
#include <new>

#include "systems_test/allocation_counter.hpp"

// NOLINTBEGIN(misc-new-delete-overloads,cert-dcl54-cpp,*-owning-memory)
// Replacement global allocation functions ([new.delete]); all forward to
// malloc/free so ASan still tracks every block.
auto operator new(std::size_t bytes) -> void* {
  return systems_test::detail::throwing(
      systems_test::detail::checked_malloc(bytes));
}
auto operator new[](std::size_t bytes) -> void* {
  return systems_test::detail::throwing(
      systems_test::detail::checked_malloc(bytes));
}
auto operator new(std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept
    -> void* {
  return systems_test::detail::checked_malloc(bytes);
}
auto operator new[](std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept
    -> void* {
  return systems_test::detail::checked_malloc(bytes);
}
auto operator new(std::size_t bytes, std::align_val_t align) -> void* {
  return systems_test::detail::throwing(
      systems_test::detail::checked_aligned(bytes, align));
}
auto operator new[](std::size_t bytes, std::align_val_t align) -> void* {
  return systems_test::detail::throwing(
      systems_test::detail::checked_aligned(bytes, align));
}
auto operator new(std::size_t bytes, std::align_val_t align,
                  const std::nothrow_t& /*tag*/) noexcept -> void* {
  return systems_test::detail::checked_aligned(bytes, align);
}
auto operator new[](std::size_t bytes, std::align_val_t align,
                    const std::nothrow_t& /*tag*/) noexcept -> void* {
  return systems_test::detail::checked_aligned(bytes, align);
}
void operator delete(void* ptr) noexcept { systems_test::detail::release(ptr); }
void operator delete[](void* ptr) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete(void* ptr, std::size_t /*bytes*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete[](void* ptr, std::size_t /*bytes*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete(void* ptr, std::align_val_t /*align*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete[](void* ptr, std::align_val_t /*align*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete(void* ptr, std::size_t /*bytes*/,
                     std::align_val_t /*align*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete[](void* ptr, std::size_t /*bytes*/,
                       std::align_val_t /*align*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete(void* ptr, const std::nothrow_t& /*tag*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete[](void* ptr, const std::nothrow_t& /*tag*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete(void* ptr, std::align_val_t /*align*/,
                     const std::nothrow_t& /*tag*/) noexcept {
  systems_test::detail::release(ptr);
}
void operator delete[](void* ptr, std::align_val_t /*align*/,
                       const std::nothrow_t& /*tag*/) noexcept {
  systems_test::detail::release(ptr);
}
// NOLINTEND(misc-new-delete-overloads,cert-dcl54-cpp,*-owning-memory)
