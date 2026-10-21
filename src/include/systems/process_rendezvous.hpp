#pragma once

/// @file process_rendezvous.hpp
/// @brief OS-agnostic cross-process initialization handshake (`ProducerReady`
/// and `ConsumerReady` latches) with platform-specific POSIX / Darwin / Linux
/// low-level wakeup adapters.
///
/// Problem
/// -------
/// Separate producer and consumer processes communicating over a lock-free
/// shared-memory ring buffer (`systems::ShmChannel`) must coordinate their
/// initialization so that:
///   1. the consumer waits until the producer has created the shared channel,
///   2. the producer waits until the consumer has opened the channel and
///      attached its subscriber cursor (`ShmReadMode::kLatest` or `kInOrder`)
///      before publishing the first sample, and
///   3. no coordination or system calls occur during the main data loop.
/// Requirements: `docs/systems/process_rendezvous.md` (prefix `PRV`).
///
/// Design summary
/// --------------
///   - Two monotonic latch stages (`ProducerReady` and `ConsumerReady`) stored
///     as cache-line-aligned lock-free atomics in a named shared-memory header.
///   - OS-level notification in `src/cpp/systems/process_rendezvous_posix.cpp`:
///     uses POSIX named semaphores (`sem_open` / `sem_post`) with kernel
///     `sem_timedwait` on Linux (`platform::kHasPosixSemTimedwait`) and
///     `sem_trywait` + bounded backoff on macOS Apple Silicon (where XNU omits
///     `sem_timedwait`).
///   - Crash-safe (`SIGKILL` resilient): holds no cross-process mutex locks, so
///     a process killed at any instruction never deadlocks surviving waiters.
///   - `attach(name, timeout)` atomically creates or opens the rendezvous,
///     tolerating whichever process starts first.
///
/// Thread / process safety
/// -----------------------
///   - All public member functions on a `ProcessRendezvous` handle are safe to
///     call concurrently from multiple threads and across multiple processes.
///
/// Blocking
/// --------
///   - `signal_*`, `is_*`, `try_wait_*`, `close()`, `is_closed()` never block.
///   - `wait_*_for(timeout)` blocks up to `timeout`; `wait_*()` blocks until
///     the stage is signaled or `close()` is called.
///
/// Memory
/// ------
/// `create()`, `open()`, and `attach()` map shared resources during
/// initialization. Every steady-state operation is heap-free (PRV-REQ-008).
///
/// Error handling
/// --------------
/// No exceptions, no RTTI: every operation is `noexcept` and reports errors via
/// `std::expected<..., ProcessRendezvousError>`.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>

namespace systems {

/// @brief Errors reported by `ProcessRendezvous`.
enum class [[nodiscard]] ProcessRendezvousError : std::uint8_t {
  kNotImplemented,    ///< Stub: operation has not been implemented yet.
  kInvalidArgument,   ///< Bad name, invalid timeout, or moved-from handle.
  kNameTooLong,       ///< Name longer than `platform::kMaxShmNameLength`.
  kAlreadyExists,     ///< `create`: an object with this name already exists.
  kNotFound,          ///< `open` / `unlink`: no object with this name.
  kLayoutMismatch,    ///< `open`: object is not a compatible rendezvous.
  kWouldBlock,        ///< `try_wait_*`: stage is not ready yet.
  kTimedOut,          ///< `wait_*_for` / `attach`: timeout elapsed.
  kClosed,            ///< Rendezvous closed before stage became ready.
  kMemoryLockFailed,  ///< `create` / `open`: pages could not be wired to RAM.
  kOsError,           ///< Unexpected operating-system failure.
};

/// @brief Human-readable name of a `ProcessRendezvousError`.
[[nodiscard]] constexpr auto to_string(ProcessRendezvousError error) noexcept
    -> std::string_view {
  switch (error) {
    case ProcessRendezvousError::kNotImplemented:
      return "not implemented";
    case ProcessRendezvousError::kInvalidArgument:
      return "invalid argument";
    case ProcessRendezvousError::kNameTooLong:
      return "name too long";
    case ProcessRendezvousError::kAlreadyExists:
      return "already exists";
    case ProcessRendezvousError::kNotFound:
      return "not found";
    case ProcessRendezvousError::kLayoutMismatch:
      return "layout mismatch";
    case ProcessRendezvousError::kWouldBlock:
      return "would block";
    case ProcessRendezvousError::kTimedOut:
      return "timed out";
    case ProcessRendezvousError::kClosed:
      return "closed";
    case ProcessRendezvousError::kMemoryLockFailed:
      return "memory lock failed";
    case ProcessRendezvousError::kOsError:
      return "operating-system error";
  }
  return "unknown error";
}

namespace detail {

/// @brief OS adapter managing the shared-memory mapping and platform-specific
/// wakeup primitive for `ProcessRendezvous` (PRV-REQ-011).
///
/// Implemented in `src/cpp/systems/process_rendezvous_posix.cpp`.
class RendezvousOsHandle {
 public:
  RendezvousOsHandle() noexcept = default;
  ~RendezvousOsHandle();

  RendezvousOsHandle(const RendezvousOsHandle&) = delete;
  auto operator=(const RendezvousOsHandle&) -> RendezvousOsHandle& = delete;

  RendezvousOsHandle(RendezvousOsHandle&& other) noexcept
      : base_{std::exchange(other.base_, nullptr)},
        size_{std::exchange(other.size_, 0)},
        sem_{std::exchange(other.sem_, nullptr)} {}

  auto operator=(RendezvousOsHandle&& other) noexcept -> RendezvousOsHandle& {
    if (this != &other) {
      RendezvousOsHandle discarded{std::move(*this)};
      base_ = std::exchange(other.base_, nullptr);
      size_ = std::exchange(other.size_, 0);
      sem_ = std::exchange(other.sem_, nullptr);
    }
    return *this;
  }

  /// @brief Creates a new zero-filled shared segment and wakeup semaphore.
  [[nodiscard]] static auto create(std::string_view name,
                                   std::size_t bytes) noexcept
      -> std::expected<RendezvousOsHandle, ProcessRendezvousError>;

  /// @brief Opens an existing shared segment and wakeup semaphore.
  [[nodiscard]] static auto open(std::string_view name) noexcept
      -> std::expected<RendezvousOsHandle, ProcessRendezvousError>;

  /// @brief Removes the shared segment and semaphore names.
  [[nodiscard]] static auto unlink(std::string_view name) noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief Broadcasts a wakeup hint to any waiting processes/threads.
  void notify() const noexcept;

  /// @brief Waits up to `slice` for a wakeup hint (uses `sem_timedwait` on
  /// Linux and `sem_trywait` + backoff on macOS).
  void wait_slice(std::chrono::nanoseconds slice) const noexcept;

  [[nodiscard]] auto data() const noexcept -> std::byte* { return base_; }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
  [[nodiscard]] auto valid() const noexcept -> bool { return base_ != nullptr; }

 private:
  RendezvousOsHandle(std::byte* base, std::size_t size, void* sem) noexcept
      : base_{base}, size_{size}, sem_{sem} {}

  std::byte* base_{nullptr};
  std::size_t size_{0};
  void* sem_{nullptr};
};

}  // namespace detail

/// @brief Named cross-process startup rendezvous handle (move-only).
///
/// Coordinates initialization between a producer process and one or more
/// consumer processes so the consumer is attached and waiting before the
/// producer begins its steady-state loop.
class ProcessRendezvous {
 public:
  ProcessRendezvous(const ProcessRendezvous&) = delete;
  auto operator=(const ProcessRendezvous&) -> ProcessRendezvous& = delete;
  ProcessRendezvous(ProcessRendezvous&&) noexcept = default;
  auto operator=(ProcessRendezvous&&) noexcept -> ProcessRendezvous& = default;
  ~ProcessRendezvous() = default;

  /// @brief Creates and initializes a new named rendezvous.
  /// @param name `/name`, at most `platform::kMaxShmNameLength` characters.
  /// @return `kInvalidArgument`, `kNameTooLong`, `kAlreadyExists`,
  ///         `kMemoryLockFailed`, `kOsError`.
  /// @par Requirements
  /// PRV-REQ-001, PRV-REQ-002
  [[nodiscard]] static auto create(std::string_view name) noexcept
      -> std::expected<ProcessRendezvous, ProcessRendezvousError>;

  /// @brief Opens an existing named rendezvous created by any process.
  /// @return `kInvalidArgument`, `kNameTooLong`, `kNotFound`,
  ///         `kLayoutMismatch`, `kMemoryLockFailed`, `kOsError`.
  /// @par Requirements
  /// PRV-REQ-001, PRV-REQ-002
  [[nodiscard]] static auto open(std::string_view name) noexcept
      -> std::expected<ProcessRendezvous, ProcessRendezvousError>;

  /// @brief Creates the rendezvous if absent, or opens it once initialized
  /// within `timeout` (allows producer and consumer to start in any order).
  /// @return `kInvalidArgument`, `kNameTooLong`, `kTimedOut`,
  ///         `kLayoutMismatch`, `kMemoryLockFailed`, `kOsError`.
  /// @par Requirements
  /// PRV-REQ-001, PRV-REQ-002
  [[nodiscard]] static auto attach(std::string_view name,
                                   std::chrono::nanoseconds timeout) noexcept
      -> std::expected<ProcessRendezvous, ProcessRendezvousError>;

  /// @brief Removes the rendezvous name (existing handles stay usable).
  /// @par Requirements
  /// PRV-REQ-002, PRV-REQ-003
  [[nodiscard]] static auto unlink(std::string_view name) noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief Marks the producer stage ready and wakes all waiters. Idempotent.
  /// @par Requirements
  /// PRV-REQ-004, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto signal_producer_ready() noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief True once `signal_producer_ready()` has been called.
  /// @par Requirements
  /// PRV-REQ-004, PRV-REQ-008
  [[nodiscard]] auto is_producer_ready() const noexcept -> bool;

  /// @brief Non-blocking check for producer readiness.
  /// @return `{}` if ready, `kClosed` if closed before ready, `kWouldBlock`
  ///         otherwise.
  /// @par Requirements
  /// PRV-REQ-004, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto try_wait_producer_ready() const noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief Waits up to `timeout` for producer readiness.
  /// @par Requirements
  /// PRV-REQ-004, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto wait_producer_ready_for(std::chrono::nanoseconds timeout)
      const noexcept -> std::expected<void, ProcessRendezvousError>;

  /// @brief Waits until producer readiness is signaled or `close()` is called.
  /// @par Requirements
  /// PRV-REQ-004, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto wait_producer_ready() const noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief Marks the consumer stage ready and wakes all waiters. Idempotent.
  /// @par Requirements
  /// PRV-REQ-005, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto signal_consumer_ready() noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief True once `signal_consumer_ready()` has been called.
  /// @par Requirements
  /// PRV-REQ-005, PRV-REQ-008
  [[nodiscard]] auto is_consumer_ready() const noexcept -> bool;

  /// @brief Non-blocking check for consumer readiness.
  /// @return `{}` if ready, `kClosed` if closed before ready, `kWouldBlock`
  ///         otherwise.
  /// @par Requirements
  /// PRV-REQ-005, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto try_wait_consumer_ready() const noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief Waits up to `timeout` for consumer readiness.
  /// @par Requirements
  /// PRV-REQ-005, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto wait_consumer_ready_for(std::chrono::nanoseconds timeout)
      const noexcept -> std::expected<void, ProcessRendezvousError>;

  /// @brief Waits until consumer readiness is signaled or `close()` is called.
  /// @par Requirements
  /// PRV-REQ-005, PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto wait_consumer_ready() const noexcept
      -> std::expected<void, ProcessRendezvousError>;

  /// @brief Closes the rendezvous and wakes all blocked waiters. Idempotent.
  /// @par Requirements
  /// PRV-REQ-006, PRV-REQ-008
  void close() noexcept;

  /// @brief True once any handle has called `close()`.
  /// @par Requirements
  /// PRV-REQ-006, PRV-REQ-008
  [[nodiscard]] auto is_closed() const noexcept -> bool;

 private:
  explicit ProcessRendezvous(detail::RendezvousOsHandle handle) noexcept
      : handle_{std::move(handle)} {}

  detail::RendezvousOsHandle handle_;
};

}  // namespace systems
