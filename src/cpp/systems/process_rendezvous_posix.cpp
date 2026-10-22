/// @file process_rendezvous_posix.cpp
/// @brief POSIX / Linux / Darwin OS adapter for `systems::ProcessRendezvous`.
///
/// The only translation unit of `ProcessRendezvous` allowed to call the
/// operating system (PRV-REQ-011).
///
/// Platform strategy
/// -----------------
///   - Shared state lives in a POSIX shared-memory mapping (`shm_open` +
///     `mmap(MAP_SHARED)`), pre-faulted and locked into RAM (`mlock`).
///   - Cross-process wakeup uses a POSIX named semaphore (`sem_open` /
///     `sem_post`):
///       * On Linux (`platform::kHasPosixSemTimedwait == true`), `wait_slice`
///         sleeps in the kernel via `sem_timedwait` and re-posts on wake so all
///         concurrent waiters are released immediately.
///       * On macOS Apple Silicon (`platform::kHasPosixSemTimedwait == false`),
///         XNU provides `sem_open` / `sem_post` / `sem_trywait` in
///         `<semaphore.h>` but omits `sem_timedwait`. `wait_slice` checks
///         `sem_trywait` (re-posting on success) and otherwise sleeps for a
///         bounded monotonic slice (`std::min(slice, 200us)`).

#if defined(SYSTEMS_POSIX_SUPPORTED) &&                                       \
    ((defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) || \
     (defined(__APPLE__) && defined(__aarch64__)))

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef>
#if defined(HAVE_POSIX_SEM_TIMEDWAIT)
#include <ctime>
#endif
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <thread>

#include "systems/platform.hpp"
#include "systems/process_rendezvous.hpp"

static_assert(systems::platform::kIsSupportedTarget,
              "process_rendezvous_posix.cpp compiled on an unsupported target");
static_assert(systems::platform::kHasPosixNamedSemaphore,
              "process_rendezvous_posix.cpp requires POSIX named semaphores");
#if defined(HAVE_POSIX_SEM_TIMEDWAIT)
static_assert(systems::platform::kHasPosixSemTimedwait,
              "CMake detected sem_timedwait but platform.hpp disagrees");
#else
static_assert(!systems::platform::kHasPosixSemTimedwait,
              "platform.hpp expects sem_timedwait but CMake did not find it");
#endif

namespace systems::detail {
namespace {

#if defined(MAP_POPULATE)
constexpr int kPopulateFlag = MAP_POPULATE;
#else
constexpr int kPopulateFlag = 0;
#endif

constexpr int kMlockRetries = 3;
constexpr auto kMlockFirstDelay = std::chrono::milliseconds{1};
constexpr std::size_t kFallbackPageSize = 4096;

using CName = std::array<char, platform::kMaxShmNameLength + 1>;

[[nodiscard]] auto to_c_name(std::string_view name) noexcept
    -> std::optional<CName> {
  if (name.size() >= CName{}.size()) {
    return std::nullopt;
  }
  CName c_name{};
  name.copy(c_name.data(), name.size());
  return c_name;
}

template <typename Fn>
[[nodiscard]] auto retry_on_eintr(Fn fn) noexcept {
  for (;;) {
    auto result = fn();
    if (result != -1 || errno != EINTR) {
      return result;
    }
  }
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) noexcept : fd_{fd} {}
  ~FileDescriptor() {
    if (fd_ >= 0) {
      static_cast<void>(::close(fd_));
    }
  }
  FileDescriptor(const FileDescriptor&) = delete;
  auto operator=(const FileDescriptor&) -> FileDescriptor& = delete;
  FileDescriptor(FileDescriptor&&) = delete;
  auto operator=(FileDescriptor&&) -> FileDescriptor& = delete;

  [[nodiscard]] auto get() const noexcept -> int { return fd_; }
  [[nodiscard]] auto valid() const noexcept -> bool { return fd_ >= 0; }

 private:
  int fd_{-1};
};

[[nodiscard]] auto page_size() noexcept -> std::size_t {
  // NOLINTNEXTLINE(google-runtime-int): sysconf's POSIX return type is long.
  const long size = ::sysconf(_SC_PAGESIZE);
  return size > 0 ? static_cast<std::size_t>(size) : kFallbackPageSize;
}

[[nodiscard]] auto map_shared(int fd, std::size_t bytes,
                              int extra_flags) noexcept -> std::byte* {
  void* const base = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                            MAP_SHARED | extra_flags, fd, 0);
  // NOLINTNEXTLINE(*-cstyle-cast,performance-no-int-to-ptr): MAP_FAILED.
  if (base == MAP_FAILED) {
    return nullptr;
  }
  return static_cast<std::byte*>(base);
}

void prefault_by_writing(std::span<std::byte> region) noexcept {
  const std::size_t step = page_size();
  for (std::size_t offset = 0; offset < region.size(); offset += step) {
    std::atomic_ref{region[offset]}.store(std::byte{0},
                                          std::memory_order_relaxed);
  }
}

void prefault_by_reading(std::span<std::byte> region) noexcept {
  const std::size_t step = page_size();
  std::byte sink{0};
  for (std::size_t offset = 0; offset < region.size(); offset += step) {
    sink |= std::atomic_ref{region[offset]}.load(std::memory_order_relaxed);
  }
  std::atomic_signal_fence(std::memory_order_seq_cst);
  static_cast<void>(sink);
}

[[nodiscard]] auto lock_pages(std::span<std::byte> region) noexcept -> bool {
  auto delay = kMlockFirstDelay;
  for (int attempt = 0;; ++attempt) {
    if (::mlock(region.data(), region.size()) == 0) {
      return true;
    }
    if (errno != EAGAIN || attempt >= kMlockRetries) {
      return false;
    }
    std::this_thread::sleep_for(delay);
    delay *= 2;
  }
}

[[nodiscard]] auto open_named_sem(const char* c_name, bool fresh) noexcept
    -> void* {
  if (fresh) {
    static_cast<void>(::sem_unlink(c_name));
  }
  // NOLINTBEGIN(*-vararg): POSIX sem_open is variadic.
  sem_t* const sem = ::sem_open(c_name, O_CREAT, 0600, 0);
  // NOLINTEND(*-vararg)
  // NOLINTNEXTLINE(*-cstyle-cast,performance-no-int-to-ptr): SEM_FAILED.
  if (sem == SEM_FAILED) {
    return nullptr;
  }
  return static_cast<void*>(sem);
}

}  // namespace

RendezvousOsHandle::~RendezvousOsHandle() {
  if (sem_ != nullptr) {
    static_cast<void>(::sem_close(static_cast<sem_t*>(sem_)));
  }
  if (base_ != nullptr) {
    static_cast<void>(::munmap(base_, size_));
  }
}

auto RendezvousOsHandle::create(std::string_view name,
                                std::size_t bytes) noexcept
    -> std::expected<RendezvousOsHandle, ProcessRendezvousError> {
  const auto c_name = to_c_name(name);
  if (!c_name || bytes == 0) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }

  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  const FileDescriptor fd{retry_on_eintr([&] {
    return ::shm_open(c_name->data(), O_CREAT | O_EXCL | O_RDWR, 0600);
  })};
  // NOLINTEND(*-vararg)
  if (!fd.valid()) {
    return std::unexpected(errno == EEXIST
                               ? ProcessRendezvousError::kAlreadyExists
                               : ProcessRendezvousError::kOsError);
  }

  const auto fail = [&](ProcessRendezvousError error) {
    static_cast<void>(::shm_unlink(c_name->data()));
    static_cast<void>(::sem_unlink(c_name->data()));
    return std::unexpected(error);
  };

  if (retry_on_eintr([&] {
        return ::ftruncate(fd.get(), static_cast<off_t>(bytes));
      }) != 0) {
    return fail(ProcessRendezvousError::kOsError);
  }
  std::byte* const base = map_shared(fd.get(), bytes, 0);
  if (base == nullptr) {
    return fail(ProcessRendezvousError::kOsError);
  }
  void* const sem = open_named_sem(c_name->data(), /*fresh=*/true);
  RendezvousOsHandle handle{base, bytes, sem};
  const std::span<std::byte> memory{base, bytes};

  prefault_by_writing(memory);
  if (!lock_pages(memory)) {
    return fail(ProcessRendezvousError::kMemoryLockFailed);
  }
  return handle;
}

auto RendezvousOsHandle::open(std::string_view name) noexcept
    -> std::expected<RendezvousOsHandle, ProcessRendezvousError> {
  const auto c_name = to_c_name(name);
  if (!c_name) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  const FileDescriptor fd{
      retry_on_eintr([&] { return ::shm_open(c_name->data(), O_RDWR, 0); })};
  // NOLINTEND(*-vararg)
  if (!fd.valid()) {
    return std::unexpected(errno == ENOENT ? ProcessRendezvousError::kNotFound
                                           : ProcessRendezvousError::kOsError);
  }
  struct stat info{};
  if (::fstat(fd.get(), &info) != 0 || info.st_size < 0) {
    return std::unexpected(ProcessRendezvousError::kOsError);
  }
  const auto bytes = static_cast<std::size_t>(info.st_size);
  if (bytes == 0) {
    return RendezvousOsHandle{};
  }
  std::byte* const base = map_shared(fd.get(), bytes, kPopulateFlag);
  if (base == nullptr) {
    return std::unexpected(ProcessRendezvousError::kOsError);
  }
  void* const sem = open_named_sem(c_name->data(), /*fresh=*/false);
  RendezvousOsHandle handle{base, bytes, sem};
  const std::span<std::byte> memory{base, bytes};

  if (!lock_pages(memory)) {
    return std::unexpected(ProcessRendezvousError::kMemoryLockFailed);
  }
  prefault_by_reading(memory);
  return handle;
}

auto RendezvousOsHandle::unlink(std::string_view name) noexcept
    -> std::expected<void, ProcessRendezvousError> {
  const auto c_name = to_c_name(name);
  if (!c_name) {
    return std::unexpected(ProcessRendezvousError::kInvalidArgument);
  }
  static_cast<void>(::sem_unlink(c_name->data()));
  if (::shm_unlink(c_name->data()) != 0) {
    return std::unexpected(errno == ENOENT ? ProcessRendezvousError::kNotFound
                                           : ProcessRendezvousError::kOsError);
  }
  return {};
}

void RendezvousOsHandle::notify() const noexcept {
  if (sem_ != nullptr) {
    static_cast<void>(::sem_post(static_cast<sem_t*>(sem_)));
  }
}

void RendezvousOsHandle::wait_slice(
    std::chrono::nanoseconds slice) const noexcept {
  if (slice <= std::chrono::nanoseconds::zero()) {
    return;
  }
  if constexpr (platform::kHasPosixSemTimedwait) {
#if defined(HAVE_POSIX_SEM_TIMEDWAIT)
    if (sem_ != nullptr) {
      const auto cap_slice = std::min<std::chrono::nanoseconds>(
          slice, std::chrono::milliseconds{50});
      const auto deadline_tp =
          std::chrono::system_clock::now() +
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
              cap_slice);
      const auto since_epoch = deadline_tp.time_since_epoch();
      const auto secs =
          std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
      const auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(
          since_epoch - secs);
      std::timespec deadline{};
      deadline.tv_sec = static_cast<decltype(deadline.tv_sec)>(secs.count());
      deadline.tv_nsec = static_cast<decltype(deadline.tv_nsec)>(nsecs.count());
      if (::sem_timedwait(static_cast<sem_t*>(sem_), &deadline) == 0) {
        // Re-post so other processes/threads waiting on the same stage wake
        // too.
        static_cast<void>(::sem_post(static_cast<sem_t*>(sem_)));
      }
      return;
    }
#endif
  }
  if (sem_ != nullptr && ::sem_trywait(static_cast<sem_t*>(sem_)) == 0) {
    static_cast<void>(::sem_post(static_cast<sem_t*>(sem_)));
    return;
  }
  constexpr auto kPollStep = std::chrono::microseconds{200};
  std::this_thread::sleep_for(
      std::min<std::chrono::nanoseconds>(slice, kPollStep));
}

}  // namespace systems::detail

#else
#error \
    "process_rendezvous_posix.cpp requires a supported POSIX target (Linux x86_64/aarch64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
