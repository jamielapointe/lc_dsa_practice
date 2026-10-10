#pragma once

/// @file shm_pubsub.hpp
/// @brief Single-publisher / multi-subscriber channel over a POSIX
/// shared-memory ring buffer, safe across threads and processes.
///
/// Problem
/// -------
/// A publisher writes fixed-size blocks (slots 0..n-1 of a ring) into named
/// shared memory; any number of subscribers in any process read them at their
/// own pace. Requirements: `docs/systems/shm_pubsub.md` (prefix `SHMPS`).
///
/// Design summary (decided in the spec)
/// ------------------------------------
///   - Overwrite oldest: `publish()` never blocks; a subscriber that falls a
///     full ring behind skips ahead and is told how many samples it missed.
///   - Exactly one publisher at a time; any number of subscribers, each with a
///     private cursor (the publisher never knows about subscribers).
///   - Sizes are fixed at `create()`; `open()` reads them from the segment.
///   - A publisher killed at any instruction never exposes a torn sample, and
///     a replacement publisher can attach once the old process is dead.
///
/// Thread / process safety
/// -----------------------
///   - The shared channel supports one publisher and many subscribers
///     concurrently, from any mix of threads and processes.
///   - Each `ShmPublisher` / `ShmSubscriber` handle is used by one thread at a
///     time. A `ShmChannel` handle may be shared by threads in one process
///     (all its member functions are safe to call concurrently).
///   - Handles hold a non-owning pointer to their `ShmChannel`; the channel
///     handle must outlive them and must not be moved while they exist.
///
/// Blocking
/// --------
///   - `publish()`, `try_receive()`, `close()`, `is_closed()` never block.
///   - `receive_for()` blocks up to its timeout; `receive()` blocks until a
///     sample arrives or the channel is closed. Both wake within 100 ms of a
///     publish or close, including from another process.
///
/// Memory
/// ------
/// `create()`, `open()`, and `attach()` may allocate / map memory. Every other
/// operation is heap-free (SHMPS-REQ-018).
///
/// Error handling
/// --------------
/// No exceptions, no RTTI: every operation is `noexcept` and reports failure
/// through `std::expected<..., ShmPubSubError>`.
///
/// Portability
/// -----------
/// All OS calls live in `src/cpp/systems/shm_pubsub_posix.cpp`
/// (`detail::SharedMemoryRegion`). Names are limited to
/// `systems::platform::kMaxShmNameLength` characters. The single-writer,
/// lock-free design needs no robust mutexes, so every requirement is testable
/// on macOS as well as Linux / QNX.
///
/// Implementation outline (your work)
/// ----------------------------------
///   1. OS adapter (`shm_pubsub_posix.cpp`): implement
///      `detail::SharedMemoryRegion::create` (`shm_open` with
///      `O_CREAT | O_EXCL`, `ftruncate`, `mmap(MAP_SHARED)`), `open` (map an
///      existing object at its current size via `fstat`), `unlink`, the
///      destructor (`munmap`), and `process_alive(pid)`. Map `errno` values to
///      `ShmPubSubError` (`EEXIST` -> `kAlreadyExists`, `ENOENT` ->
///      `kNotFound`, everything else -> `kOsError`). Close descriptors after
///      mapping; retry on `EINTR`.
///   2. Shared layout (`shm_pubsub.cpp`, private): a header with magic,
///      layout version, configuration, the publisher's claim (for example its
///      pid), the next sequence to publish, and a `closed` flag; then
///      `slot_count` slots, each with a per-slot sequence counter, a payload
///      size, and `slot_size` payload bytes. Keep the header and each slot
///      on separate cache lines (`systems::platform::kCacheLineSize`).
///      `static_assert` that every atomic you place there is always lock-free.
///   3. `ShmChannel::create` / `open`: validate the name and configuration,
///      compute the total size, create or map the region, initialize the
///      layout (create) or validate it (open, `kLayoutMismatch`).
///   4. `ShmPublisher::attach`: claim the publisher role atomically; if a
///      claim exists, take it over only when its owner process is dead.
///      Release the claim in the destructor. After a takeover, repair any
///      slot the dead publisher left half-written.
///   5. `publish`: pick slot `(sequence - 1) % slot_count`, mark it "being
///      written" (for example an odd per-slot counter), copy the payload,
///      then publish it with release ordering and bump the next sequence.
///   6. `try_receive`: compare your cursor with the newest committed
///      sequence, detect overrun (compute `missed`, jump to the oldest slot
///      still valid), copy the slot out, and re-check the slot's counter after
///      the copy; retry if it changed (seqlock read side).
///   7. `receive_for` / `receive`: wait for "new sequence or closed" with a
///      bounded mechanism (process-shared condition variable, futex, or
///      polling with exponential back-off capped at a few milliseconds).
///   8. `close` / `is_closed`: set and read the shared `closed` flag; wake
///      waiters.
///
/// Pitfalls
/// --------
///   - Torn reads: a slot can be overwritten while you copy it; validate after
///     the copy, not just before.
///   - Overrun arithmetic with 64-bit sequences; a subscriber that attached
///     before the first publish; payloads of size 0.
///   - `kBufferTooSmall` must not consume the sample.
///   - Memory ordering: payload writes must be visible before the sequence
///     that publishes them (release / acquire, or explicit fences).
///   - Pointers are meaningless in another process: store indices / offsets.
///   - macOS: no `sem_timedwait`, no robust mutexes, 31-character shm names.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

namespace systems {

/// @brief Errors reported by the shared-memory pub/sub channel.
enum class [[nodiscard]] ShmPubSubError : std::uint8_t {
  kNotImplemented,   ///< Stub: the operation has not been implemented yet.
  kInvalidArgument,  ///< Bad name syntax or configuration out of range.
  kNameTooLong,      ///< Name longer than `platform::kMaxShmNameLength`.
  kAlreadyExists,    ///< `create`: an object with this name already exists.
  kNotFound,         ///< `open` / `unlink`: no object with this name.
  kLayoutMismatch,   ///< `open`: object is not a compatible channel.
  kPublisherExists,  ///< `ShmPublisher::attach`: a live publisher exists.
  kPayloadTooLarge,  ///< `publish`: payload larger than `slot_size`.
  kBufferTooSmall,   ///< Receive: buffer smaller than the next sample.
  kWouldBlock,       ///< `try_receive`: no unread sample.
  kTimedOut,         ///< `receive_for`: timeout elapsed.
  kClosed,           ///< Channel closed (and, for receives, drained).
  kOsError,          ///< Unexpected operating-system failure.
};

/// @brief Human-readable name of an error (for logs and test messages).
[[nodiscard]] constexpr auto to_string(ShmPubSubError error) noexcept
    -> std::string_view {
  switch (error) {
    case ShmPubSubError::kNotImplemented:
      return "not implemented";
    case ShmPubSubError::kInvalidArgument:
      return "invalid argument";
    case ShmPubSubError::kNameTooLong:
      return "name too long";
    case ShmPubSubError::kAlreadyExists:
      return "already exists";
    case ShmPubSubError::kNotFound:
      return "not found";
    case ShmPubSubError::kLayoutMismatch:
      return "layout mismatch";
    case ShmPubSubError::kPublisherExists:
      return "publisher exists";
    case ShmPubSubError::kPayloadTooLarge:
      return "payload too large";
    case ShmPubSubError::kBufferTooSmall:
      return "buffer too small";
    case ShmPubSubError::kWouldBlock:
      return "would block";
    case ShmPubSubError::kTimedOut:
      return "timed out";
    case ShmPubSubError::kClosed:
      return "closed";
    case ShmPubSubError::kOsError:
      return "operating-system error";
  }
  return "unknown error";
}

/// @brief Smallest allowed number of slots.
inline constexpr std::uint32_t kShmMinSlotCount = 2;
/// @brief Largest allowed number of slots.
inline constexpr std::uint32_t kShmMaxSlotCount = 4096;
/// @brief Largest allowed payload size per slot, in bytes.
inline constexpr std::uint32_t kShmMaxSlotSize = 64U * 1024U;

/// @brief Channel geometry, fixed at creation (SHMPS-REQ-001).
struct ShmPubSubConfig {
  /// Number of slots; a power of two in `[kShmMinSlotCount, kShmMaxSlotCount]`.
  std::uint32_t slot_count{0};
  /// Maximum payload bytes per slot; in `[1, kShmMaxSlotSize]`.
  std::uint32_t slot_size{0};

  friend constexpr auto operator==(const ShmPubSubConfig&,
                                   const ShmPubSubConfig&) -> bool = default;
};

/// @brief Metadata of a received sample (payload bytes go to the caller's
/// buffer).
struct ShmSample {
  std::uint64_t sequence{0};  ///< Publisher-assigned number, starting at 1.
  std::uint32_t size{0};      ///< Payload bytes written to the buffer.
  std::uint64_t missed{0};    ///< Samples skipped since the previous receive.
};

namespace detail {

/// @brief OS adapter: a mapped POSIX shared-memory object (move-only).
///
/// The only type that calls the operating system; implemented in
/// `shm_pubsub_posix.cpp` (SHMPS-REQ-021). It does not validate names; the
/// caller does.
class SharedMemoryRegion {
 public:
  SharedMemoryRegion() noexcept = default;
  ~SharedMemoryRegion();

  SharedMemoryRegion(const SharedMemoryRegion&) = delete;
  auto operator=(const SharedMemoryRegion&) -> SharedMemoryRegion& = delete;
  SharedMemoryRegion(SharedMemoryRegion&& other) noexcept
      : base_{std::exchange(other.base_, nullptr)},
        size_{std::exchange(other.size_, 0)} {}
  auto operator=(SharedMemoryRegion&& other) noexcept -> SharedMemoryRegion& {
    if (this != &other) {
      SharedMemoryRegion discarded{std::move(*this)};
      base_ = std::exchange(other.base_, nullptr);
      size_ = std::exchange(other.size_, 0);
    }
    return *this;
  }

  /// @brief Creates a new zero-filled object of `bytes` bytes and maps it.
  /// @return `kAlreadyExists`, `kOsError`.
  [[nodiscard]] static auto create(std::string_view name,
                                   std::size_t bytes) noexcept
      -> std::expected<SharedMemoryRegion, ShmPubSubError>;

  /// @brief Maps an existing object at its current size.
  /// @return `kNotFound`, `kOsError`.
  [[nodiscard]] static auto open(std::string_view name) noexcept
      -> std::expected<SharedMemoryRegion, ShmPubSubError>;

  /// @brief Removes the name; existing mappings stay valid.
  /// @return `kNotFound`, `kOsError`.
  [[nodiscard]] static auto unlink(std::string_view name) noexcept
      -> std::expected<void, ShmPubSubError>;

  /// @brief True if process `pid` exists (used for publisher takeover).
  [[nodiscard]] static auto process_alive(std::int64_t pid) noexcept -> bool;

  /// @brief Id of the calling process.
  [[nodiscard]] static auto current_process_id() noexcept -> std::int64_t;

  [[nodiscard]] auto data() const noexcept -> std::byte* { return base_; }
  [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
  [[nodiscard]] auto valid() const noexcept -> bool { return base_ != nullptr; }

 private:
  SharedMemoryRegion(std::byte* base, std::size_t size) noexcept
      : base_{base}, size_{size} {}

  std::byte* base_{nullptr};
  std::size_t size_{0};
};

}  // namespace detail

/// @brief A process's handle to a named channel (owns the mapping).
///
/// Move-only. Destroying it unmaps the memory but does not unlink the name
/// (SHMPS-REQ-019). Must outlive (and not be moved while referenced by) any
/// `ShmPublisher` / `ShmSubscriber` attached to it.
class ShmChannel {
 public:
  ShmChannel(const ShmChannel&) = delete;
  auto operator=(const ShmChannel&) -> ShmChannel& = delete;
  ShmChannel(ShmChannel&&) noexcept = default;
  auto operator=(ShmChannel&&) noexcept -> ShmChannel& = default;
  ~ShmChannel() = default;

  /// @brief Creates and initializes a new channel.
  /// @param name `/name`, at most `platform::kMaxShmNameLength` characters.
  /// @param config Geometry (validated).
  /// @return `kInvalidArgument`, `kNameTooLong`, `kAlreadyExists`, `kOsError`.
  /// @par Requirements
  /// SHMPS-REQ-001, SHMPS-REQ-002, SHMPS-REQ-003
  [[nodiscard]] static auto create(std::string_view name,
                                   const ShmPubSubConfig& config) noexcept
      -> std::expected<ShmChannel, ShmPubSubError>;

  /// @brief Opens an existing channel created by any process.
  /// @return `kInvalidArgument`, `kNameTooLong`, `kNotFound`,
  ///         `kLayoutMismatch`, `kOsError`.
  /// @par Requirements
  /// SHMPS-REQ-004, SHMPS-REQ-005
  [[nodiscard]] static auto open(std::string_view name) noexcept
      -> std::expected<ShmChannel, ShmPubSubError>;

  /// @brief Removes the channel name (existing handles keep working).
  /// @return `kInvalidArgument`, `kNameTooLong`, `kNotFound`, `kOsError`.
  /// @par Requirements
  /// SHMPS-REQ-006
  [[nodiscard]] static auto unlink(std::string_view name) noexcept
      -> std::expected<void, ShmPubSubError>;

  /// @brief Geometry the channel was created with.
  [[nodiscard]] auto config() const noexcept -> ShmPubSubConfig;

  /// @brief Closes the channel for every process: publishing stops, readers
  /// drain and then see `kClosed`, blocked readers wake. Idempotent.
  /// @par Requirements
  /// SHMPS-REQ-014
  void close() noexcept;

  /// @brief True once any process has called `close()`.
  [[nodiscard]] auto is_closed() const noexcept -> bool;

 private:
  friend class ShmPublisher;
  friend class ShmSubscriber;

  explicit ShmChannel(detail::SharedMemoryRegion region) noexcept
      : region_{std::move(region)} {}

  // TODO(SHMPS): add the state your design needs (for example a typed pointer
  // to the shared header inside region_).
  detail::SharedMemoryRegion region_;
};

/// @brief The single writer of a channel.
///
/// Move-only; releases the publisher role when destroyed (SHMPS-REQ-007).
class ShmPublisher {
 public:
  ShmPublisher(const ShmPublisher&) = delete;
  auto operator=(const ShmPublisher&) -> ShmPublisher& = delete;
  ShmPublisher(ShmPublisher&& other) noexcept
      : channel_{std::exchange(other.channel_, nullptr)} {}
  auto operator=(ShmPublisher&& other) noexcept -> ShmPublisher&;
  ~ShmPublisher();

  /// @brief Claims the publisher role on `channel`.
  /// @return `kPublisherExists` while another live publisher holds it,
  ///         `kClosed` if the channel is closed.
  /// @par Requirements
  /// SHMPS-REQ-007, SHMPS-REQ-020
  [[nodiscard]] static auto attach(ShmChannel& channel) noexcept
      -> std::expected<ShmPublisher, ShmPubSubError>;

  /// @brief Copies `payload` into the next slot without blocking.
  /// @return The sample's sequence number; `kPayloadTooLarge`, `kClosed`.
  /// @par Requirements
  /// SHMPS-REQ-008, SHMPS-REQ-009, SHMPS-REQ-018
  [[nodiscard]] auto publish(std::span<const std::byte> payload) noexcept
      -> std::expected<std::uint64_t, ShmPubSubError>;

 private:
  explicit ShmPublisher(ShmChannel& channel) noexcept : channel_{&channel} {}

  // TODO(SHMPS): add the state your design needs.
  ShmChannel* channel_{nullptr};
};

/// @brief One reader of a channel with its own cursor.
///
/// Move-only. Holds no shared state, so a crashed subscriber needs no
/// recovery.
class ShmSubscriber {
 public:
  ShmSubscriber(const ShmSubscriber&) = delete;
  auto operator=(const ShmSubscriber&) -> ShmSubscriber& = delete;
  ShmSubscriber(ShmSubscriber&& other) noexcept
      : channel_{std::exchange(other.channel_, nullptr)},
        next_sequence_{std::exchange(other.next_sequence_, 0)} {}
  auto operator=(ShmSubscriber&& other) noexcept -> ShmSubscriber& {
    channel_ = std::exchange(other.channel_, nullptr);
    next_sequence_ = std::exchange(other.next_sequence_, 0);
    return *this;
  }
  ~ShmSubscriber() = default;

  /// @brief Attaches a subscriber that starts at the next published sample.
  /// @par Requirements
  /// SHMPS-REQ-010, SHMPS-REQ-017
  [[nodiscard]] static auto attach(ShmChannel& channel) noexcept
      -> std::expected<ShmSubscriber, ShmPubSubError>;

  /// @brief Copies the next unread sample into `buffer` without blocking.
  /// @return Sample metadata; `kWouldBlock`, `kBufferTooSmall` (sample not
  ///         consumed), `kClosed` (closed and drained).
  /// @par Requirements
  /// SHMPS-REQ-010, SHMPS-REQ-011, SHMPS-REQ-012, SHMPS-REQ-015
  [[nodiscard]] auto try_receive(std::span<std::byte> buffer) noexcept
      -> std::expected<ShmSample, ShmPubSubError>;

  /// @brief Like `try_receive`, waiting up to `timeout` for a sample.
  /// @return `kTimedOut` after `timeout`; otherwise as `try_receive`.
  /// @par Requirements
  /// SHMPS-REQ-013, SHMPS-REQ-014
  [[nodiscard]] auto receive_for(std::span<std::byte> buffer,
                                 std::chrono::nanoseconds timeout) noexcept
      -> std::expected<ShmSample, ShmPubSubError>;

  /// @brief Waits until a sample is available or the channel is closed.
  /// @par Requirements
  /// SHMPS-REQ-013, SHMPS-REQ-014
  [[nodiscard]] auto receive(std::span<std::byte> buffer) noexcept
      -> std::expected<ShmSample, ShmPubSubError>;

 private:
  ShmSubscriber(ShmChannel& channel, std::uint64_t next_sequence) noexcept
      : channel_{&channel}, next_sequence_{next_sequence} {}

  // TODO(SHMPS): add the state your design needs.
  ShmChannel* channel_{nullptr};
  std::uint64_t next_sequence_{0};
};

}  // namespace systems
