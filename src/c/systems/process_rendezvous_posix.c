/// @file process_rendezvous_posix.c
/// @brief POSIX / Linux / Darwin OS adapter for process_rendezvous in ISO C23.
///
/// Confines all OS calls (PRV-REQ-011).

#include "systems/platform.h"

#if defined(SYSTEMS_POSIX_SUPPORTED) &&                                       \
    ((defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) || \
     (defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))))

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "process_rendezvous_posix.h"
#include "systems/process_rendezvous.h"

#if defined(MAP_POPULATE)
static constexpr int kPopulateFlag = MAP_POPULATE;
#else
static constexpr int kPopulateFlag = 0;
#endif

static constexpr int kMlockRetries = 3;
static constexpr uint64_t kMlockFirstDelayUs = 1000U;
static constexpr size_t kFallbackPageSize = 4096U;

static size_t get_page_size() {
  const long sz = sysconf(_SC_PAGESIZE);
  return sz > 0 ? (size_t)sz : kFallbackPageSize;
}

static void prefault_memory_write(void* base, size_t bytes) {
  const size_t step = get_page_size();
  volatile uint8_t* ptr = (volatile uint8_t*)base;
  for (size_t offset = 0U; offset < bytes; offset += step) {
    ptr[offset] = 0U;
  }
}

static void prefault_memory_read(const void* base, size_t bytes) {
  const size_t step = get_page_size();
  const volatile uint8_t* ptr = (const volatile uint8_t*)base;
  uint8_t sink = 0U;
  for (size_t offset = 0U; offset < bytes; offset += step) {
    sink = (uint8_t)(sink | ptr[offset]);
  }
  atomic_signal_fence(memory_order_seq_cst);
  (void)sink;
}

static bool lock_mapped_pages(void* base, size_t bytes) {
  uint64_t delay_us = kMlockFirstDelayUs;
  for (int attempt = 0; attempt <= kMlockRetries; ++attempt) {
    if (mlock(base, bytes) == 0) {
      return true;
    }
    if (errno != EAGAIN || attempt >= kMlockRetries) {
      return false;
    }
    process_rendezvous_posix_sleep_us((uint32_t)delay_us);
    delay_us *= 2U;
  }
  return false;
}

static sem_t* open_named_semaphore(const char* name, bool fresh) {
  if (fresh) {
    (void)sem_unlink(name);
  }
  // NOLINTBEGIN(*-vararg): POSIX sem_open is variadic.
  sem_t* sem = sem_open(name, O_CREAT, 0600, 0);
  // NOLINTEND(*-vararg)
  if (sem == SEM_FAILED) {
    return nullptr;
  }
  return sem;
}

enum process_rendezvous_error process_rendezvous_posix_create(
    struct process_rendezvous* rdv, const char* name, size_t bytes) {
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic.
  int fd = -1;
  do {
    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
  } while (fd == -1 && errno == EINTR);
  // NOLINTEND(*-vararg)

  if (fd < 0) {
    return errno == EEXIST ? PROCESS_RENDEZVOUS_ERR_ALREADY_EXISTS
                           : PROCESS_RENDEZVOUS_ERR_OS;
  }

  int trunc_rc = -1;
  do {
    trunc_rc = ftruncate(fd, (off_t)bytes);
  } while (trunc_rc == -1 && errno == EINTR);

  if (trunc_rc != 0) {
    (void)close(fd);
    (void)shm_unlink(name);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }

  void* base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (base == MAP_FAILED) {
    (void)close(fd);
    (void)shm_unlink(name);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }

  sem_t* sem = open_named_semaphore(name, true);
  if (sem == nullptr) {
    (void)munmap(base, bytes);
    (void)close(fd);
    (void)shm_unlink(name);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }

  prefault_memory_write(base, bytes);
  if (!lock_mapped_pages(base, bytes)) {
    (void)sem_close(sem);
    (void)sem_unlink(name);
    (void)munmap(base, bytes);
    (void)close(fd);
    (void)shm_unlink(name);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }

  (void)strncpy(rdv->name, name, SYSTEMS_MAX_SHM_NAME_LENGTH);
  rdv->name[SYSTEMS_MAX_SHM_NAME_LENGTH] = '\0';
  rdv->shm_fd = fd;
  rdv->shared = (struct process_rendezvous_shared*)base;
  rdv->sem = sem;
  rdv->is_creator = true;
  return PROCESS_RENDEZVOUS_OK;
}

enum process_rendezvous_error process_rendezvous_posix_open(
    struct process_rendezvous* rdv, const char* name) {
  // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic.
  int fd = -1;
  do {
    fd = shm_open(name, O_RDWR, 0);
  } while (fd == -1 && errno == EINTR);
  // NOLINTEND(*-vararg)

  if (fd < 0) {
    return errno == ENOENT ? PROCESS_RENDEZVOUS_ERR_NOT_FOUND
                           : PROCESS_RENDEZVOUS_ERR_OS;
  }

  struct stat st = {};
  if (fstat(fd, &st) != 0 || st.st_size < 0) {
    (void)close(fd);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }
  if (st.st_size < (off_t)sizeof(struct process_rendezvous_shared)) {
    (void)close(fd);
    return PROCESS_RENDEZVOUS_ERR_LAYOUT_MISMATCH;
  }

  const size_t bytes = sizeof(struct process_rendezvous_shared);
  void* base = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                    MAP_SHARED | kPopulateFlag, fd, 0);
  if (base == MAP_FAILED) {
    (void)close(fd);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }

  sem_t* sem = open_named_semaphore(name, false);
  if (sem == nullptr) {
    (void)munmap(base, bytes);
    (void)close(fd);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }

  if (!lock_mapped_pages(base, bytes)) {
    (void)sem_close(sem);
    (void)munmap(base, bytes);
    (void)close(fd);
    return PROCESS_RENDEZVOUS_ERR_OS;
  }
  prefault_memory_read(base, bytes);

  (void)strncpy(rdv->name, name, SYSTEMS_MAX_SHM_NAME_LENGTH);
  rdv->name[SYSTEMS_MAX_SHM_NAME_LENGTH] = '\0';
  rdv->shm_fd = fd;
  rdv->shared = (struct process_rendezvous_shared*)base;
  rdv->sem = sem;
  rdv->is_creator = false;
  return PROCESS_RENDEZVOUS_OK;
}

enum process_rendezvous_error process_rendezvous_posix_unlink(
    const char* name) {
  (void)sem_unlink(name);
  if (shm_unlink(name) != 0) {
    return errno == ENOENT ? PROCESS_RENDEZVOUS_ERR_NOT_FOUND
                           : PROCESS_RENDEZVOUS_ERR_OS;
  }
  return PROCESS_RENDEZVOUS_OK;
}

void process_rendezvous_posix_cleanup(struct process_rendezvous* rdv) {
  if (rdv == nullptr) {
    return;
  }
  if (rdv->sem != nullptr) {
    (void)sem_close(rdv->sem);
    rdv->sem = nullptr;
  }
  if (rdv->shared != nullptr) {
    (void)munmap(rdv->shared, sizeof(struct process_rendezvous_shared));
    rdv->shared = nullptr;
  }
  if (rdv->shm_fd >= 0) {
    (void)close(rdv->shm_fd);
    rdv->shm_fd = -1;
  }
  rdv->name[0] = '\0';
  rdv->is_creator = false;
}

void process_rendezvous_posix_notify(const struct process_rendezvous* rdv) {
  if (rdv != nullptr && rdv->sem != nullptr) {
    (void)sem_post(rdv->sem);
  }
}

void process_rendezvous_posix_wait_slice(const struct process_rendezvous* rdv,
                                         int64_t slice_ns) {
  if (rdv == nullptr || rdv->sem == nullptr || slice_ns <= 0) {
    return;
  }

#if defined(HAVE_POSIX_SEM_TIMEDWAIT)
  if (systems_has_posix_sem_timedwait) {
    const int64_t cap_slice = slice_ns > 50'000'000LL ? 50'000'000LL : slice_ns;
    struct timespec now = {};
    clock_gettime(CLOCK_REALTIME, &now);
    struct timespec deadline = {};
    const uint64_t total_nsec = (uint64_t)now.tv_nsec + (uint64_t)cap_slice;
    deadline.tv_sec = now.tv_sec + (time_t)(total_nsec / 1'000'000'000ULL);
    deadline.tv_nsec = (long)(total_nsec % 1'000'000'000ULL);
    if (sem_timedwait(rdv->sem, &deadline) == 0) {
      (void)sem_post(rdv->sem);
    }
    return;
  }
#endif

  if (sem_trywait(rdv->sem) == 0) {
    (void)sem_post(rdv->sem);
    return;
  }

  const int64_t poll_step_ns = slice_ns > 200'000LL ? 200'000LL : slice_ns;
  struct timespec req = {.tv_sec = 0, .tv_nsec = (long)poll_step_ns};
  nanosleep(&req, nullptr);
}

void process_rendezvous_posix_sleep_us(uint32_t us) {
  const uint64_t total_ns = (uint64_t)us * 1000ULL;
  struct timespec req = {
      .tv_sec = (time_t)(total_ns / 1'000'000'000ULL),
      .tv_nsec = (long)(total_ns % 1'000'000'000ULL),
  };
  nanosleep(&req, nullptr);
}

#else
#error \
    "process_rendezvous_posix.c requires a supported POSIX target (Linux x86_64/aarch64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
