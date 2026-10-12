/// @file shm_pubsub.cpp
/// @brief Portable channel logic for `systems::ShmChannel`,
/// `systems::ShmPublisher`, and `systems::ShmSubscriber`.
///
/// This file must stay free of POSIX headers (SHMPS-REQ-021): every OS call
/// goes through `detail::SharedMemoryRegion` (`shm_pubsub_posix.cpp`).
///
/// Reading guide
/// -------------
///   1. The shared layout (`ChannelHeader`, `SlotHeader`) and its
///      `static_assert`s: what lives in shared memory and why it is safe there.
///   2. `ShmPublisher::publish`: the seqlock writer, step by step.
///   3. `read_slot` and `ShmSubscriber::try_receive`: the seqlock reader and
///      the skip / retry policy.
///   4. `ShmPublisher::attach`: claiming the role and recovering after a crash.
///   5. `Backoff` and `receive_until`: blocking without making the publisher
///      wake anyone.
/// Requirements: `docs/systems/shm_pubsub.md`.

#include "systems/shm_pubsub.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <new>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "systems/platform.hpp"

namespace systems {
namespace {

using Clock = std::chrono::steady_clock;

// =====================================================================
// 1. Shared-memory layout (SHMPS-REQ-022)
// =====================================================================
//
//   offset 0    ChannelHeader (3 cache lines, see below)
//   then        slot_count slots, each `slot_stride` bytes:
//                 SlotHeader (16 bytes: seq, size, reserved)
//                 payload    (slot_size rounded up to 8 bytes, as u64 words)
//                 padding    (up to the next cache-line boundary)
//
// Everything here is either plain trivially-copyable data that is written
// once, before the magic number is published, or a `std::atomic` that is
// always lock-free. A lock-based atomic would hide a mutex inside the object,
// and that mutex would be private to one process. Nothing contains a pointer:
// each process maps the segment at a different address.

/// "SHMPSUB1" in ASCII. Identifies a channel segment.
constexpr std::uint64_t kMagic = 0x5348'4D50'5355'4231ULL;
/// Bump whenever the layout below changes, so `open()` rejects old segments.
constexpr std::uint32_t kLayoutVersion = 1;
/// False-sharing granularity (64 bytes on x86-64 / Cortex-A, see platform).
constexpr std::size_t kCacheLine = platform::kCacheLineSize;
/// Payload is stored and copied as 64-bit words (see `store_payload`).
constexpr std::size_t kWordBytes = sizeof(std::uint64_t);

/// @brief Channel-wide state at offset 0 of the mapping.
///
/// The three groups sit on separate cache lines because different parties
/// write them at very different rates. Mixing them would make every reader's
/// `published` poll bounce the line that holds the geometry, and so on.
struct alignas(kCacheLine) ChannelHeader {
  // --- Line 0: identity and geometry. Written once by `create()`, before
  // `magic` is release-stored; read-only afterwards. Plain fields are fine
  // because every reader first acquire-loads `magic`.
  std::atomic<std::uint64_t> magic{0};
  std::uint32_t layout_version{0};
  std::uint32_t slot_count{0};
  std::uint32_t slot_size{0};
  std::uint32_t slot_stride{0};

  // --- Line 1: written by the publisher.
  /// Sequence of the newest *committed* sample (0 = nothing yet). Subscribers
  /// poll it to learn whether anything new exists and how far behind they are.
  alignas(kCacheLine) std::atomic<std::uint64_t> published{0};
  /// Process id holding the publisher role, 0 when free (SHMPS-REQ-007).
  std::atomic<std::int64_t> publisher_pid{0};

  // --- Line 2: written at most once per channel lifetime.
  alignas(kCacheLine) std::atomic<std::uint32_t> closed{0};
};

/// @brief Metadata at the start of every slot.
struct SlotHeader {
  /// The per-slot sequence lock: `2s` = stable sample `s`, `2s - 1` =
  /// sample `s` is being written, 0 = never written.
  std::atomic<std::uint64_t> seq{0};
  /// Payload length of the sample in this slot. Written inside the seqlock
  /// window, so it is validated exactly like the payload.
  std::atomic<std::uint32_t> size{0};
  std::uint32_t reserved{0};  ///< Keeps the payload 8-byte aligned.
};

// Cross-process atomics must be lock-free. An address-based lock table
// (libatomic's fallback) is per process and would protect nothing.
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::int64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::byte>::is_always_lock_free);
// Payload words sit at 8-byte offsets inside cache-line-aligned slots.
static_assert(std::atomic_ref<std::uint64_t>::required_alignment <= kWordBytes);
// Nothing in shared memory may need a destructor or hold hidden pointers.
static_assert(std::is_trivially_destructible_v<ChannelHeader>);
static_assert(std::is_trivially_destructible_v<SlotHeader>);
static_assert(std::is_standard_layout_v<ChannelHeader>);
static_assert(std::is_standard_layout_v<SlotHeader>);
static_assert(sizeof(SlotHeader) == 2 * kWordBytes);
static_assert(sizeof(ChannelHeader) % kCacheLine == 0);

constexpr std::size_t kHeaderBytes = sizeof(ChannelHeader);
constexpr std::size_t kSlotHeaderBytes = sizeof(SlotHeader);

[[nodiscard]] constexpr auto round_up(std::size_t value,
                                      std::size_t multiple) noexcept
    -> std::size_t {
  return (value + multiple - 1) / multiple * multiple;
}

/// @brief Bytes per slot: header and word-rounded payload, padded to a cache
/// line so two slots never share one. Otherwise the publisher writing slot
/// i+1 would keep invalidating the line a reader is copying from slot i.
[[nodiscard]] constexpr auto slot_stride_for(std::uint32_t slot_size) noexcept
    -> std::uint32_t {
  return static_cast<std::uint32_t>(
      round_up(kSlotHeaderBytes + round_up(slot_size, kWordBytes), kCacheLine));
}

/// @brief Total mapping size for a configuration.
[[nodiscard]] constexpr auto region_bytes(
    const ShmPubSubConfig& config) noexcept -> std::size_t {
  return kHeaderBytes + (static_cast<std::size_t>(config.slot_count) *
                         slot_stride_for(config.slot_size));
}

// Largest legal segment: 4096 slots x ~64 KiB, about 256 MiB. It fits
// comfortably in std::size_t, so `region_bytes` cannot overflow.
static_assert(region_bytes({.slot_count = kShmMaxSlotCount,
                            .slot_size = kShmMaxSlotSize}) <
              (std::size_t{1} << 30U));

// ---------------------------------------------------------------------
// Typed access to the mapping. These two helpers are the only places
// that turn raw bytes into layout types or do pointer arithmetic.
// ---------------------------------------------------------------------

/// @brief The `ChannelHeader` at the start of a mapping.
[[nodiscard]] auto header_of(std::byte* base) noexcept -> ChannelHeader& {
  // `create()` constructed a ChannelHeader at this address, possibly in
  // another process. `std::launder` tells the compiler to treat the bytes as
  // that live object.
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast): shared-memory layout view.
  return *std::launder(reinterpret_cast<ChannelHeader*>(base));
}

/// @brief `base + offset`. Keeps pointer arithmetic in one audited spot.
[[nodiscard]] auto byte_at(std::byte* base, std::size_t offset) noexcept
    -> std::byte* {
  // NOLINTNEXTLINE(*-pro-bounds-pointer-arithmetic): validated layout offset.
  return base + offset;
}

/// @brief A slot as seen through a `ChannelView`.
struct SlotRef {
  SlotHeader* header;              ///< Sequence lock and size.
  std::span<std::uint64_t> words;  ///< Payload storage (relaxed atomics).
};

/// @brief The slot that stores sample `sequence` (sequence >= 1).
///
/// Pure arithmetic on cached geometry: this is how the publisher finds its
/// slot without reading shared memory.
[[nodiscard]] auto slot_for(const detail::ChannelView& view,
                            std::uint64_t sequence) noexcept -> SlotRef {
  const auto index = static_cast<std::size_t>((sequence - 1U) & view.slot_mask);
  std::byte* const slot =
      byte_at(view.base, kHeaderBytes + (index * view.slot_stride));
  // NOLINTBEGIN(*-pro-type-reinterpret-cast): shared-memory layout view.
  auto* const header = std::launder(reinterpret_cast<SlotHeader*>(slot));
  auto* const words =
      reinterpret_cast<std::uint64_t*>(byte_at(slot, kSlotHeaderBytes));
  // NOLINTEND(*-pro-type-reinterpret-cast)
  return {.header = header,
          .words = {words, round_up(view.slot_size, kWordBytes) / kWordBytes}};
}

// ---------------------------------------------------------------------
// Payload copies with relaxed atomics.
//
// Why not memcpy? A reader's copy can overlap the publisher's write of the
// same bytes; the seqlock detects that afterwards and discards the copy.
// With plain memcpy that overlap is still a data race, which C++ makes
// undefined behavior no matter what we do with the result. Relaxed
// atomic loads and stores make the race well defined (the reader may see a
// mix of old and new words, and the seqlock rejects it). They compile to
// ordinary loads and stores. `std::memcpy` is only used to move bytes between
// the caller's buffer and a local word (no shared memory involved).
// ---------------------------------------------------------------------

/// @brief Writes `payload` into `words`, zero-padding the last word.
void store_payload(std::span<std::uint64_t> words,
                   std::span<const std::byte> payload) noexcept {
  const std::size_t full_words = payload.size() / kWordBytes;
  for (std::size_t i = 0; i < full_words; ++i) {
    std::uint64_t word = 0;
    std::memcpy(&word, payload.subspan(i * kWordBytes, kWordBytes).data(),
                kWordBytes);
    std::atomic_ref{words[i]}.store(word, std::memory_order_relaxed);
  }
  const std::size_t tail = payload.size() % kWordBytes;
  if (tail != 0) {
    std::uint64_t word = 0;
    std::memcpy(&word, payload.subspan(full_words * kWordBytes).data(), tail);
    std::atomic_ref{words[full_words]}.store(word, std::memory_order_relaxed);
  }
}

/// @brief Reads `out.size()` payload bytes from `words` into `out`.
void load_payload(std::span<std::uint64_t> words,
                  std::span<std::byte> out) noexcept {
  const std::size_t full_words = out.size() / kWordBytes;
  for (std::size_t i = 0; i < full_words; ++i) {
    const std::uint64_t word =
        std::atomic_ref{words[i]}.load(std::memory_order_relaxed);
    std::memcpy(out.subspan(i * kWordBytes, kWordBytes).data(), &word,
                kWordBytes);
  }
  const std::size_t tail = out.size() % kWordBytes;
  if (tail != 0) {
    const std::uint64_t word =
        std::atomic_ref{words[full_words]}.load(std::memory_order_relaxed);
    std::memcpy(out.subspan(full_words * kWordBytes).data(), &word, tail);
  }
}

// =====================================================================
// 3. Seqlock read of one slot
// =====================================================================

/// @brief Outcome of one attempt to read a specific sample.
enum class ReadOutcome : std::uint8_t {
  kOk,              ///< Copied an intact sample.
  kBufferTooSmall,  ///< Intact sample, but it does not fit; nothing copied.
  kOverwritten,     ///< The publisher lapped this slot (before or during).
  kNotReady,        ///< The slot does not hold this sample yet.
};

/// @brief Seqlock read side: copy sample `sequence` out of its slot.
///
/// @param view     Cached mapping geometry.
/// @param sequence Sample wanted; the slot must hold `2 * sequence`.
/// @param buffer   Destination; `size_out` receives the payload length.
///
/// Ordering (each step pairs with a step in `ShmPublisher::publish`):
///   (R1) `v1 = seq.load(acquire)`. Pairs with the writer's (W4) release
///        store of `2s`: if we read `2s`, the whole payload of sample `s`
///        happens-before everything that follows.
///   (R2) relaxed loads of `size` and the payload words.
///   (R3) `atomic_thread_fence(acquire)`. If any word we loaded in (R2) was
///        written by a *later* publication (after the writer's (W2) release
///        fence), this fence synchronizes with that fence. The writer's (W1)
///        odd store then happens-before (R4), so (R4) cannot still see `2s`.
///        This is the LoadLoad barrier that ARM needs (`dmb ishld`); x86 gets
///        it for free.
///   (R4) `v2 = seq.load(relaxed)`. Unchanged means nothing overlapped the
///   copy.
[[nodiscard]] auto read_slot(const detail::ChannelView& view,
                             std::uint64_t sequence,
                             std::span<std::byte> buffer,
                             std::uint32_t& size_out) noexcept -> ReadOutcome {
  const SlotRef slot = slot_for(view, sequence);
  const std::uint64_t wanted = 2U * sequence;

  const std::uint64_t v1 =
      slot.header->seq.load(std::memory_order_acquire);  // (R1)
  if (v1 != wanted) {
    // Larger (even or odd): a newer sample is in, or being written into,
    // the slot, so ours is gone. Smaller: ours has not been committed
    // (only possible after a publisher crashed mid-write).
    return v1 > wanted ? ReadOutcome::kOverwritten : ReadOutcome::kNotReady;
  }

  const std::uint32_t size =
      slot.header->size.load(std::memory_order_relaxed);  // (R2)
  // A size that does not fit the slot can only be a torn value (or a foreign
  // writer), so never trust it for the copy length. Report it the same way
  // as a tear. The caller re-reads the head and retries.
  const bool size_plausible = size <= view.slot_size;
  const bool fits = size_plausible && size <= buffer.size();
  if (fits) {
    load_payload(slot.words, buffer.first(size));  // (R2)
  }

  std::atomic_thread_fence(std::memory_order_acquire);  // (R3)
  const std::uint64_t v2 =
      slot.header->seq.load(std::memory_order_relaxed);  // (R4)
  if (v2 != v1 || !size_plausible) {
    return ReadOutcome::kOverwritten;  // torn: the copy (or size) is garbage
  }
  if (!fits) {
    return ReadOutcome::kBufferTooSmall;  // validated size, genuinely too big
  }
  size_out = size;
  return ReadOutcome::kOk;
}

// =====================================================================
// Validation helpers (init phase)
// =====================================================================

/// @brief SHMPS-REQ-002: `/name`, exactly one leading slash, no NULs,
/// at most `platform::kMaxShmNameLength` characters.
[[nodiscard]] auto validate_name(std::string_view name) noexcept
    -> std::expected<void, ShmPubSubError> {
  if (name.size() < 2 || name.front() != '/' ||
      name.find('/', 1) != std::string_view::npos || name.contains('\0')) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }
  if (name.size() > platform::kMaxShmNameLength) {
    return std::unexpected(ShmPubSubError::kNameTooLong);
  }
  return {};
}

/// @brief SHMPS-REQ-001: power-of-two slot count and sizes in range.
[[nodiscard]] constexpr auto config_is_valid(
    const ShmPubSubConfig& config) noexcept -> bool {
  return std::has_single_bit(config.slot_count) &&
         config.slot_count >= kShmMinSlotCount &&
         config.slot_count <= kShmMaxSlotCount && config.slot_size >= 1 &&
         config.slot_size <= kShmMaxSlotSize;
}

/// @brief Builds the layout in a fresh, zero-filled mapping.
///
/// Uses `std::construct_at` so the atomics start their lifetime properly.
/// Payload words are `std::uint64_t` (an implicit-lifetime type) and are
/// already zero. The magic number is release-stored **last**: a concurrent
/// `open()` either sees no magic and rejects the segment, or sees the magic
/// together with every field written before it.
void initialize_layout(std::byte* base,
                       const ShmPubSubConfig& config) noexcept {
  ChannelHeader* const header =
      std::construct_at(&header_of(base));  // value-initializes every field
  header->layout_version = kLayoutVersion;
  header->slot_count = config.slot_count;
  header->slot_size = config.slot_size;
  header->slot_stride = slot_stride_for(config.slot_size);

  const detail::ChannelView view{.base = base,
                                 .slot_mask = config.slot_count - 1U,
                                 .slot_size = config.slot_size,
                                 .slot_stride = header->slot_stride};
  for (std::uint64_t sequence = 1; sequence <= config.slot_count; ++sequence) {
    std::construct_at(slot_for(view, sequence).header);
  }
  header->magic.store(kMagic, std::memory_order_release);
}

/// @brief SHMPS-REQ-005: is this mapping a channel we understand?
[[nodiscard]] auto layout_is_valid(
    const detail::SharedMemoryRegion& region) noexcept -> bool {
  if (!region.valid() || region.size() < kHeaderBytes) {
    return false;
  }
  const ChannelHeader& header = header_of(region.data());
  // Acquire pairs with the release store in `initialize_layout`, making the
  // plain geometry fields below safe to read.
  if (header.magic.load(std::memory_order_acquire) != kMagic ||
      header.layout_version != kLayoutVersion) {
    return false;
  }
  const ShmPubSubConfig config{.slot_count = header.slot_count,
                               .slot_size = header.slot_size};
  return config_is_valid(config) &&
         header.slot_stride == slot_stride_for(config.slot_size) &&
         region.size() >= region_bytes(config);
}

// =====================================================================
// 4. Crash recovery helper
// =====================================================================

/// @brief First sequence a newly attached publisher may assign.
///
/// Normally `published + 1`. If the previous publisher died mid-publish:
///   - it may have left a slot odd (`2k - 1`). Sample k was never committed
///     and no reader can have seen it, so we reuse k: the new publisher
///     rewrites the same slot, and the odd value it stores is the same one;
///   - it may have committed a slot (`2k`) but died before advancing
///     `published` to k. We continue at k + 1 so the committed sample is not
///     overwritten under the same number.
/// Taking `floor(seq / 2)` over all slots handles both cases at once. Every
/// sequence we hand out is therefore greater than every sequence that was
/// ever committed (SHMPS-REQ-020).
[[nodiscard]] auto recover_next_sequence(
    const detail::ChannelView& view) noexcept -> std::uint64_t {
  std::uint64_t newest =
      header_of(view.base).published.load(std::memory_order_acquire);
  const std::uint64_t slot_count = std::uint64_t{view.slot_mask} + 1U;
  for (std::uint64_t sequence = 1; sequence <= slot_count; ++sequence) {
    const std::uint64_t lock =
        slot_for(view, sequence).header->seq.load(std::memory_order_acquire);
    newest = std::max(newest, lock / 2U);
  }
  return newest + 1U;
}

// =====================================================================
// 5. Bounded back-off for blocking receives
// =====================================================================

/// @brief CPU hint that we are busy-waiting: `pause` on x86 (saves power,
/// avoids a memory-order pipeline flush on exit), `yield` on ARM.
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  asm volatile("yield" ::: "memory");
#else
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

/// @brief Escalating wait: spin, then yield once, then sleep with doubling
/// durations capped at `kMaxSleep`, never past the caller's deadline.
///
/// Why polling? A futex or condition variable would make the publisher do a
/// wake-up system call on every publish (or whenever waiters exist). That
/// breaks its wait-free, syscall-free guarantee. Polling moves all waiting
/// cost to the soft-real-time subscribers. The 1 ms cap bounds wake latency
/// far below the 100 ms requirement (SHMPS-REQ-013/014).
class Backoff {
 public:
  void wait(Clock::time_point deadline) noexcept {
    if (spins_ < kSpinLimit) {
      ++spins_;
      cpu_relax();
      return;
    }
    if (!yielded_) {
      yielded_ = true;
      std::this_thread::yield();
      return;
    }
    const auto now = Clock::now();
    if (now >= deadline) {
      return;
    }
    // Clamp to the deadline so `receive_for` does not overshoot its timeout
    // by a whole back-off step.
    std::this_thread::sleep_for(
        std::min<Clock::duration>(sleep_, deadline - now));
    sleep_ = std::min(sleep_ * 2, kMaxSleep);
  }

 private:
  static constexpr int kSpinLimit = 64;
  static constexpr Clock::duration kFirstSleep = std::chrono::microseconds{50};
  static constexpr Clock::duration kMaxSleep = std::chrono::milliseconds{1};

  int spins_{0};
  bool yielded_{false};
  Clock::duration sleep_{kFirstSleep};
};

/// @brief `now + timeout`, saturating instead of overflowing for huge
/// timeouts (for example `nanoseconds::max()`).
[[nodiscard]] auto deadline_after(std::chrono::nanoseconds timeout) noexcept
    -> Clock::time_point {
  const auto now = Clock::now();
  if (timeout <= std::chrono::nanoseconds::zero()) {
    return now;
  }
  const auto room = Clock::time_point::max() - now;
  if (timeout >= std::chrono::duration_cast<std::chrono::nanoseconds>(room)) {
    return Clock::time_point::max();
  }
  return now + std::chrono::duration_cast<Clock::duration>(timeout);
}

}  // namespace

// =====================================================================
// ShmChannel
// =====================================================================

auto ShmChannel::create(std::string_view name,
                        const ShmPubSubConfig& config) noexcept
    -> std::expected<ShmChannel, ShmPubSubError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  if (!config_is_valid(config)) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }
  // O_EXCL creation, dummy-write pre-fault of every page, and mlock all
  // happen in the adapter. On success the memory is resident, zero-filled,
  // and still unpublished (no magic yet).
  auto region = detail::SharedMemoryRegion::create(name, region_bytes(config));
  if (!region) {
    return std::unexpected(region.error());
  }
  initialize_layout(region->data(), config);
  return ShmChannel{std::move(*region)};
}

auto ShmChannel::open(std::string_view name) noexcept
    -> std::expected<ShmChannel, ShmPubSubError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  // MAP_POPULATE + mlock + read-touch in the adapter: this process's page
  // tables are filled without writing to the (possibly live) segment.
  auto region = detail::SharedMemoryRegion::open(name);
  if (!region) {
    return std::unexpected(region.error());
  }
  if (!layout_is_valid(*region)) {
    return std::unexpected(ShmPubSubError::kLayoutMismatch);
  }
  return ShmChannel{std::move(*region)};
}

auto ShmChannel::unlink(std::string_view name) noexcept
    -> std::expected<void, ShmPubSubError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  return detail::SharedMemoryRegion::unlink(name);
}

auto ShmChannel::view() const noexcept -> detail::ChannelView {
  if (!region_.valid()) {
    return {};
  }
  const ChannelHeader& header = header_of(region_.data());
  return {.base = region_.data(),
          .slot_mask = header.slot_count - 1U,
          .slot_size = header.slot_size,
          .slot_stride = header.slot_stride};
}

auto ShmChannel::config() const noexcept -> ShmPubSubConfig {
  if (!region_.valid()) {
    return {};
  }
  const ChannelHeader& header = header_of(region_.data());
  return {.slot_count = header.slot_count, .slot_size = header.slot_size};
}

void ShmChannel::close() noexcept {
  if (!region_.valid()) {
    return;
  }
  // Release: everything this thread did before closing is visible to a
  // reader that observes `closed`. There is nothing to wake: every waiter
  // polls (see `Backoff`) and sees the flag within about 1 ms.
  header_of(region_.data()).closed.store(1, std::memory_order_release);
}

auto ShmChannel::is_closed() const noexcept -> bool {
  return region_.valid() &&
         header_of(region_.data()).closed.load(std::memory_order_acquire) != 0;
}

// =====================================================================
// ShmPublisher
// =====================================================================

auto ShmPublisher::operator=(ShmPublisher&& other) noexcept -> ShmPublisher& {
  if (this != &other) {
    release();  // give up our own role before adopting `other`'s
    view_ = std::exchange(other.view_, {});
    next_sequence_ = std::exchange(other.next_sequence_, 0);
    owner_pid_ = std::exchange(other.owner_pid_, 0);
  }
  return *this;
}

ShmPublisher::~ShmPublisher() { release(); }

void ShmPublisher::release() noexcept {
  if (view_.base == nullptr) {
    return;  // moved-from: owns no role
  }
  // Fork safety: a child forked from the publisher process gets a copy of
  // this handle. If the child destroyed its copy, it must not release the
  // parent's role. Only the process that claimed the role gives it back.
  if (owner_pid_ == detail::SharedMemoryRegion::current_process_id()) {
    std::int64_t expected = owner_pid_;
    // Release: our final slot writes happen-before the next publisher's
    // acquire in `attach()`. A failed CAS means someone already took over
    // (we were presumed dead); leave their claim alone.
    static_cast<void>(header_of(view_.base)
                          .publisher_pid.compare_exchange_strong(
                              expected, 0, std::memory_order_release,
                              std::memory_order_relaxed));
  }
  view_ = {};
}

auto ShmPublisher::attach(ShmChannel& channel) noexcept
    -> std::expected<ShmPublisher, ShmPubSubError> {
  const detail::ChannelView view = channel.view();
  if (view.base == nullptr) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }
  ChannelHeader& header = header_of(view.base);
  if (header.closed.load(std::memory_order_acquire) != 0) {
    return std::unexpected(ShmPubSubError::kClosed);
  }

  // Claim the role: CAS 0 -> our pid. acq_rel: acquire sees everything the
  // previous publisher wrote before releasing; release publishes our claim.
  const std::int64_t me = detail::SharedMemoryRegion::current_process_id();
  std::int64_t holder = 0;
  if (!header.publisher_pid.compare_exchange_strong(
          holder, me, std::memory_order_acq_rel, std::memory_order_acquire)) {
    // Held. Our own process counts as alive: one publisher per channel, even
    // across threads. A live foreign process blocks us too.
    if (holder == me || detail::SharedMemoryRegion::process_alive(holder)) {
      return std::unexpected(ShmPubSubError::kPublisherExists);
    }
    // The holder is dead (SIGKILL, crash). Take over, but only if nobody
    // else took over first: the CAS expects exactly the dead pid.
    if (!header.publisher_pid.compare_exchange_strong(
            holder, me, std::memory_order_acq_rel, std::memory_order_acquire)) {
      return std::unexpected(ShmPubSubError::kPublisherExists);
    }
  }
  // Holding the role: no other writer exists, so the scan is stable.
  return ShmPublisher{view, recover_next_sequence(view), me};
}

// =====================================================================
// 2. Seqlock writer (wait-free)
// =====================================================================
//
// Wait-free here means: a fixed number of steps plus the O(payload) copy.
// No loop waits on another party, there are no read-modify-write
// atomics (no CAS retry), no locks, and no system calls. Nothing a
// subscriber does (stalling, crashing, being SIGSTOPped mid-read) can slow
// this function down.

auto ShmPublisher::publish(std::span<const std::byte> payload) noexcept
    -> std::expected<std::uint64_t, ShmPubSubError> {
  if (view_.base == nullptr) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);  // moved-from
  }
  if (payload.size() > view_.slot_size) {
    return std::unexpected(ShmPubSubError::kPayloadTooLarge);
  }
  ChannelHeader& header = header_of(view_.base);
  // The only load of shared state on this path: one flag on a line that is
  // written once. Relaxed is enough: no data hangs off it, and coherence
  // still guarantees that a close made earlier in this thread is seen.
  if (header.closed.load(std::memory_order_relaxed) != 0) {
    return std::unexpected(ShmPubSubError::kClosed);
  }

  // Sequence and slot come from our own counter, so the slot's current
  // lock value is never loaded: we already know it.
  const std::uint64_t sequence = next_sequence_;
  const SlotRef slot = slot_for(view_, sequence);

  // (W1) Odd: "writing sample `sequence`". Relaxed on its own...
  slot.header->seq.store((2U * sequence) - 1U, std::memory_order_relaxed);
  // (W2) ...the release fence orders (W1) before every store below. A
  // reader that sees any byte of the new payload also sees the odd value on
  // its re-check (R4). ARM: `dmb ish`; x86: no instruction (TSO).
  std::atomic_thread_fence(std::memory_order_release);

  // (W3) Payload and size, relaxed (ordered by W2 before and W4 after).
  store_payload(slot.words, payload);
  slot.header->size.store(static_cast<std::uint32_t>(payload.size()),
                          std::memory_order_relaxed);

  // (W4) Even: "sample `sequence` is stable". Release keeps (W3) from
  // drifting past it; it pairs with the reader's acquire (R1). ARM: `stlr`.
  slot.header->seq.store(2U * sequence, std::memory_order_release);

  // (W5) Advertise the newest committed sample. Release pairs with the
  // acquire load of `published` in `try_receive`, so a reader that sees
  // `sequence` here finds the slot already committed at (R1).
  header.published.store(sequence, std::memory_order_release);

  next_sequence_ = sequence + 1U;
  return sequence;
}

// =====================================================================
// ShmSubscriber
// =====================================================================

auto ShmSubscriber::attach(ShmChannel& channel, ShmReadMode mode) noexcept
    -> std::expected<ShmSubscriber, ShmPubSubError> {
  const detail::ChannelView view = channel.view();
  if (view.base == nullptr) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }
  // No history replay (assumption A-6): start at the next sample to be
  // published.
  const std::uint64_t newest =
      header_of(view.base).published.load(std::memory_order_acquire);
  return ShmSubscriber{view, newest + 1U, mode};
}

auto ShmSubscriber::try_receive(std::span<std::byte> buffer) noexcept
    -> std::expected<ShmSample, ShmPubSubError> {
  if (view_.base == nullptr) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);  // moved-from
  }
  const ChannelHeader& header = header_of(view_.base);
  const std::uint64_t slot_count = std::uint64_t{view_.slot_mask} + 1U;

  // The cursor and missed count are only committed to the handle on success,
  // so `kBufferTooSmall` / `kWouldBlock` consume nothing.
  std::uint64_t cursor = next_sequence_;
  std::uint64_t missed = 0;
  bool force_newest = false;

  // Lock-free loop: an iteration only fails (kOverwritten) because the
  // publisher wrote something new, which is system-wide progress.
  for (;;) {
    // Load `closed` BEFORE the head. If we see closed, every sample
    // published before the close is already visible in the head, so we
    // drain them all before reporting kClosed (SHMPS-REQ-014).
    const bool closed = header.closed.load(std::memory_order_acquire) != 0;
    const std::uint64_t head = header.published.load(std::memory_order_acquire);
    if (head < cursor) {
      return std::unexpected(closed ? ShmPubSubError::kClosed
                                    : ShmPubSubError::kWouldBlock);
    }

    // Skip policy (SHMPS-REQ-011 / REQ-027). `lag` = unread samples older
    // than the newest one.
    const std::uint64_t lag = head - cursor;
    const bool lapped = lag >= slot_count;  // cursor's slot was reused
    if (lag > 0 && (lapped || force_newest || mode_ == ShmReadMode::kLatest)) {
      missed += lag;
      cursor = head;  // jump to the newest committed sample
    }

    std::uint32_t size = 0;
    switch (read_slot(view_, cursor, buffer, size)) {
      case ReadOutcome::kOk:
        next_sequence_ = cursor + 1U;
        return ShmSample{.sequence = cursor, .size = size, .missed = missed};
      case ReadOutcome::kBufferTooSmall:
        return std::unexpected(ShmPubSubError::kBufferTooSmall);
      case ReadOutcome::kNotReady:
        // Only after a publisher crashed mid-write: the slot is not
        // committed yet. Do not spin on a writer that may be dead.
        return std::unexpected(closed ? ShmPubSubError::kClosed
                                      : ShmPubSubError::kWouldBlock);
      case ReadOutcome::kOverwritten:
        // Active write or torn read: abort this copy, go back to the top,
        // reload the head, and grab the newest sample.
        force_newest = true;
        break;
    }
  }
}

auto ShmSubscriber::receive_until(std::span<std::byte> buffer,
                                  Clock::time_point deadline) noexcept
    -> std::expected<ShmSample, ShmPubSubError> {
  Backoff backoff;
  for (;;) {
    auto sample = try_receive(buffer);
    if (sample || sample.error() != ShmPubSubError::kWouldBlock) {
      return sample;  // data, kClosed (drained), or a real error
    }
    if (Clock::now() >= deadline) {
      return std::unexpected(ShmPubSubError::kTimedOut);
    }
    backoff.wait(deadline);
  }
}

auto ShmSubscriber::receive_for(std::span<std::byte> buffer,
                                std::chrono::nanoseconds timeout) noexcept
    -> std::expected<ShmSample, ShmPubSubError> {
  return receive_until(buffer, deadline_after(timeout));
}

auto ShmSubscriber::receive(std::span<std::byte> buffer) noexcept
    -> std::expected<ShmSample, ShmPubSubError> {
  // No deadline: returns on a sample or on close (the poll sees `closed`
  // within about 1 ms).
  return receive_until(buffer, Clock::time_point::max());
}

}  // namespace systems
