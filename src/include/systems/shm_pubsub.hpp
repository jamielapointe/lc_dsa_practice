#pragma once

/// @file shm_pubsub.hpp
/// @brief Single-publisher/multi-subscriber (SPMS) channel over POSIX
/// shared-memory ring buffer, safe across threads and processes.
///
/// Problem
/// -------
/// A publisher writes fixed-size blocks (slots 0..n-1 of a ring) into named
/// shared memory; any number of subscribers in any process read them at their
/// own pace. Requirements: `docs/systems/shm_pubsub.md` (prefix `SHMPS`).
///
/// Design summary
/// --------------
///   - Overwrite oldest: `publish()` never blocks. The ring always holds the
///     newest `slot_count` samples.
///   - Exactly one publisher at a time; any number of subscribers, each with a
///     private cursor (the publisher never knows about subscribers).
///   - Sizes are fixed at `create()`; `open()` reads them from the segment.
///   - A publisher killed at any instruction never exposes a torn sample, and
///     a replacement publisher can attach once the old process is dead.
///
/// How it works: per-slot seqlocks (read this first)
/// -------------------------------------------------
/// Every slot starts with a 64-bit atomic *sequence lock* word. Its parity is
/// the protocol:
///
///   - **even** `2s`: the slot is stable and holds sample number `s`;
///   - **odd** `2s - 1`: the publisher is writing sample `s` into it right now.
///
/// The slot of sample `s` is `(s - 1) % slot_count`. The publisher keeps `s`
/// in its own handle, so it computes the odd/even values arithmetically and
/// never loads shared state to find out where to write. That is what makes it
/// **wait-free**: `publish()` runs a fixed number of steps (plus the payload
/// copy), with no loops on shared state, no read-modify-write atomics, no
/// locks, and no system calls, whatever the subscribers are doing.
///
/// A subscriber that wants sample `s` checks the lock word for exactly `2s`
/// before *and after* copying the payload. If the second check differs, the
/// publisher lapped it mid-copy (a torn read). The subscriber then drops the
/// copy, re-reads the publisher's head, and jumps to the newest sample. A
/// subscriber only ever retries because the publisher made progress, so the
/// read side is **lock-free**.
///
/// Memory ordering (why this is correct on AArch64, not only on x86)
/// ----------------------------------------------------------------
/// x86-64 never reorders stores with stores or loads with loads, which hides
/// most ordering bugs. ARMv8 reorders all of them, so the protocol states
/// every required ordering explicitly (see `shm_pubsub.cpp`):
///
/// | Step (W = writer, R = reader)                | Prevents on ARM          |
/// | -------------------------------------------- | ------------------------ |
/// | W: `seq = 2s-1`, then fence(release)         | payload before odd mark  |
/// | W: payload stores, then `seq = 2s` (release) | even mark before payload |
/// | R: `v1 = seq` (acquire), then payload loads  | payload before 1st check |
/// | R: payload loads, fence(acquire), `v2 = seq` | 2nd check before payload |
///
/// Payload bytes are read and written with relaxed `std::atomic_ref` word
/// operations rather than `memcpy`. A `memcpy` racing with the writer would be
/// a data race (undefined behavior in C++, and ThreadSanitizer would flag it).
/// Relaxed atomics compile to the same plain `ldr`/`str` / `mov`
/// instructions, so the safety costs nothing.
///
/// Read modes (`ShmReadMode`)
/// --------------------------
///   - `kInOrder` (default; IMU integration, state estimation, logging):
///     every sample in sequence order while the subscriber keeps up. If it
///     is lapped (falls `slot_count` or more behind) or a read tears, it jumps
///     to the **newest** sample and `ShmSample::missed` says how many it
///     skipped.
///   - `kLatest` (ML inference, video, lidar): every receive returns the
///     newest unread sample and skips anything older.
///
/// Jumping to the newest sample, not the oldest, is also the robust choice:
/// the oldest slot is the very next one the publisher overwrites, while the
/// newest is about `slot_count - 1` publish periods away from being reused.
///
/// Memory residency (Linux real-time requirement)
/// ----------------------------------------------
/// Linux allocates physical pages lazily, on first touch, and every process
/// has its own page tables. A page fault inside `publish()` would stall a
/// real-time thread for microseconds. `create()` and `open()` are therefore
/// part of the *initialization* phase. Each one faults every page of the
/// mapping into its process and wires the pages with `mlock()`:
///   - `create()` writes a dummy byte to every page of the brand-new, empty
///     segment (allocating it), then calls `mlock`;
///   - `open()` maps with `MAP_POPULATE`, calls `mlock`, then read-touches
///     every page. It never writes, because the segment may already hold live
///     samples (a late subscriber, or a publisher restarted after a crash).
///
/// If the pages cannot be locked, initialization fails with
/// `kMemoryLockFailed` (see `SharedMemoryRegion`). Process-wide policy such
/// as `mlockall(MCL_CURRENT | MCL_FUTURE)` belongs in the application's boot
/// code, not in this library.
///
/// Thread / process safety
/// -----------------------
///   - The shared channel supports one publisher and many subscribers
///     concurrently, from any mix of threads and processes.
///   - Each `ShmPublisher` / `ShmSubscriber` handle is used by one thread at a
///     time. A `ShmChannel` handle may be shared by threads in one process
///     (all its member functions are safe to call concurrently).
///   - Publisher and subscriber handles point into their channel's mapping;
///     the `ShmChannel` must outlive them.
///
/// Blocking
/// --------
///   - `publish()`, `try_receive()`, `close()`, `is_closed()` never block.
///   - `receive_for()` blocks up to its timeout; `receive()` blocks until a
///     sample arrives or the channel is closed. Both poll with bounded
///     back-off (spin, yield, then sleep capped at 1 ms), so they wake within
///     about 1 ms of a publish or close in any process. The publisher never
///     has to wake anyone, which keeps it free of system calls.
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
/// `systems::platform::kMaxShmNameLength` characters. That file also explains
/// how a QNX port would allocate physically contiguous, non-pageable memory
/// instead of pre-faulting.
///
/// Known limits
/// ------------
///   - Sequences are 64-bit; wrap-around would take 2^63 publishes.
///   - Publisher takeover trusts process ids (`getpgid(pid)` plus a zombie
///     check). It assumes every process that shares a channel runs in the
///     same PID namespace (A-13). Within one namespace, a dead holder's pid
///     that the kernel recycled for an unrelated live process only delays a
///     takeover until that process exits. A holder in another PID namespace
///     breaks the assumption, because its pid means nothing in ours. If no
///     process here has that number, the probe returns `ESRCH` and a live
///     publisher is taken over wrongfully: two live publishers then write
///     the same ring and corrupt it. If an unrelated process here has it (a
///     container's pid 1 is the host's init), a dead holder is never
///     detected and takeover stays blocked while that process lives.

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
  kNotImplemented,    ///< Stub: the operation has not been implemented yet.
  kInvalidArgument,   ///< Bad name / configuration, or a moved-from handle.
  kNameTooLong,       ///< Name longer than `platform::kMaxShmNameLength`.
  kAlreadyExists,     ///< `create`: an object with this name already exists.
  kNotFound,          ///< `open` / `unlink`: no object with this name.
  kLayoutMismatch,    ///< `open`: object is not a compatible channel.
  kPublisherExists,   ///< `ShmPublisher::attach`: a live publisher exists.
  kPayloadTooLarge,   ///< `publish`: payload larger than `slot_size`.
  kBufferTooSmall,    ///< Receive: buffer smaller than the next sample.
  kWouldBlock,        ///< `try_receive`: no unread sample.
  kTimedOut,          ///< `receive_for`: timeout elapsed.
  kClosed,            ///< Channel closed (and, for receives, drained).
  kMemoryLockFailed,  ///< `create` / `open`: pages could not be wired to RAM.
  kOsError,           ///< Unexpected operating-system failure.
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
    case ShmPubSubError::kMemoryLockFailed:
      return "memory lock failed";
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

/// @brief How a subscriber treats samples it has not read yet
/// (SHMPS-REQ-027).
enum class ShmReadMode : std::uint8_t {
  /// Every sample in order while keeping up; jump to the newest when lapped
  /// or torn. For consumers that need continuity (IMU, EKF, logging).
  kInOrder,
  /// Always the newest unread sample; older unread samples are skipped. For
  /// consumers that only care about freshness (ML, video, lidar).
  kLatest,
};

namespace detail {

/// @brief One process's view of a mapped channel: where the mapping starts
/// and the ring geometry.
///
/// Trivially copyable. Each handle caches a copy so the hot paths find a slot
/// with pure arithmetic: no pointer chasing through the `ShmChannel` and no
/// loads of shared memory. A default-constructed view (`base == nullptr`)
/// marks a moved-from handle.
struct ChannelView {
  std::byte* base{nullptr};      ///< Start of the mapping (the shared header).
  std::uint32_t slot_mask{0};    ///< `slot_count - 1` (power-of-two ring).
  std::uint32_t slot_size{0};    ///< Largest payload a slot holds, in bytes.
  std::uint32_t slot_stride{0};  ///< Bytes from one slot to the next.
};

/// @brief OS adapter: a mapped POSIX shared-memory object (move-only).
///
/// The only type that calls the operating system; implemented in
/// `shm_pubsub_posix.cpp` (SHMPS-REQ-021). It does not validate names; the
/// caller does.
///
/// Every successful `create` / `open` returns a mapping that is wired into
/// RAM (`mlock`) with every page already faulted into this process's page
/// tables (SHMPS-REQ-023). If the pages cannot be wired, the call fails with
/// `kMemoryLockFailed` and leaves nothing behind (SHMPS-REQ-024).
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

  /// @brief Creates a new zero-filled object of `bytes` bytes, maps it,
  /// pre-faults every page with a dummy write, and locks it into RAM.
  /// @return `kAlreadyExists`, `kMemoryLockFailed` (the object is unlinked
  ///         again), `kOsError`.
  [[nodiscard]] static auto create(std::string_view name,
                                   std::size_t bytes) noexcept
      -> std::expected<SharedMemoryRegion, ShmPubSubError>;

  /// @brief Maps an existing object at its current size, populates and
  /// locks the pages, then read-touches each page. Never writes. A
  /// zero-length object yields an empty (`!valid()`) region.
  /// @return `kNotFound`, `kMemoryLockFailed`, `kOsError`.
  [[nodiscard]] static auto open(std::string_view name) noexcept
      -> std::expected<SharedMemoryRegion, ShmPubSubError>;

  /// @brief Removes the name; existing mappings stay valid.
  /// @return `kNotFound`, `kOsError`.
  [[nodiscard]] static auto unlink(std::string_view name) noexcept
      -> std::expected<void, ShmPubSubError>;

  /// @brief True if process `pid` exists and is not a zombie (used for
  /// publisher takeover).
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
/// (SHMPS-REQ-019). Must outlive any `ShmPublisher` / `ShmSubscriber`
/// attached to it.
class ShmChannel {
 public:
  ShmChannel(const ShmChannel&) = delete;
  auto operator=(const ShmChannel&) -> ShmChannel& = delete;
  ShmChannel(ShmChannel&&) noexcept = default;
  auto operator=(ShmChannel&&) noexcept -> ShmChannel& = default;
  ~ShmChannel() = default;

  /// @brief Creates, initializes, pre-faults, and locks a new channel.
  /// @param name `/name`, at most `platform::kMaxShmNameLength` characters.
  /// @param config Geometry (validated).
  /// @return `kInvalidArgument`, `kNameTooLong`, `kAlreadyExists`,
  ///         `kMemoryLockFailed`, `kOsError`.
  /// @par Blocking
  /// Initialization-phase call: touches every page and may sleep briefly
  /// while retrying `mlock`.
  /// @par Requirements
  /// SHMPS-REQ-001, SHMPS-REQ-002, SHMPS-REQ-003, SHMPS-REQ-023,
  /// SHMPS-REQ-024
  [[nodiscard]] static auto create(std::string_view name,
                                   const ShmPubSubConfig& config) noexcept
      -> std::expected<ShmChannel, ShmPubSubError>;

  /// @brief Opens an existing channel created by any process, faulting and
  /// locking its pages into this process.
  /// @return `kInvalidArgument`, `kNameTooLong`, `kNotFound`,
  ///         `kLayoutMismatch`, `kMemoryLockFailed`, `kOsError`.
  /// @par Requirements
  /// SHMPS-REQ-004, SHMPS-REQ-005, SHMPS-REQ-023, SHMPS-REQ-024
  [[nodiscard]] static auto open(std::string_view name) noexcept
      -> std::expected<ShmChannel, ShmPubSubError>;

  /// @brief Removes the channel name (existing handles keep working).
  /// @return `kInvalidArgument`, `kNameTooLong`, `kNotFound`, `kOsError`.
  /// @par Requirements
  /// SHMPS-REQ-006
  [[nodiscard]] static auto unlink(std::string_view name) noexcept
      -> std::expected<void, ShmPubSubError>;

  /// @brief Geometry the channel was created with (`{}` if moved-from).
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

  /// @brief Base pointer and geometry for handles to cache at `attach()`.
  [[nodiscard]] auto view() const noexcept -> detail::ChannelView;

  detail::SharedMemoryRegion region_;
};

/// @brief The single writer of a channel. Wait-free.
///
/// Move-only; releases the publisher role when destroyed (SHMPS-REQ-007).
class ShmPublisher {
 public:
  ShmPublisher(const ShmPublisher&) = delete;
  auto operator=(const ShmPublisher&) -> ShmPublisher& = delete;
  ShmPublisher(ShmPublisher&& other) noexcept
      : view_{std::exchange(other.view_, {})},
        next_sequence_{std::exchange(other.next_sequence_, 0)},
        owner_pid_{std::exchange(other.owner_pid_, 0)} {}
  auto operator=(ShmPublisher&& other) noexcept -> ShmPublisher&;
  ~ShmPublisher();

  /// @brief Claims the publisher role on `channel`.
  ///
  /// When the role is held by a process that has died, takes it over and
  /// continues numbering after the last sample that process committed.
  /// @return `kPublisherExists` while another live publisher holds it,
  ///         `kClosed` if the channel is closed, `kInvalidArgument` for a
  ///         moved-from channel.
  /// @par Requirements
  /// SHMPS-REQ-007, SHMPS-REQ-020
  [[nodiscard]] static auto attach(ShmChannel& channel) noexcept
      -> std::expected<ShmPublisher, ShmPubSubError>;

  /// @brief Copies `payload` into the next slot. Wait-free.
  /// @return The sample's sequence number; `kPayloadTooLarge`, `kClosed`,
  ///         `kInvalidArgument` (moved-from handle).
  /// @par Blocking
  /// Never: a bounded number of steps plus the payload copy, independent of
  /// subscribers. No locks, no read-modify-write atomics, no system calls.
  /// @par Requirements
  /// SHMPS-REQ-008, SHMPS-REQ-009, SHMPS-REQ-018, SHMPS-REQ-025,
  /// SHMPS-REQ-026
  [[nodiscard]] auto publish(std::span<const std::byte> payload) noexcept
      -> std::expected<std::uint64_t, ShmPubSubError>;

 private:
  ShmPublisher(detail::ChannelView view, std::uint64_t next_sequence,
               std::int64_t owner_pid) noexcept
      : view_{view}, next_sequence_{next_sequence}, owner_pid_{owner_pid} {}

  /// @brief Gives the publisher role back (destructor / move-assignment).
  void release() noexcept;

  detail::ChannelView view_{};      ///< Mapping and geometry (cached).
  std::uint64_t next_sequence_{0};  ///< Sequence the next publish assigns.
  std::int64_t owner_pid_{0};       ///< Process that claimed the role.
};

/// @brief One reader of a channel with its own cursor. Lock-free.
///
/// Move-only. Holds no shared state, so a crashed subscriber needs no
/// recovery.
class ShmSubscriber {
 public:
  ShmSubscriber(const ShmSubscriber&) = delete;
  auto operator=(const ShmSubscriber&) -> ShmSubscriber& = delete;
  ShmSubscriber(ShmSubscriber&& other) noexcept
      : view_{std::exchange(other.view_, {})},
        next_sequence_{std::exchange(other.next_sequence_, 0)},
        mode_{other.mode_} {}
  auto operator=(ShmSubscriber&& other) noexcept -> ShmSubscriber& {
    view_ = std::exchange(other.view_, {});
    next_sequence_ = std::exchange(other.next_sequence_, 0);
    mode_ = other.mode_;
    return *this;
  }
  ~ShmSubscriber() = default;

  /// @brief Attaches a subscriber that starts at the next published sample.
  /// @param mode `kInOrder` (default) or `kLatest`; see `ShmReadMode`.
  /// @return `kInvalidArgument` for a moved-from channel.
  /// @par Requirements
  /// SHMPS-REQ-010, SHMPS-REQ-017, SHMPS-REQ-027
  [[nodiscard]] static auto attach(
      ShmChannel& channel, ShmReadMode mode = ShmReadMode::kInOrder) noexcept
      -> std::expected<ShmSubscriber, ShmPubSubError>;

  /// @brief Copies the next unread sample into `buffer` without blocking.
  /// @return Sample metadata; `kWouldBlock`, `kBufferTooSmall` (sample not
  ///         consumed), `kClosed` (closed and drained).
  /// @par Requirements
  /// SHMPS-REQ-010, SHMPS-REQ-011, SHMPS-REQ-012, SHMPS-REQ-015,
  /// SHMPS-REQ-026, SHMPS-REQ-027
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
  ShmSubscriber(detail::ChannelView view, std::uint64_t next_sequence,
                ShmReadMode mode) noexcept
      : view_{view}, next_sequence_{next_sequence}, mode_{mode} {}

  /// @brief Shared body of `receive_for` / `receive`: poll with back-off
  /// until a sample, close, or `deadline`.
  [[nodiscard]] auto receive_until(
      std::span<std::byte> buffer,
      std::chrono::steady_clock::time_point deadline) noexcept
      -> std::expected<ShmSample, ShmPubSubError>;

  detail::ChannelView view_{};      ///< Mapping and geometry (cached).
  std::uint64_t next_sequence_{0};  ///< Next sequence this reader wants.
  ShmReadMode mode_{ShmReadMode::kInOrder};
};

}  // namespace systems
