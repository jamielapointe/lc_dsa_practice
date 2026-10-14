/// @file shm_pubsub_posix.cpp
/// @brief POSIX OS adapter for the shared-memory pub/sub channel.
///
/// The only translation unit of this component allowed to call the operating
/// system (SHMPS-REQ-021). Porting to another OS means rewriting this file.
///
/// Memory residency (SHMPS-REQ-023 / REQ-024)
/// ------------------------------------------
/// Linux backs POSIX shared memory with tmpfs pages that are allocated
/// lazily, on first touch, and every process has its own page tables. Left
/// alone, the first `publish()` into each page would take a page fault (the
/// kernel stops the thread, allocates or maps a page, and returns). That is
/// microseconds of jitter in a hard-real-time path, and it can also stall on
/// reclaim. So both entry points finish all faulting during initialization:
///
///   | Step      | `create()` (new object) | `open()` (maybe live)        |
///   | --------- | ----------------------- | ---------------------------- |
///   | mmap      | `MAP_SHARED`            | `MAP_SHARED \| MAP_POPULATE` |
///   | pre-fault | write 0 to every page   | read-touch every page        |
///   | wire      | `mlock()` + retry       | `mlock()` + retry            |
///
/// `create()` may write because the object is brand new and all zeros, and
/// no other process can treat it as a channel before the magic number is
/// stored. `open()` must not write: a late subscriber or a publisher
/// restarted after a crash maps a segment that already holds samples.
///
/// Page size comes from `sysconf(_SC_PAGESIZE)`, never a hard-coded 4 KiB:
/// AArch64 kernels are commonly built with 16 KiB or 64 KiB pages.
///
/// `mlock` is retried only for `EAGAIN` ("some pages could not be locked
/// right now"). `EPERM` / `ENOMEM` mean `RLIMIT_MEMLOCK` (or a missing
/// `CAP_IPC_LOCK`) forbids it, which no retry fixes. Either way the call
/// fails with `kMemoryLockFailed`, because an unwired real-time buffer is a
/// boot-time fault, not something to discover at run time.
///
/// QNX notes (not implemented; how a port would differ)
/// ---------------------------------------------------
/// QNX Neutrino is built for deterministic memory, so most of the Linux
/// work above disappears:
///   - Allocation: replace `ftruncate` with
///     `shm_ctl(fd, SHMCTL_ANON | SHMCTL_PHYS, 0, bytes)`. The kernel then
///     allocates *physically contiguous* memory immediately, so there are no
///     lazy pages to pre-fault. For a pool reserved by the board's startup
///     code, use `posix_typed_mem_open("/ram/<pool>", O_RDWR,
///     POSIX_TYPED_MEM_ALLOCATE_CONTIG)` and map that instead.
///   - Locking: QNX does not swap. Mappings can still be lazy (`MAP_LAZY`),
///     and the system may run `procnto` without superlocking. Keep the
///     `mlock` call (or rely on `procnto -mL` / `mlockall`) per the target's
///     safety manual.
///   - Privileges: `SHMCTL_PHYS` and locking need the procmgr abilities
///     `PROCMGR_AID_MEM_PHYS` / `PROCMGR_AID_MEM_LOCK` for non-root
///     processes (`procmgr_ability()`).
///   - `open()`: only map; the pages are already physical and resident.
///   - Liveness: `getpgid(pid)` (POSIX, `<unistd.h>`) works the same; the
///     `/proc/<pid>/stat` zombie check below is Linux-specific (QNX would
///     query `/proc/<pid>/as` `DCMD_PROC_INFO` instead).
///   - Capability flags already reserved in `systems/platform.hpp`:
///     `kHasPhysicalShmCtl`. A port adds a `shm_pubsub_qnx.cpp` (or narrow
///     `#if defined(__QNX__)` blocks around QNX-only symbols here).
///
/// macOS (Darwin / Apple Silicon arm64) notes
/// ------------------------------------------
///   - Allocation: XNU's `shm_open` + `ftruncate` rounds segment sizes up to
///     the 16 KiB VM page size (`_SC_PAGESIZE`), so `fstat` in `open()` may
///     report a page-rounded size larger than `region_bytes(config)`.
///   - Pre-fault and locking: `MAP_POPULATE` is Linux-only (`kHasMapPopulate`
///     is false on macOS), so `open()` relies on `mlock()` plus the explicit
///     `prefault_by_reading()` touch loop to install page-table entries.
///   - Zombie detection: Darwin has no `/proc`; `is_zombie()` queries
///     `sysctl({CTL_KERN, KERN_PROC, KERN_PROC_PID, pid})` and checks
///     `kp_proc.p_stat == SZOMB`.

#if defined(SYSTEMS_POSIX_SUPPORTED) &&                                       \
    ((defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) || \
     (defined(__APPLE__) && defined(__aarch64__)))

#include <fcntl.h>
#include <sys/mman.h>
#if defined(__APPLE__)
#include <sys/proc.h>
#include <sys/sysctl.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

#include "systems/platform.hpp"
#include "systems/shm_pubsub.hpp"

static_assert(systems::platform::kIsSupportedTarget,
              "shm_pubsub_posix.cpp compiled on an unsupported target");

namespace systems::detail {
namespace {

// `MAP_POPULATE` exists only on Linux. This is a narrowly scoped `#if`
// around a symbol that does not exist elsewhere, tied to the platform
// capability flag by the static_assert below.
#if defined(MAP_POPULATE)
constexpr int kPopulateFlag = MAP_POPULATE;
#else
constexpr int kPopulateFlag = 0;
#endif
static_assert(!platform::kHasMapPopulate || kPopulateFlag != 0,
              "platform says MAP_POPULATE exists but the headers lack it");

/// Retries after the first `mlock` attempt (EAGAIN only).
constexpr int kMlockRetries = 3;
/// First retry delay; doubles per retry (1, 2, 4 ms).
constexpr auto kMlockFirstDelay = std::chrono::milliseconds{1};
/// Fallback when `sysconf` cannot report the page size.
constexpr std::size_t kFallbackPageSize = 4096;

/// NUL-terminated copy of a name for the C API. Bounded, no heap
/// (SHMPS-REQ-018 does not apply to init, but we stay heap-free anyway).
/// Callers validate names to `platform::kMaxShmNameLength`; the buffer is
/// sized for that limit.
using CName = std::array<char, platform::kMaxShmNameLength + 1>;

[[nodiscard]] auto to_c_name(std::string_view name) noexcept
    -> std::optional<CName> {
  if (name.size() >= CName{}.size()) {
    return std::nullopt;
  }
  CName c_name{};  // zero-filled, so the terminator is already there
  name.copy(c_name.data(), name.size());
  return c_name;
}

/// @brief Calls `fn` again while it fails with EINTR (a signal arrived).
template <typename Fn>
[[nodiscard]] auto retry_on_eintr(Fn fn) noexcept {
  for (;;) {
    auto result = fn();
    if (result != -1 || errno != EINTR) {
      return result;
    }
  }
}

/// @brief RAII owner of a file descriptor. The mapping keeps the object
/// alive, so the descriptor is closed as soon as `mmap` is done.
class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) noexcept : fd_{fd} {}
  ~FileDescriptor() {
    if (fd_ >= 0) {
      static_cast<void>(::close(fd_));  // close is not retried on EINTR (POSIX)
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

/// @brief `mmap(PROT_READ | PROT_WRITE, MAP_SHARED | extra_flags)`.
/// @return The base address, or nullptr on failure (errno set).
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

/// @brief create() pre-fault: write a dummy byte into every page.
///
/// The first write to each page of the new tmpfs object makes the kernel
/// allocate a physical page and install a writable entry in our page
/// table. The object is freshly `ftruncate`d (all zeros), so writing 0 changes
/// nothing. The store goes through `std::atomic_ref` (relaxed) so the
/// compiler cannot drop it as a "useless" store, and tools like TSan see a
/// well-defined access.
void prefault_by_writing(std::span<std::byte> region) noexcept {
  const std::size_t step = page_size();
  for (std::size_t offset = 0; offset < region.size(); offset += step) {
    std::atomic_ref{region[offset]}.store(std::byte{0},
                                          std::memory_order_relaxed);
  }
}

/// @brief open() pre-fault: read-touch every page, never write.
///
/// The physical pages already exist (the creator allocated them). What this
/// process lacks are page-table entries. `MAP_POPULATE` and `mlock` normally
/// fill them already; the explicit touch makes the guarantee independent of
/// those flags (for example on hosts without `MAP_POPULATE`). Relaxed atomic
/// loads are well defined even while a publisher writes the same bytes.
void prefault_by_reading(std::span<std::byte> region) noexcept {
  const std::size_t step = page_size();
  std::byte sink{0};
  for (std::size_t offset = 0; offset < region.size(); offset += step) {
    sink |= std::atomic_ref{region[offset]}.load(std::memory_order_relaxed);
  }
  // Make the loads observable so they are not optimized away.
  std::atomic_signal_fence(std::memory_order_seq_cst);
  static_cast<void>(sink);
}

/// @brief Wires `region` into RAM. Retries transient `EAGAIN` up to
/// `kMlockRetries` times with exponential back-off.
///
/// Note for test builds: the ASan and TSan runtimes intercept `mlock()` and
/// return success without locking anything (locking would also pin their
/// huge shadow mappings). Lock accounting and lock-failure behavior can only
/// be observed in non-sanitizer or UBSan builds.
/// @return True when the pages are locked.
[[nodiscard]] auto lock_pages(std::span<std::byte> region) noexcept -> bool {
  auto delay = kMlockFirstDelay;
  for (int attempt = 0;; ++attempt) {
    if (::mlock(region.data(), region.size()) == 0) {
      return true;
    }
    // EPERM / ENOMEM: RLIMIT_MEMLOCK or missing CAP_IPC_LOCK. Retrying
    // cannot help; fail initialization now.
    if (errno != EAGAIN || attempt >= kMlockRetries) {
      return false;
    }
    std::this_thread::sleep_for(delay);
    delay *= 2;
  }
}

/// @brief True if the OS reports `pid` as a zombie (`Z`/`X` in
/// `/proc/<pid>/stat` on Linux, `SZOMB` via `sysctl` `KERN_PROC_PID` on macOS).
///
/// `getpgid(pid)` still succeeds for a zombie: a process that has died but
/// whose parent has not reaped it yet. A publisher killed under a careless
/// supervisor would otherwise block every takeover until the parent calls
/// `wait()`.
[[nodiscard]] auto is_zombie(std::int64_t pid) noexcept -> bool {
  if constexpr (platform::kIsMacos) {
#if defined(__APPLE__)
    std::array<int, 4> mib{CTL_KERN, KERN_PROC, KERN_PROC_PID,
                           static_cast<int>(pid)};
    struct kinfo_proc info{};
    std::size_t length = sizeof(info);
    if (::sysctl(mib.data(), static_cast<unsigned int>(mib.size()), &info,
                 &length, nullptr, 0) != 0 ||
        length == 0) {
      return false;
    }
    return info.kp_proc.p_stat == SZOMB;
#endif
  }
  if constexpr (!platform::kIsLinux) {
    static_cast<void>(pid);
    return false;
  } else {
    // Build "/proc/<pid>/stat" in a fixed buffer (zero-filled, so the NUL
    // terminator is implicit).
    constexpr std::string_view kPrefix = "/proc/";
    constexpr std::string_view kSuffix = "/stat";
    std::array<char, 48> path{};
    kPrefix.copy(path.data(), kPrefix.size());
    const std::span<char> digits = std::span{path}.subspan(
        kPrefix.size(), path.size() - kPrefix.size() - kSuffix.size() - 1);
    const auto [end, ec] =
        std::to_chars(digits.data(), std::to_address(digits.end()), pid);
    if (ec != std::errc{}) {
      return false;
    }
    kSuffix.copy(end, kSuffix.size());

    // NOLINTBEGIN(*-vararg): POSIX open is variadic.
    const FileDescriptor fd{retry_on_eintr(
        [&] { return ::open(path.data(), O_RDONLY | O_CLOEXEC); })};
    // NOLINTEND(*-vararg)
    if (!fd.valid()) {
      return false;
    }
    std::array<char, 512> stat{};
    const auto bytes = retry_on_eintr(
        [&] { return ::read(fd.get(), stat.data(), stat.size() - 1); });
    if (bytes <= 0) {
      return false;
    }
    // Format: "<pid> (<comm>) <state> ...". `comm` may contain spaces or
    // parentheses, so the state is the character after the LAST ')'.
    const std::string_view text{stat.data(), static_cast<std::size_t>(bytes)};
    const auto close_paren = text.rfind(')');
    if (close_paren == std::string_view::npos ||
        close_paren + 2 >= text.size()) {
      return false;
    }
    const char state = text[close_paren + 2];
    return state == 'Z' || state == 'X';
  }
}

}  // namespace

SharedMemoryRegion::~SharedMemoryRegion() {
  if (base_ != nullptr) {
    // Unmapping also drops this process's mlock on the range. The object
    // itself lives on until it is unlinked and every process has unmapped it.
    static_cast<void>(::munmap(base_, size_));
  }
}

auto SharedMemoryRegion::create(std::string_view name,
                                std::size_t bytes) noexcept
    -> std::expected<SharedMemoryRegion, ShmPubSubError> {
  const auto c_name = to_c_name(name);
  if (!c_name || bytes == 0) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }

  // O_EXCL: fail instead of re-initializing a live channel (SHMPS-REQ-003).
  // 0600: owner-only access (security is out of scope, so be conservative).
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  const FileDescriptor fd{retry_on_eintr([&] {
    return ::shm_open(c_name->data(), O_CREAT | O_EXCL | O_RDWR, 0600);
  })};
  // NOLINTEND(*-vararg)
  if (!fd.valid()) {
    return std::unexpected(errno == EEXIST ? ShmPubSubError::kAlreadyExists
                                           : ShmPubSubError::kOsError);
  }
  // From here on we own a new name: every failure must unlink it again.
  const auto fail = [&](ShmPubSubError error) {
    static_cast<void>(::shm_unlink(c_name->data()));
    return std::unexpected(error);
  };

  if (retry_on_eintr([&] {
        return ::ftruncate(fd.get(), static_cast<off_t>(bytes));
      }) != 0) {
    return fail(ShmPubSubError::kOsError);
  }
  std::byte* const base = map_shared(fd.get(), bytes, 0);
  if (base == nullptr) {
    return fail(ShmPubSubError::kOsError);
  }
  // Own the mapping right away: an early return below unmaps it.
  SharedMemoryRegion region{base, bytes};
  const std::span<std::byte> memory{base, bytes};

  prefault_by_writing(memory);  // allocate every page now, not in publish()
  if (!lock_pages(memory)) {    // and keep it in RAM
    return fail(ShmPubSubError::kMemoryLockFailed);
  }
  return region;
}

auto SharedMemoryRegion::open(std::string_view name) noexcept
    -> std::expected<SharedMemoryRegion, ShmPubSubError> {
  const auto c_name = to_c_name(name);
  if (!c_name) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  const FileDescriptor fd{
      retry_on_eintr([&] { return ::shm_open(c_name->data(), O_RDWR, 0); })};
  // NOLINTEND(*-vararg)
  if (!fd.valid()) {
    return std::unexpected(errno == ENOENT ? ShmPubSubError::kNotFound
                                           : ShmPubSubError::kOsError);
  }
  struct stat info{};
  if (::fstat(fd.get(), &info) != 0 || info.st_size < 0) {
    return std::unexpected(ShmPubSubError::kOsError);
  }
  const auto bytes = static_cast<std::size_t>(info.st_size);
  if (bytes == 0) {
    // mmap rejects length 0. Report an empty region and let the layout
    // check classify it (a half-created or foreign object).
    return SharedMemoryRegion{};
  }
  // MAP_POPULATE: fault every page into our page table during the mmap
  // call itself (read-fault semantics for shared mappings: nothing is
  // written).
  std::byte* const base = map_shared(fd.get(), bytes, kPopulateFlag);
  if (base == nullptr) {
    return std::unexpected(ShmPubSubError::kOsError);
  }
  SharedMemoryRegion region{base, bytes};
  const std::span<std::byte> memory{base, bytes};

  if (!lock_pages(memory)) {
    return std::unexpected(ShmPubSubError::kMemoryLockFailed);
  }
  prefault_by_reading(memory);
  return region;
}

auto SharedMemoryRegion::unlink(std::string_view name) noexcept
    -> std::expected<void, ShmPubSubError> {
  const auto c_name = to_c_name(name);
  if (!c_name) {
    return std::unexpected(ShmPubSubError::kInvalidArgument);
  }
  if (::shm_unlink(c_name->data()) != 0) {
    return std::unexpected(errno == ENOENT ? ShmPubSubError::kNotFound
                                           : ShmPubSubError::kOsError);
  }
  return {};
}

auto SharedMemoryRegion::process_alive(std::int64_t pid) noexcept -> bool {
  if (pid <= 0) {
    return false;
  }
  // getpgid only looks the process up; it sends nothing and changes
  // nothing. ESRCH means "no such process": gone. Any other error (POSIX
  // allows EPERM for a process in another session) proves the pid exists,
  // or at least does not prove it is gone, so treat it as alive.
  if (::getpgid(static_cast<pid_t>(pid)) == -1) {
    return errno != ESRCH;
  }
  return !is_zombie(pid);
}

auto SharedMemoryRegion::current_process_id() noexcept -> std::int64_t {
  return static_cast<std::int64_t>(::getpid());
}

}  // namespace systems::detail

#else
#error \
    "shm_pubsub_posix.cpp requires a supported POSIX target (Linux x86_64/aarch64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
