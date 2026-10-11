#pragma once

/// @file shared_region.hpp
/// @brief Anonymous `MAP_SHARED` memory that a test parent shares with its
/// forked children (rendezvous flags, counters, result slots).
///
/// This is test scaffolding, not the component under test: components that
/// need *named* shared memory must create it themselves (through their OS
/// adapter) so the test exercises their real code path.
///
/// Only place types in a region that are valid when accessed from several
/// processes at once: trivially copyable data and lock-free (address-free)
/// atomics. `std::atomic<T>` is only guaranteed to work across processes when
/// `std::atomic<T>::is_always_lock_free` is true; this is enforced below.

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace systems_test {

namespace detail {

/// Owns an anonymous shared mapping of `bytes` bytes (page-aligned, zeroed).
class AnonymousSharedMapping {
 public:
  explicit AnonymousSharedMapping(std::size_t bytes) noexcept;
  ~AnonymousSharedMapping();

  AnonymousSharedMapping(const AnonymousSharedMapping&) = delete;
  auto operator=(const AnonymousSharedMapping&)
      -> AnonymousSharedMapping& = delete;
  AnonymousSharedMapping(AnonymousSharedMapping&& other) noexcept
      : base_{std::exchange(other.base_, nullptr)},
        bytes_{std::exchange(other.bytes_, 0)} {}
  auto operator=(AnonymousSharedMapping&&) -> AnonymousSharedMapping& = delete;

  [[nodiscard]] auto data() const noexcept -> void* { return base_; }
  [[nodiscard]] auto valid() const noexcept -> bool { return base_ != nullptr; }

 private:
  void* base_{nullptr};
  std::size_t bytes_{0};
};

/// True unless `T` is (an array of) a `std::atomic` that is not always
/// lock-free. Atomics in user structs must be checked by the user with
/// `static_assert(std::atomic<U>::is_always_lock_free)`.
template <typename T>
struct IsAddressFree : std::true_type {};

template <typename U>
struct IsAddressFree<std::atomic<U>>
    : std::bool_constant<std::atomic<U>::is_always_lock_free> {};

template <typename U, std::size_t N>
// NOLINTNEXTLINE(*-avoid-c-arrays): trait must match C array types.
struct IsAddressFree<U[N]> : IsAddressFree<U> {};

}  // namespace detail

/// @brief A `T` living in memory shared with forked children.
///
/// Construct the region in the parent *before* `ChildProcess::spawn`; the
/// child inherits the mapping, so writes from either side are visible to the
/// other. `T` is value-initialized in place.
///
/// @tparam T Trivially destructible type: plain data, an always-lock-free
///         `std::atomic`, or a struct of those (for example a rendezvous
///         block of `std::atomic<std::uint32_t>` flags).
template <typename T>
class SharedRegion {
  static_assert(std::is_trivially_destructible_v<T>,
                "SharedRegion<T> requires a trivially destructible type");
  static_assert(detail::IsAddressFree<T>::value,
                "std::atomic in shared memory must be always lock-free");

 public:
  SharedRegion() noexcept : mapping_{sizeof(T)} {
    if (mapping_.valid()) {
      object_ = ::new (mapping_.data()) T{};
    }
  }

  ~SharedRegion() {
    if (object_ != nullptr) {
      std::destroy_at(object_);
    }
  }

  SharedRegion(const SharedRegion&) = delete;
  auto operator=(const SharedRegion&) -> SharedRegion& = delete;
  SharedRegion(SharedRegion&&) = delete;
  auto operator=(SharedRegion&&) -> SharedRegion& = delete;

  /// @brief False if `mmap` failed; check with `ASSERT_TRUE` before use.
  [[nodiscard]] auto valid() const noexcept -> bool {
    return object_ != nullptr;
  }

  [[nodiscard]] auto get() const noexcept -> T& { return *object_; }
  auto operator*() const noexcept -> T& { return *object_; }
  auto operator->() const noexcept -> T* { return object_; }

 private:
  detail::AnonymousSharedMapping mapping_;
  T* object_{nullptr};
};

}  // namespace systems_test
