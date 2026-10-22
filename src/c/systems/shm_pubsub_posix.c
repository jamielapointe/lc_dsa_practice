/// @file shm_pubsub_posix.c
/// @brief POSIX OS adapter for the shared-memory pub/sub channel in ISO C23.
///
/// The only translation unit of this component allowed to call the operating
/// system (SHMPS-REQ-021).

#include "systems/platform.h"

#if defined(SYSTEMS_POSIX_SUPPORTED) &&                                       \
    ((defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) || \
     (defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))))

#include <errno.h>  // IWYU pragma: keep
#include <fcntl.h>  // IWYU pragma: keep
#include <sched.h>  // IWYU pragma: keep
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>  // IWYU pragma: keep
#if defined(__APPLE__)
#include <sys/proc.h>
#include <sys/sysctl.h>
#endif
#include <sys/stat.h>  // IWYU pragma: keep
#include <time.h>      // IWYU pragma: keep
#include <unistd.h>    // IWYU pragma: keep

#include "shm_pubsub_posix.h"
#include "systems/shm_pubsub.h"

static_assert(systems_is_supported_target,
              "shm_pubsub_posix.c compiled on an unsupported target");

#if defined(MAP_POPULATE)
static constexpr int kPopulateFlag = MAP_POPULATE;
#else
static constexpr int kPopulateFlag = 0;
#endif
static_assert((!systems_has_map_populate || kPopulateFlag != 0) != 0,
              "platform says MAP_POPULATE exists but the headers lack it");

static constexpr int kMlockRetries = 3;
static constexpr int64_t kMlockFirstDelayNs = 1'000'000LL;
static constexpr size_t kFallbackPageSize = 4096U;

static size_t get_page_size() {
  // NOLINTNEXTLINE(google-runtime-int): sysconf's POSIX return type is long.
  const long sz = sysconf(_SC_PAGESIZE);
  return sz > 0 ? (size_t)sz : kFallbackPageSize;
}

static void prefault_by_writing(uint8_t* base, size_t bytes) {
  const size_t step = get_page_size();
  for (size_t offset = 0U; offset < bytes; offset += step) {
    _Atomic(uint8_t)* cell = (_Atomic(uint8_t)*)(base + offset);
    atomic_store_explicit(cell, 0U, memory_order_relaxed);
  }
}

static void prefault_by_reading(const uint8_t* base, size_t bytes) {
  const size_t step = get_page_size();
  uint8_t sink = 0U;
  for (size_t offset = 0U; offset < bytes; offset += step) {
    const _Atomic(uint8_t)* cell = (const _Atomic(uint8_t)*)(base + offset);
    sink = (uint8_t)(sink | atomic_load_explicit(cell, memory_order_relaxed));
  }
  atomic_signal_fence(memory_order_seq_cst);
  (void)sink;
}

static bool lock_pages(void* base, size_t bytes) {
  int64_t delay_ns = kMlockFirstDelayNs;
  for (int attempt = 0;; ++attempt) {
    if (mlock(base, bytes) == 0) {
      return true;
    }
    if (errno != EAGAIN || attempt >= kMlockRetries) {
      return false;
    }
    shm_pubsub_posix_sleep_ns(delay_ns);
    delay_ns *= 2LL;
  }
}

static bool is_zombie_process(int64_t pid) {
#if defined(__APPLE__)
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)pid};
  struct kinfo_proc info = {};
  size_t length = sizeof(info);
  if (sysctl(mib, 4U, &info, &length, nullptr, 0) != 0 || length == 0U) {
    return false;
  }
  return info.kp_proc.p_stat == SZOMB;
#elif defined(__linux__)
  if (pid <= 0) {
    return false;
  }
  char path[48] = {'/', 'p', 'r', 'o', 'c', '/'};
  size_t pos = 6U;
  uint64_t val = (uint64_t)pid;
  char rev[24] = {};
  size_t digits = 0U;
  while (val > 0U) {
    rev[digits++] = (char)('0' + (val % 10U));
    val /= 10U;
  }
  while (digits > 0U) {
    path[pos++] = rev[--digits];
  }
  static constexpr char kSuffix[] = "/stat";
  for (size_t i = 0U; i < sizeof(kSuffix); ++i) {
    path[pos + i] = kSuffix[i];
  }
  // NOLINTBEGIN(*-vararg): POSIX open is variadic.
  int fd = -1;
  do {
    fd = open(path, O_RDONLY | O_CLOEXEC);
  } while (fd == -1 && errno == EINTR);
  // NOLINTEND(*-vararg)
  if (fd < 0) {
    return false;
  }
  char stat_buf[512] = {};
  ssize_t bytes_read = -1;
  do {
    bytes_read = read(fd, stat_buf, sizeof(stat_buf) - 1U);
  } while (bytes_read == -1 && errno == EINTR);
  (void)close(fd);
  if (bytes_read <= 0) {
    return false;
  }
  stat_buf[bytes_read] = '\0';
  const char* close_paren = nullptr;
  for (ssize_t i = bytes_read - 1; i >= 0; --i) {
    if (stat_buf[i] == ')') {
      close_paren = &stat_buf[i];
      break;
    }
  }
  if (close_paren == nullptr || close_paren[1] == '\0' ||
      close_paren[2] == '\0') {
    return false;
  }
  const char state = close_paren[2];
  bool is_dead = state == 'Z';
  if (state == 'X') {
    is_dead = true;
  }
  return is_dead;
#else
  (void)pid;
  return false;
#endif
}

enum shm_pubsub_error shm_pubsub_posix_create(struct shm_channel* channel,
                                              const char* name, size_t bytes) {
  if (channel == nullptr || name == nullptr || bytes == 0U) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }

  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  int fd = -1;
  do {
    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
  } while (fd == -1 && errno == EINTR);
  // NOLINTEND(*-vararg)

  if (fd < 0) {
    return errno == EEXIST ? SHM_PUBSUB_ERR_ALREADY_EXISTS
                           : SHM_PUBSUB_ERR_OS_ERROR;
  }

  int trunc_rc = -1;
  do {
    trunc_rc = ftruncate(fd, (off_t)bytes);
  } while (trunc_rc == -1 && errno == EINTR);

  if (trunc_rc != 0) {
    (void)close(fd);
    (void)shm_unlink(name);
    return SHM_PUBSUB_ERR_OS_ERROR;
  }

  void* base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  (void)close(fd);
  if (base == MAP_FAILED) {
    (void)shm_unlink(name);
    return SHM_PUBSUB_ERR_OS_ERROR;
  }

  uint8_t* byte_base = (uint8_t*)base;
  prefault_by_writing(byte_base, bytes);
  if (!lock_pages(byte_base, bytes)) {
    (void)munmap(byte_base, bytes);
    (void)shm_unlink(name);
    return SHM_PUBSUB_ERR_MEMORY_LOCK_FAILED;
  }

  channel->base = byte_base;
  channel->size = bytes;
  return SHM_PUBSUB_OK;
}

enum shm_pubsub_error shm_pubsub_posix_open(struct shm_channel* channel,
                                            const char* name) {
  if (channel == nullptr || name == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }

  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic on Darwin.
  int fd = -1;
  do {
    fd = shm_open(name, O_RDWR, 0);
  } while (fd == -1 && errno == EINTR);
  // NOLINTEND(*-vararg)

  if (fd < 0) {
    return errno == ENOENT ? SHM_PUBSUB_ERR_NOT_FOUND : SHM_PUBSUB_ERR_OS_ERROR;
  }

  struct stat info = {};
  if (fstat(fd, &info) != 0 || info.st_size < 0) {
    (void)close(fd);
    return SHM_PUBSUB_ERR_OS_ERROR;
  }

  const size_t bytes = (size_t)info.st_size;
  if (bytes == 0U) {
    (void)close(fd);
    channel->base = nullptr;
    channel->size = 0U;
    return SHM_PUBSUB_OK;
  }

  void* base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                    MAP_SHARED | kPopulateFlag, fd, 0);
  (void)close(fd);
  if (base == MAP_FAILED) {
    return SHM_PUBSUB_ERR_OS_ERROR;
  }

  uint8_t* byte_base = (uint8_t*)base;
  if (!lock_pages(byte_base, bytes)) {
    (void)munmap(byte_base, bytes);
    return SHM_PUBSUB_ERR_MEMORY_LOCK_FAILED;
  }
  prefault_by_reading(byte_base, bytes);

  channel->base = byte_base;
  channel->size = bytes;
  return SHM_PUBSUB_OK;
}

enum shm_pubsub_error shm_pubsub_posix_unlink(const char* name) {
  if (name == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  if (shm_unlink(name) != 0) {
    return errno == ENOENT ? SHM_PUBSUB_ERR_NOT_FOUND : SHM_PUBSUB_ERR_OS_ERROR;
  }
  return SHM_PUBSUB_OK;
}

void shm_pubsub_posix_unmap(struct shm_channel* channel) {
  if (channel == nullptr) {
    return;
  }
  if (channel->base != nullptr && channel->size > 0U) {
    (void)munmap(channel->base, channel->size);
  }
  channel->base = nullptr;
  channel->size = 0U;
}

bool shm_pubsub_posix_process_alive(int64_t pid) {
  if (pid <= 0) {
    return false;
  }
  if (getpgid((pid_t)pid) == -1) {
    return errno != ESRCH;
  }
  if (is_zombie_process(pid)) {
    return false;
  }
  return true;
}

int64_t shm_pubsub_posix_current_pid() { return (int64_t)getpid(); }

void shm_pubsub_posix_yield() { (void)sched_yield(); }

void shm_pubsub_posix_sleep_ns(int64_t sleep_ns) {
  if (sleep_ns <= 0) {
    return;
  }
  struct timespec req = {
      .tv_sec = (time_t)(sleep_ns / 1'000'000'000LL),
      .tv_nsec = (long)(sleep_ns % 1'000'000'000LL),
  };
  nanosleep(&req, nullptr);
}

#else
#error \
    "shm_pubsub_posix.c requires a supported POSIX target (Linux x86_64/aarch64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
