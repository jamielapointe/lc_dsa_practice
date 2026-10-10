#pragma once

/// @file bounded_fifo.hpp
/// @brief Fixed-capacity, thread-safe (MPMC) FIFO queue with no heap allocation
/// and no STL containers.
///
/// `systems::BoundedFifo<T, Capacity>` is a ring buffer whose storage lives
/// inside the object itself (a plain C array of `std::optional<T>` slots), so
/// it can be placed in static storage or on the stack, which makes it a
/// reasonable starting point for embedded / systems code. Synchronization uses
/// a single `std::mutex` plus two `std::condition_variable`s (not-empty /
/// not-full).
///
/// Thread safety:
///   - Any number of producers and consumers may call any member function
///   concurrently.
///   - FIFO order is preserved with respect to the order in which pushes
///   acquire the internal lock.
///
/// Shutdown:
///   - `close()` wakes every blocked thread. After close, pushes fail (`false`)
///   while pops keep
///     draining the remaining elements and then return `std::nullopt`.
///
/// Exception safety:
///   - Element construction happens before any index is updated, so a throwing
///   constructor leaves
///     the queue unchanged (strong guarantee). `T` must be
///     nothrow-destructible.
///
/// Complexity: every operation is O(1) (excluding time spent blocked).

#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace systems {

/// @brief Bounded multi-producer / multi-consumer FIFO.
/// @tparam T Element type; must be nothrow-destructible.
/// @tparam Capacity Maximum number of elements; must be greater than zero.
template <typename T, std::size_t Capacity>
class BoundedFifo {
  static_assert(Capacity > 0, "BoundedFifo capacity must be greater than zero");
  static_assert(std::is_nothrow_destructible_v<T>,
                "BoundedFifo requires a nothrow-destructible element type");

 public:
  using value_type = T;

  BoundedFifo() = default;
  ~BoundedFifo() = default;

  BoundedFifo(const BoundedFifo&) = delete;
  auto operator=(const BoundedFifo&) -> BoundedFifo& = delete;
  BoundedFifo(BoundedFifo&&) = delete;
  auto operator=(BoundedFifo&&) -> BoundedFifo& = delete;

  /// @brief Maximum number of elements the queue can hold.
  [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t {
    return Capacity;
  }

  /// @brief Current number of queued elements (a snapshot; may be stale
  /// immediately).
  [[nodiscard]] auto size() const -> std::size_t {
    const std::scoped_lock lock{mutex_};
    return count_;
  }

  /// @brief True when no element is queued (snapshot).
  [[nodiscard]] auto empty() const -> bool { return size() == 0; }

  /// @brief True when `capacity()` elements are queued (snapshot).
  [[nodiscard]] auto full() const -> bool { return size() == Capacity; }

  /// @brief True once `close()` has been called.
  [[nodiscard]] auto closed() const -> bool {
    const std::scoped_lock lock{mutex_};
    return closed_;
  }

  /// @brief Stops accepting new elements and wakes every blocked producer and
  /// consumer.
  void close() {
    {
      const std::scoped_lock lock{mutex_};
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  /// @brief Constructs an element in place if there is room; never blocks.
  /// @return `true` if the element was enqueued; `false` if the queue is full
  /// or closed.
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  [[nodiscard]] auto try_emplace(Args&&... args) -> bool {
    {
      const std::scoped_lock lock{mutex_};
      if (closed_ || count_ == Capacity) {
        return false;
      }
      emplace_back_locked(std::forward<Args>(args)...);
    }
    not_empty_.notify_one();
    return true;
  }

  /// @brief Copies `value` into the queue if there is room; never blocks.
  [[nodiscard]] auto try_push(const T& value) -> bool {
    return try_emplace(value);
  }

  /// @brief Moves `value` into the queue if there is room; `value` is untouched
  /// on failure.
  [[nodiscard]] auto try_push(T&& value) -> bool {
    return try_emplace(std::move(value));
  }

  /// @brief Constructs an element in place, blocking while the queue is full.
  /// @return `true` if enqueued; `false` if the queue was (or became) closed.
  template <typename... Args>
    requires std::constructible_from<T, Args...>
  [[nodiscard]] auto emplace(Args&&... args) -> bool {
    std::unique_lock lock{mutex_};
    not_full_.wait(lock, [this] { return closed_ || count_ < Capacity; });
    if (closed_) {
      return false;
    }
    emplace_back_locked(std::forward<Args>(args)...);
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// @brief Copies `value` into the queue, blocking while full.
  [[nodiscard]] auto push(const T& value) -> bool { return emplace(value); }

  /// @brief Moves `value` into the queue, blocking while full; `value` is
  /// untouched on failure.
  [[nodiscard]] auto push(T&& value) -> bool {
    return emplace(std::move(value));
  }

  /// @brief Like `emplace` but gives up after `timeout`.
  /// @return `true` if enqueued; `false` on timeout or if the queue is closed.
  template <typename Rep, typename Period, typename... Args>
    requires std::constructible_from<T, Args...>
  [[nodiscard]] auto emplace_for(std::chrono::duration<Rep, Period> timeout,
                                 Args&&... args) -> bool {
    std::unique_lock lock{mutex_};
    if (!not_full_.wait_for(lock, timeout,
                            [this] { return closed_ || count_ < Capacity; }) ||
        closed_) {
      return false;
    }
    emplace_back_locked(std::forward<Args>(args)...);
    lock.unlock();
    not_empty_.notify_one();
    return true;
  }

  /// @brief Removes and returns the oldest element if one is queued; never
  /// blocks.
  [[nodiscard]] auto try_pop() -> std::optional<T> {
    std::unique_lock lock{mutex_};
    return pop_front_locked(lock);
  }

  /// @brief Removes and returns the oldest element, blocking while the queue is
  /// empty.
  /// @return The element, or `std::nullopt` once the queue is closed and fully
  /// drained.
  [[nodiscard]] auto pop() -> std::optional<T> {
    std::unique_lock lock{mutex_};
    not_empty_.wait(lock, [this] { return closed_ || count_ > 0; });
    return pop_front_locked(lock);
  }

  /// @brief Like `pop` but gives up after `timeout`.
  /// @return The element, or `std::nullopt` on timeout or when closed and
  /// drained.
  template <typename Rep, typename Period>
  [[nodiscard]] auto pop_for(std::chrono::duration<Rep, Period> timeout)
      -> std::optional<T> {
    std::unique_lock lock{mutex_};
    if (!not_empty_.wait_for(lock, timeout,
                             [this] { return closed_ || count_ > 0; })) {
      return std::nullopt;
    }
    return pop_front_locked(lock);
  }

 private:
  /// Appends an element; the caller holds `mutex_` and has verified there is
  /// room.
  template <typename... Args>
  void emplace_back_locked(Args&&... args) {
    const std::size_t tail = (head_ + count_) % Capacity;
    slots_[tail].emplace(
        std::forward<Args>(args)...);  // May throw; indices not yet updated.
    ++count_;
  }

  /// Pops the oldest element, releasing `lock` before waking a producer.
  auto pop_front_locked(std::unique_lock<std::mutex>& lock)
      -> std::optional<T> {
    if (count_ == 0) {
      return std::nullopt;
    }
    std::optional<T>& slot = slots_[head_];
    std::optional<T> out{std::in_place,
                         std::move(*slot)};  // May throw; queue unchanged.
    slot.reset();
    head_ = (head_ + 1) % Capacity;
    --count_;
    lock.unlock();
    not_full_.notify_one();
    return out;
  }

  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  // Intentionally a C array: this container is built without STL containers (no
  // std::array).
  // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays)
  std::optional<T> slots_[Capacity];
  std::size_t head_{0};
  std::size_t count_{0};
  bool closed_{false};
};

}  // namespace systems
