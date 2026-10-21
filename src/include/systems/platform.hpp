#pragma once

/// @file platform.hpp
/// @brief Compile-time operating-system capability flags for the systems
/// track.
///
/// Systems components target POSIX (Linux, embedded Linux, and QNX Neutrino).
/// macOS is a development host only: it provides POSIX shared memory and
/// process-shared mutexes / condition variables, but no robust mutexes
/// (`PTHREAD_MUTEX_ROBUST`), and it limits shared-memory names to 31
/// characters.
///
/// Use these flags instead of scattering `#ifdef __linux__` through component
/// code:
///   - in OS adapters, to choose an implementation strategy with
///     `if constexpr` (or a narrowly scoped `#if` when a symbol does not exist
///     on a platform, for example `pthread_mutexattr_setrobust`);
///   - in tests, to `GTEST_SKIP()` requirements that the host cannot satisfy,
///     with a reason that names the missing capability.
///
/// Every flag is a `constexpr bool` or `constexpr std::size_t`, so the header
/// has no runtime cost and no link dependency.

#include <cstddef>
#include <cstdint>
#include <new>

namespace systems::platform {

/// @brief Host operating-system family.
enum class OsFamily : std::uint8_t {
  kLinux,  ///< Linux, including embedded Linux distributions.
  kQnx,    ///< QNX Neutrino / QNX OS for Safety.
  kMacos,  ///< macOS (development host only).
  kOther,  ///< Anything else; capabilities are reported conservatively.
};

#if defined(__QNX__) || defined(__QNXNTO__)
inline constexpr OsFamily kOsFamily = OsFamily::kQnx;
#elif defined(__linux__)
inline constexpr OsFamily kOsFamily = OsFamily::kLinux;
#elif defined(__APPLE__)
inline constexpr OsFamily kOsFamily = OsFamily::kMacos;
#else
inline constexpr OsFamily kOsFamily = OsFamily::kOther;
#endif

/// @brief Host CPU architecture family.
enum class ArchFamily : std::uint8_t {
  kX86_64,   ///< 64-bit x86 (x86-64 / AMD64).
  kAarch64,  ///< 64-bit ARM (AArch64 / arm64, including Apple Silicon).
  kOther,    ///< Any other architecture.
};

#if defined(__x86_64__) || defined(_M_X64)
inline constexpr ArchFamily kArchFamily = ArchFamily::kX86_64;
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
inline constexpr ArchFamily kArchFamily = ArchFamily::kAarch64;
#else
inline constexpr ArchFamily kArchFamily = ArchFamily::kOther;
#endif

/// @brief True on Linux (desktop or embedded).
inline constexpr bool kIsLinux = kOsFamily == OsFamily::kLinux;
/// @brief True on QNX Neutrino.
inline constexpr bool kIsQnx = kOsFamily == OsFamily::kQnx;
/// @brief True on macOS (development host).
inline constexpr bool kIsMacos = kOsFamily == OsFamily::kMacos;
/// @brief True on a production target OS (Linux or QNX).
inline constexpr bool kIsProductionTarget = kIsLinux || kIsQnx;

/// @brief True on 64-bit x86 (x86_64).
inline constexpr bool kIsX86_64 = kArchFamily == ArchFamily::kX86_64;
/// @brief True on 64-bit ARM (aarch64 / arm64).
inline constexpr bool kIsAarch64 = kArchFamily == ArchFamily::kAarch64;

/// @brief True on a supported host OS and architecture combination:
/// Linux (x86_64 or aarch64) or macOS on Apple Silicon (arm64).
inline constexpr bool kIsSupportedTarget =
    (kIsLinux && (kIsX86_64 || kIsAarch64)) || (kIsMacos && kIsAarch64);

/// @brief `shm_open` / `shm_unlink` / `mmap(MAP_SHARED)` are available.
inline constexpr bool kHasPosixSharedMemory = kIsLinux || kIsQnx || kIsMacos;

/// @brief Named POSIX semaphores (`sem_open`, `sem_post`, `sem_trywait`,
/// `sem_wait`, `sem_close`, `sem_unlink`) are available.
inline constexpr bool kHasPosixNamedSemaphore = kIsLinux || kIsQnx || kIsMacos;

/// @brief `sem_timedwait` is available on named POSIX semaphores. Present on
/// Linux and QNX; absent on macOS (Darwin XNU).
inline constexpr bool kHasPosixSemTimedwait = kIsLinux || kIsQnx;

/// @brief `pthread_mutexattr_setpshared(PTHREAD_PROCESS_SHARED)` works.
inline constexpr bool kHasProcessSharedMutex = kIsLinux || kIsQnx || kIsMacos;

/// @brief `pthread_condattr_setpshared(PTHREAD_PROCESS_SHARED)` works.
inline constexpr bool kHasProcessSharedCondvar = kIsLinux || kIsQnx || kIsMacos;

/// @brief Robust mutexes are available: a lock held by a process that dies is
/// reported to the next locker as `EOWNERDEAD` and can be recovered with
/// `pthread_mutex_consistent`. Absent on macOS.
inline constexpr bool kHasRobustMutex = kIsLinux || kIsQnx;

/// @brief `mmap(..., MAP_POPULATE, ...)` pre-faults a mapping's page-table
/// entries during the call (Linux only). Elsewhere, pages are faulted in by
/// an explicit touch loop.
inline constexpr bool kHasMapPopulate = kIsLinux;

/// @brief `shm_ctl(fd, SHMCTL_ANON | SHMCTL_PHYS, ...)` allocates physically
/// contiguous, non-pageable shared memory at once (QNX only). Reserved for a
/// QNX port of the shared-memory components; Linux instead pre-faults and
/// `mlock`s.
inline constexpr bool kHasPhysicalShmCtl = kIsQnx;

/// @brief Longest portable POSIX shared-memory object name, including the
/// leading `/`. macOS allows 31 characters; 30 leaves headroom and is accepted
/// by Linux (`NAME_MAX`) and QNX.
inline constexpr std::size_t kMaxShmNameLength = 30;

/// @brief Cache-line size used to separate data written by different cores or
/// processes (false-sharing avoidance) in shared layouts. Apple Silicon uses a
/// 128-byte coherence granule (`hw.cachelinesize`).
#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr std::size_t kCacheLineSize = 128;
#elif defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t kCacheLineSize =
    std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLineSize = 64;
#endif

static_assert(kCacheLineSize >= alignof(std::max_align_t),
              "cache line must be at least max_align_t");

}  // namespace systems::platform
