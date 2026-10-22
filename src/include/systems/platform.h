#pragma once

/// @file platform.h
/// @brief Compile-time operating-system capability flags and bit utilities for
/// the ISO C23 systems track.
///
/// Systems components target POSIX (Linux, embedded Linux, and QNX Neutrino).
/// macOS is a development host only: it provides POSIX shared memory and
/// process-shared mutexes / condition variables, but no robust mutexes
/// (PTHREAD_MUTEX_ROBUST), and it limits shared-memory names to 31 characters.
///
/// Dual-language safe: can be included by both ISO C23 and C++23 translation
/// units.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Host operating-system family enumeration with fixed uint8_t
/// underlying type.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum systems_os_family : uint8_t {
  SYSTEMS_OS_LINUX = 0,
  SYSTEMS_OS_QNX = 1,
  SYSTEMS_OS_MACOS = 2,
  SYSTEMS_OS_OTHER = 3,
} systems_os_family;

/// @brief Host CPU architecture family enumeration with fixed uint8_t
/// underlying type.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
typedef enum systems_arch_family : uint8_t {
  SYSTEMS_ARCH_X86_64 = 0,
  SYSTEMS_ARCH_AARCH64 = 1,
  SYSTEMS_ARCH_OTHER = 2,
} systems_arch_family;

// NOLINTBEGIN(cppcoreguidelines-macro-usage,modernize-macro-to-enum)
#if defined(__QNX__) || defined(__QNXNTO__)
#define SYSTEMS_OS_CURRENT SYSTEMS_OS_QNX
#elif defined(__linux__)
#define SYSTEMS_OS_CURRENT SYSTEMS_OS_LINUX
#elif defined(__APPLE__)
#define SYSTEMS_OS_CURRENT SYSTEMS_OS_MACOS
#else
#define SYSTEMS_OS_CURRENT SYSTEMS_OS_OTHER
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define SYSTEMS_ARCH_CURRENT SYSTEMS_ARCH_X86_64
#elif defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define SYSTEMS_ARCH_CURRENT SYSTEMS_ARCH_AARCH64
#else
#define SYSTEMS_ARCH_CURRENT SYSTEMS_ARCH_OTHER
#endif
// NOLINTEND(cppcoreguidelines-macro-usage,modernize-macro-to-enum)

/// @brief Detects the current host operating-system family.
[[nodiscard]] static inline systems_os_family systems_get_os_family() {
  return SYSTEMS_OS_CURRENT;
}

/// @brief Detects the current host CPU architecture family.
[[nodiscard]] static inline systems_arch_family systems_get_arch_family() {
  return SYSTEMS_ARCH_CURRENT;
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage,modernize-macro-to-enum)
#if defined(__linux__)
#define SYSTEMS_IS_LINUX 1
#else
#define SYSTEMS_IS_LINUX 0
#endif

#if defined(__QNX__) || defined(__QNXNTO__)
#define SYSTEMS_IS_QNX 1
#else
#define SYSTEMS_IS_QNX 0
#endif

#if defined(__APPLE__)
#define SYSTEMS_IS_MACOS 1
#else
#define SYSTEMS_IS_MACOS 0
#endif

#define SYSTEMS_IS_PRODUCTION_TARGET (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX)

#if defined(__x86_64__) || defined(_M_X64)
#define SYSTEMS_IS_X86_64 1
#else
#define SYSTEMS_IS_X86_64 0
#endif

#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
#define SYSTEMS_IS_AARCH64 1
#else
#define SYSTEMS_IS_AARCH64 0
#endif

#define SYSTEMS_IS_SUPPORTED_TARGET                                   \
  ((SYSTEMS_IS_LINUX && (SYSTEMS_IS_X86_64 || SYSTEMS_IS_AARCH64)) || \
   (SYSTEMS_IS_MACOS && SYSTEMS_IS_AARCH64))

#define SYSTEMS_HAS_POSIX_SHM \
  (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX || SYSTEMS_IS_MACOS)
#define SYSTEMS_HAS_POSIX_NAMED_SEMAPHORE \
  (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX || SYSTEMS_IS_MACOS)
#define SYSTEMS_HAS_POSIX_SEM_TIMEDWAIT (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX)
#define SYSTEMS_HAS_PROCESS_SHARED_MUTEX \
  (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX || SYSTEMS_IS_MACOS)
#define SYSTEMS_HAS_PROCESS_SHARED_CONDVAR \
  (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX || SYSTEMS_IS_MACOS)
#define SYSTEMS_HAS_ROBUST_MUTEX (SYSTEMS_IS_LINUX || SYSTEMS_IS_QNX)
#define SYSTEMS_HAS_MAP_POPULATE SYSTEMS_IS_LINUX
#define SYSTEMS_HAS_PHYSICAL_SHM_CTL SYSTEMS_IS_QNX

#define SYSTEMS_MAX_SHM_NAME_LENGTH 30U

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define SYSTEMS_CACHE_LINE_SIZE 128U
#else
#define SYSTEMS_CACHE_LINE_SIZE 64U
#endif
// NOLINTEND(cppcoreguidelines-macro-usage,modernize-macro-to-enum)

static_assert(SYSTEMS_CACHE_LINE_SIZE >= alignof(max_align_t),
              "cache line must be at least alignof(max_align_t)");

static constexpr size_t systems_cache_line_size = SYSTEMS_CACHE_LINE_SIZE;
static constexpr size_t systems_max_shm_name_length =
    SYSTEMS_MAX_SHM_NAME_LENGTH;
static constexpr bool systems_is_linux = (SYSTEMS_IS_LINUX != 0);
static constexpr bool systems_is_qnx = (SYSTEMS_IS_QNX != 0);
static constexpr bool systems_is_macos = (SYSTEMS_IS_MACOS != 0);
static constexpr bool systems_is_production_target =
    (SYSTEMS_IS_PRODUCTION_TARGET != 0);
static constexpr bool systems_is_x86_64 = (SYSTEMS_IS_X86_64 != 0);
static constexpr bool systems_is_aarch64 = (SYSTEMS_IS_AARCH64 != 0);
static constexpr bool systems_is_supported_target =
    (SYSTEMS_IS_SUPPORTED_TARGET != 0);
static constexpr bool systems_has_posix_shm = (SYSTEMS_HAS_POSIX_SHM != 0);
static constexpr bool systems_has_posix_named_semaphore =
    (SYSTEMS_HAS_POSIX_NAMED_SEMAPHORE != 0);
static constexpr bool systems_has_posix_sem_timedwait =
    (SYSTEMS_HAS_POSIX_SEM_TIMEDWAIT != 0);
static constexpr bool systems_has_process_shared_mutex =
    (SYSTEMS_HAS_PROCESS_SHARED_MUTEX != 0);
static constexpr bool systems_has_process_shared_condvar =
    (SYSTEMS_HAS_PROCESS_SHARED_CONDVAR != 0);
static constexpr bool systems_has_robust_mutex =
    (SYSTEMS_HAS_ROBUST_MUTEX != 0);
static constexpr bool systems_has_map_populate =
    (SYSTEMS_HAS_MAP_POPULATE != 0);
static constexpr bool systems_has_physical_shm_ctl =
    (SYSTEMS_HAS_PHYSICAL_SHM_CTL != 0);

#if __has_include(<stdbit.h>)
#include <stdbit.h>
#else
// NOLINTBEGIN(google-runtime-int,cppcoreguidelines-macro-usage)
#ifdef __cplusplus
#define SYSTEMS_CAST_UINT(expr) static_cast<unsigned int>(expr)
#else
#define SYSTEMS_CAST_UINT(expr) ((unsigned int)(expr))
#endif

/// @brief Fallback count of leading zeros for 32-bit unsigned int.
[[nodiscard]] static inline unsigned int stdc_leading_zeros_ui(
    unsigned int val) {
  return (val == 0U) ? SYSTEMS_CAST_UINT(sizeof(unsigned int) * 8U)
                     : SYSTEMS_CAST_UINT(__builtin_clz(val));
}

/// @brief Fallback count of trailing zeros for 32-bit unsigned int.
[[nodiscard]] static inline unsigned int stdc_trailing_zeros_ui(
    unsigned int val) {
  return (val == 0U) ? SYSTEMS_CAST_UINT(sizeof(unsigned int) * 8U)
                     : SYSTEMS_CAST_UINT(__builtin_ctz(val));
}

/// @brief Fallback population count (set bits) for 32-bit unsigned int.
[[nodiscard]] static inline unsigned int stdc_count_ones_ui(unsigned int val) {
  return SYSTEMS_CAST_UINT(__builtin_popcount(val));
}

/// @brief Fallback count of leading zeros for 8-bit unsigned char.
[[nodiscard]] static inline unsigned int stdc_leading_zeros_uc(
    unsigned char val) {
  return (val == 0U)
             ? 8U
             : SYSTEMS_CAST_UINT(__builtin_clz(SYSTEMS_CAST_UINT(val))) -
                   SYSTEMS_CAST_UINT((sizeof(unsigned int) * 8U) - 8U);
}

/// @brief Fallback count of trailing zeros for 8-bit unsigned char.
[[nodiscard]] static inline unsigned int stdc_trailing_zeros_uc(
    unsigned char val) {
  return (val == 0U) ? 8U
                     : SYSTEMS_CAST_UINT(__builtin_ctz(SYSTEMS_CAST_UINT(val)));
}

/// @brief Fallback population count (set bits) for 8-bit unsigned char.
[[nodiscard]] static inline unsigned int stdc_count_ones_uc(unsigned char val) {
  return SYSTEMS_CAST_UINT(__builtin_popcount(SYSTEMS_CAST_UINT(val)));
}

/// @brief Fallback count of leading zeros for 64-bit unsigned long long.
[[nodiscard]] static inline unsigned int stdc_leading_zeros_ull(
    unsigned long long val) {
  return (val == 0ULL) ? SYSTEMS_CAST_UINT(sizeof(unsigned long long) * 8U)
                       : SYSTEMS_CAST_UINT(__builtin_clzll(val));
}

/// @brief Fallback count of trailing zeros for 64-bit unsigned long long.
[[nodiscard]] static inline unsigned int stdc_trailing_zeros_ull(
    unsigned long long val) {
  return (val == 0ULL) ? SYSTEMS_CAST_UINT(sizeof(unsigned long long) * 8U)
                       : SYSTEMS_CAST_UINT(__builtin_ctzll(val));
}

/// @brief Fallback population count (set bits) for 64-bit unsigned long long.
[[nodiscard]] static inline unsigned int stdc_count_ones_ull(
    unsigned long long val) {
  return SYSTEMS_CAST_UINT(__builtin_popcountll(val));
}

#undef SYSTEMS_CAST_UINT
// NOLINTEND(google-runtime-int,cppcoreguidelines-macro-usage)
#endif

#ifdef __cplusplus
}  // extern "C"
#endif
