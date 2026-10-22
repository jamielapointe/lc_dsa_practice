/// @file process_rendezvous.cpp
/// @brief Portable cross-process startup handshake logic for
/// `systems::ProcessRendezvous`.
///
/// This file must remain free of POSIX headers (PRV-REQ-011): all OS calls go
/// through `detail::RendezvousOsHandle` (`process_rendezvous_posix.cpp`).

#include "systems/process_rendezvous.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <new>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "systems/platform.hpp"

namespace systems {
namespace {

using Clock = std::chrono::steady_clock;

/// "SHMRNDV1" in ASCII. Identifies a ProcessRendezvous shared segment.
constexpr std::uint64_t kMagic = 0x5348'4D52'4E44'5631ULL;
/// Layout version checked by `open()`.
constexpr std::uint32_t kLayoutVersion = 1;
/// False-sharing alignment granularity.
constexpr std::size_t kCacheLine = platform::kCacheLineSize;

/// @brief Shared-memory layout for the two-stage startup rendezvous
/// (PRV-REQ-012).
struct alignas(kCacheLine) RendezvousHeader {
  // Line 0: identity and version. `magic` is release-stored last on init.
  std::atomic<std::uint64_t> magic{0};
  std::uint32_t layout_version{0};
  std::uint32_t reserved{0};

  // Line 1: stage 1 (producer created channel).
  alignas(kCacheLine) std::atomic<std::uint32_t> producer_ready{0};

  // Line 2: stage 2 (consumer attached subscriber and is waiting).
  alignas(kCacheLine) std::atomic<std::uint32_t> consumer_ready{0};

  // Line 3: close flag.
  alignas(kCacheLine) std::atomic<std::uint32_t> closed{0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::is_trivially_destructible_v<RendezvousHeader>);
static_assert(std::is_standard_layout_v<RendezvousHeader>);
static_assert(sizeof(RendezvousHeader) % kCacheLine == 0);

constexpr std::size_t kHeaderBytes = sizeof(RendezvousHeader);

[[nodiscard]] auto header_of(std::byte* base) noexcept -> RendezvousHeader& {
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast): shared-memory layout view.
  return *std::launder(reinterpret_cast<RendezvousHeader*>(base));
}

/// @brief PRV-REQ-002: `/name`, single leading slash, no NULs, within length.
[[nodiscard]] auto validate_name(std::string_view name) noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (name.size() < 2 || name.front() != '/' ||
      name.find('/', 1) != std::string_view::npos || name.contains('\0')) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  if (name.size() > platform::kMaxShmNameLength) {
    return std::unexpected(ProcessRendezvousError::kNameTooLong);
  }
  return {};
}

void initialize_layout(std::byte* base) noexcept {
  RendezvousHeader* const header = std::construct_at(&header_of(base));
  header->layout_version = kLayoutVersion;
  header->magic.store(kMagic, std::memory_order_release);
}

[[nodiscard]] auto layout_is_valid(
    const detail::RendezvousOsHandle& handle) noexcept -> bool {
  if (!handle.valid() || handle.size() < kHeaderBytes) {
    return false;
  }
  const RendezvousHeader& header = header_of(handle.data());
  return header.magic.load(std::memory_order_acquire) == kMagic &&
         header.layout_version == kLayoutVersion;
}

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

[[nodiscard]] auto try_wait_flag(
    const detail::RendezvousOsHandle& handle,
    const std::atomic<std::uint32_t>& flag) noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  const RendezvousHeader& header = header_of(handle.data());
  // Load `closed` before `flag` so a stage signaled before `close()` still
  // reports ready (PRV-REQ-006).
  const bool closed = header.closed.load(std::memory_order_acquire) != 0;
  if (flag.load(std::memory_order_acquire) != 0) {
    return {};
  }
  return std::unexpected(closed ? ProcessRendezvousError::kClosed
                                : ProcessRendezvousError::kWouldBlock);
}

[[nodiscard]] auto wait_flag_until(const detail::RendezvousOsHandle& handle,
                                   const std::atomic<std::uint32_t>& flag,
                                   Clock::time_point deadline) noexcept
    -> std::expected<void, ProcessRendezvousError> {
  constexpr auto kMaxSlice = std::chrono::milliseconds{2};
  for (;;) {
    auto status = try_wait_flag(handle, flag);
    if (status || status.error() != ProcessRendezvousError::kWouldBlock) {
      return status;
    }
    const auto now = Clock::now();
    if (now >= deadline) {
      return std::unexpected(ProcessRendezvousError::kTimedOut);
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now);
    handle.wait_slice(std::min<std::chrono::nanoseconds>(remaining, kMaxSlice));
  }
}

}  // namespace

auto ProcessRendezvous::create(std::string_view name) noexcept
    -> std::expected<ProcessRendezvous, ProcessRendezvousError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  auto handle = detail::RendezvousOsHandle::create(name, kHeaderBytes);
  if (!handle) {
    return std::unexpected(handle.error());
  }
  initialize_layout(handle->data());
  return ProcessRendezvous{std::move(*handle)};
}

auto ProcessRendezvous::open(std::string_view name) noexcept
    -> std::expected<ProcessRendezvous, ProcessRendezvousError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  auto handle = detail::RendezvousOsHandle::open(name);
  if (!handle) {
    return std::unexpected(handle.error());
  }
  if (!layout_is_valid(*handle)) {
    return std::unexpected(ProcessRendezvousError::kLayoutMismatch);
  }
  return ProcessRendezvous{std::move(*handle)};
}

auto ProcessRendezvous::attach(std::string_view name,
                               std::chrono::nanoseconds timeout) noexcept
    -> std::expected<ProcessRendezvous, ProcessRendezvousError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  const auto deadline = deadline_after(timeout);
  for (;;) {
    auto created = create(name);
    if (created || created.error() != ProcessRendezvousError::kAlreadyExists) {
      return created;
    }
    auto opened = open(name);
    if (opened) {
      return opened;
    }
    if (opened.error() != ProcessRendezvousError::kLayoutMismatch &&
        opened.error() != ProcessRendezvousError::kNotFound) {
      return opened;
    }
    if (Clock::now() >= deadline) {
      return std::unexpected(ProcessRendezvousError::kTimedOut);
    }
    std::this_thread::sleep_for(std::chrono::microseconds{100});
  }
}

auto ProcessRendezvous::unlink(std::string_view name) noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (auto valid = validate_name(name); !valid) {
    return std::unexpected(valid.error());
  }
  return detail::RendezvousOsHandle::unlink(name);
}

auto ProcessRendezvous::signal_producer_ready() noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle_.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  RendezvousHeader& header = header_of(handle_.data());
  if (header.closed.load(std::memory_order_acquire) != 0) {
    return std::unexpected(ProcessRendezvousError::kClosed);
  }
  header.producer_ready.store(1, std::memory_order_release);
  handle_.notify();
  return {};
}

auto ProcessRendezvous::is_producer_ready() const noexcept -> bool {
  return handle_.valid() &&
         header_of(handle_.data())
                 .producer_ready.load(std::memory_order_acquire) != 0;
}

auto ProcessRendezvous::try_wait_producer_ready() const noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle_.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  return try_wait_flag(handle_, header_of(handle_.data()).producer_ready);
}

auto ProcessRendezvous::wait_producer_ready_for(
    std::chrono::nanoseconds timeout) const noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle_.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  return wait_flag_until(handle_, header_of(handle_.data()).producer_ready,
                         deadline_after(timeout));
}

auto ProcessRendezvous::wait_producer_ready() const noexcept
    -> std::expected<void, ProcessRendezvousError> {
  return wait_producer_ready_for(std::chrono::nanoseconds::max());
}

auto ProcessRendezvous::signal_consumer_ready() noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle_.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  RendezvousHeader& header = header_of(handle_.data());
  if (header.closed.load(std::memory_order_acquire) != 0) {
    return std::unexpected(ProcessRendezvousError::kClosed);
  }
  header.consumer_ready.store(1, std::memory_order_release);
  handle_.notify();
  return {};
}

auto ProcessRendezvous::is_consumer_ready() const noexcept -> bool {
  return handle_.valid() &&
         header_of(handle_.data())
                 .consumer_ready.load(std::memory_order_acquire) != 0;
}

auto ProcessRendezvous::try_wait_consumer_ready() const noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle_.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  return try_wait_flag(handle_, header_of(handle_.data()).consumer_ready);
}

auto ProcessRendezvous::wait_consumer_ready_for(
    std::chrono::nanoseconds timeout) const noexcept
    -> std::expected<void, ProcessRendezvousError> {
  if (!handle_.valid()) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  return wait_flag_until(handle_, header_of(handle_.data()).consumer_ready,
                         deadline_after(timeout));
}

auto ProcessRendezvous::wait_consumer_ready() const noexcept
    -> std::expected<void, ProcessRendezvousError> {
  return wait_consumer_ready_for(std::chrono::nanoseconds::max());
}

void ProcessRendezvous::close() noexcept {
  if (!handle_.valid()) {
    return;
  }
  header_of(handle_.data()).closed.store(1, std::memory_order_release);
  handle_.notify();
}

auto ProcessRendezvous::is_closed() const noexcept -> bool {
  return handle_.valid() &&
         header_of(handle_.data()).closed.load(std::memory_order_acquire) != 0;
}

}  // namespace systems
