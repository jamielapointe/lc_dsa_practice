/// @file shm_pubsub.c
/// @brief Portable channel logic for shm_channel, shm_publisher, and
/// shm_subscriber in ISO C23.
///
/// Free of POSIX headers (SHMPS-REQ-021): every OS call goes through
/// shm_pubsub_posix.c.
/// Requirements: docs/systems/shm_pubsub.md.

#include "systems/shm_pubsub.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "shm_pubsub_posix.h"
#include "systems/platform.h"

/// "SHMPSUB1" in ASCII. Identifies a channel segment.
static constexpr uint64_t kShmMagic = 0x5348'4D50'5355'4231ULL;
/// Layout version checked by shm_channel_open().
static constexpr uint32_t kLayoutVersion = 1U;
/// Payload words are 64-bit unsigned integers.
static constexpr size_t kWordBytes = sizeof(uint64_t);

/// @brief Channel-wide state at offset 0 of the shared mapping (3 cache lines).
struct shm_channel_header {
  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint64_t) magic;
  uint32_t layout_version;
  uint32_t slot_count;
  uint32_t slot_size;
  uint32_t slot_stride;

  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint64_t) published;
  _Atomic(int64_t) publisher_pid;

  alignas(SYSTEMS_CACHE_LINE_SIZE) _Atomic(uint32_t) closed;
};

/// @brief Metadata at the start of every ring-buffer slot (16 bytes).
struct shm_slot_header {
  _Atomic(uint64_t) seq;
  _Atomic(uint32_t) size;
  uint32_t reserved;
};

static_assert(ATOMIC_LLONG_LOCK_FREE == 2,
              "Cross-process 64-bit atomics must be always lock-free");
static_assert(ATOMIC_INT_LOCK_FREE == 2,
              "Cross-process 32-bit atomics must be always lock-free");
static_assert(sizeof(struct shm_slot_header) == 2U * kWordBytes,
              "SlotHeader must be 16 bytes");
static_assert(sizeof(struct shm_channel_header) % SYSTEMS_CACHE_LINE_SIZE == 0U,
              "ChannelHeader size must be a multiple of cache line size");

static constexpr size_t kHeaderBytes = sizeof(struct shm_channel_header);
static constexpr size_t kSlotHeaderBytes = sizeof(struct shm_slot_header);

[[nodiscard]] const char* shm_pubsub_error_to_string(
    enum shm_pubsub_error error) {
  switch (error) {
    case SHM_PUBSUB_OK:
      return "ok";
    case SHM_PUBSUB_ERR_NOT_IMPLEMENTED:
      return "not implemented";
    case SHM_PUBSUB_ERR_INVALID_ARGUMENT:
      return "invalid argument";
    case SHM_PUBSUB_ERR_NAME_TOO_LONG:
      return "name too long";
    case SHM_PUBSUB_ERR_ALREADY_EXISTS:
      return "already exists";
    case SHM_PUBSUB_ERR_NOT_FOUND:
      return "not found";
    case SHM_PUBSUB_ERR_LAYOUT_MISMATCH:
      return "layout mismatch";
    case SHM_PUBSUB_ERR_PUBLISHER_EXISTS:
      return "publisher exists";
    case SHM_PUBSUB_ERR_PAYLOAD_TOO_LARGE:
      return "payload too large";
    case SHM_PUBSUB_ERR_BUFFER_TOO_SMALL:
      return "buffer too small";
    case SHM_PUBSUB_ERR_WOULD_BLOCK:
      return "would block";
    case SHM_PUBSUB_ERR_TIMED_OUT:
      return "timed out";
    case SHM_PUBSUB_ERR_CLOSED:
      return "closed";
    case SHM_PUBSUB_ERR_MEMORY_LOCK_FAILED:
      return "memory lock failed";
    case SHM_PUBSUB_ERR_OS_ERROR:
      return "operating-system error";
  }
  return "unknown error";
}

[[nodiscard]] static size_t round_up_size(size_t value, size_t multiple) {
  return ((value + multiple - 1U) / multiple) * multiple;
}

[[nodiscard]] static uint32_t slot_stride_for(uint32_t slot_size) {
  return (uint32_t)round_up_size(
      kSlotHeaderBytes + round_up_size((size_t)slot_size, kWordBytes),
      SYSTEMS_CACHE_LINE_SIZE);
}

[[nodiscard]] static size_t region_bytes_for(struct shm_pubsub_config config) {
  return kHeaderBytes + ((size_t)config.slot_count *
                         (size_t)slot_stride_for(config.slot_size));
}

[[nodiscard]] static struct shm_channel_header* header_of(uint8_t* base) {
  return (struct shm_channel_header*)(void*)base;
}

[[nodiscard]] static const struct shm_channel_header* const_header_of(
    const uint8_t* base) {
  return (const struct shm_channel_header*)(const void*)base;
}

struct shm_slot_ref {
  struct shm_slot_header* header;
  _Atomic(uint64_t)* words;
  size_t word_count;
};

[[nodiscard]] static struct shm_slot_ref slot_for(struct shm_channel_view view,
                                                  uint64_t sequence) {
  const size_t index = (size_t)((sequence - 1ULL) & (uint64_t)view.slot_mask);
  uint8_t* const slot_base =
      view.base + kHeaderBytes + (index * (size_t)view.slot_stride);
  struct shm_slot_header* const hdr = (struct shm_slot_header*)(void*)slot_base;
  _Atomic(uint64_t)* const words =
      (_Atomic(uint64_t)*)(void*)(slot_base + kSlotHeaderBytes);
  const size_t word_count =
      round_up_size((size_t)view.slot_size, kWordBytes) / kWordBytes;
  return (struct shm_slot_ref){
      .header = hdr, .words = words, .word_count = word_count};
}

static void store_payload(_Atomic(uint64_t)* words, const uint8_t* payload,
                          size_t payload_size) {
  const size_t full_words = payload_size / kWordBytes;
  for (size_t i = 0U; i < full_words; ++i) {
    uint64_t word = 0ULL;
    if (payload != nullptr) {
      memcpy(&word, payload + (i * kWordBytes), kWordBytes);
    }
    atomic_store_explicit(&words[i], word, memory_order_relaxed);
  }
  const size_t tail = payload_size % kWordBytes;
  if (tail != 0U) {
    uint64_t word = 0ULL;
    if (payload != nullptr) {
      memcpy(&word, payload + (full_words * kWordBytes), tail);
    }
    atomic_store_explicit(&words[full_words], word, memory_order_relaxed);
  }
}

static void load_payload(const _Atomic(uint64_t)* words, uint8_t* out,
                         size_t out_size) {
  const size_t full_words = out_size / kWordBytes;
  for (size_t i = 0U; i < full_words; ++i) {
    const uint64_t word = atomic_load_explicit(&words[i], memory_order_relaxed);
    if (out != nullptr) {
      memcpy(out + (i * kWordBytes), &word, kWordBytes);
    }
  }
  const size_t tail = out_size % kWordBytes;
  if (tail != 0U) {
    const uint64_t word =
        atomic_load_explicit(&words[full_words], memory_order_relaxed);
    if (out != nullptr) {
      memcpy(out + (full_words * kWordBytes), &word, tail);
    }
  }
}

enum shm_read_outcome : uint8_t {
  SHM_READ_OK = 0U,
  SHM_READ_BUFFER_TOO_SMALL = 1U,
  SHM_READ_OVERWRITTEN = 2U,
  SHM_READ_NOT_READY = 3U,
};

[[nodiscard]] static enum shm_read_outcome read_slot(
    struct shm_channel_view view, uint64_t sequence, uint8_t* buffer,
    size_t buffer_size, uint32_t* size_out) {
  const struct shm_slot_ref slot = slot_for(view, sequence);
  const uint64_t wanted = 2ULL * sequence;

  const uint64_t v1 =
      atomic_load_explicit(&slot.header->seq, memory_order_acquire);
  if (v1 != wanted) {
    return v1 > wanted ? SHM_READ_OVERWRITTEN : SHM_READ_NOT_READY;
  }

  const uint32_t size =
      atomic_load_explicit(&slot.header->size, memory_order_relaxed);
  const bool size_plausible = size <= view.slot_size;
  bool fits = false;
  if (size_plausible && (size_t)size <= buffer_size) {
    if (size == 0U || buffer != nullptr) {
      fits = true;
    }
  }
  if (fits && size > 0U) {
    load_payload(slot.words, buffer, (size_t)size);
  }

  atomic_thread_fence(memory_order_acquire);
  const uint64_t v2 =
      atomic_load_explicit(&slot.header->seq, memory_order_relaxed);
  if (v2 != v1 || !size_plausible) {
    return SHM_READ_OVERWRITTEN;
  }
  if (!fits) {
    return SHM_READ_BUFFER_TOO_SMALL;
  }
  *size_out = size;
  return SHM_READ_OK;
}

[[nodiscard]] static enum shm_pubsub_error validate_name(const char* name) {
  if (name == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  const size_t len = strlen(name);
  if (len < 2U || name[0] != '/' || strchr(name + 1, '/') != nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  if (len > SYSTEMS_MAX_SHM_NAME_LENGTH) {
    return SHM_PUBSUB_ERR_NAME_TOO_LONG;
  }
  return SHM_PUBSUB_OK;
}

[[nodiscard]] static bool config_is_valid(struct shm_pubsub_config config) {
  if (config.slot_count < SHM_MIN_SLOT_COUNT ||
      config.slot_count > SHM_MAX_SLOT_COUNT) {
    return false;
  }
  if ((config.slot_count & (config.slot_count - 1U)) != 0U) {
    return false;
  }
  if (config.slot_size < 1U || config.slot_size > SHM_MAX_SLOT_SIZE) {
    return false;
  }
  return true;
}

static void initialize_layout(uint8_t* base, struct shm_pubsub_config config) {
  struct shm_channel_header* const hdr = header_of(base);
  memset(hdr, 0, sizeof(*hdr));
  hdr->layout_version = kLayoutVersion;
  hdr->slot_count = config.slot_count;
  hdr->slot_size = config.slot_size;
  hdr->slot_stride = slot_stride_for(config.slot_size);
  atomic_store_explicit(&hdr->published, 0ULL, memory_order_relaxed);
  atomic_store_explicit(&hdr->publisher_pid, 0LL, memory_order_relaxed);
  atomic_store_explicit(&hdr->closed, 0U, memory_order_relaxed);

  const struct shm_channel_view view = {
      .base = base,
      .slot_mask = config.slot_count - 1U,
      .slot_size = config.slot_size,
      .slot_stride = hdr->slot_stride,
  };
  for (uint64_t sequence = 1ULL; sequence <= (uint64_t)config.slot_count;
       ++sequence) {
    struct shm_slot_header* const slot_hdr = slot_for(view, sequence).header;
    atomic_store_explicit(&slot_hdr->seq, 0ULL, memory_order_relaxed);
    atomic_store_explicit(&slot_hdr->size, 0U, memory_order_relaxed);
    slot_hdr->reserved = 0U;
  }
  atomic_store_explicit(&hdr->magic, kShmMagic, memory_order_release);
}

[[nodiscard]] static bool layout_is_valid(const struct shm_channel* channel) {
  if (channel == nullptr || channel->base == nullptr ||
      channel->size < kHeaderBytes) {
    return false;
  }
  const struct shm_channel_header* const hdr = const_header_of(channel->base);
  if (atomic_load_explicit(&hdr->magic, memory_order_acquire) != kShmMagic ||
      hdr->layout_version != kLayoutVersion) {
    return false;
  }
  const struct shm_pubsub_config config = {
      .slot_count = hdr->slot_count,
      .slot_size = hdr->slot_size,
  };
  if (!config_is_valid(config) ||
      hdr->slot_stride != slot_stride_for(config.slot_size)) {
    return false;
  }
  return channel->size >= region_bytes_for(config);
}

[[nodiscard]] static struct shm_channel_view channel_view(
    const struct shm_channel* channel) {
  if (channel == nullptr || channel->base == nullptr) {
    return (struct shm_channel_view){};
  }
  const struct shm_channel_header* const hdr = const_header_of(channel->base);
  return (struct shm_channel_view){
      .base = channel->base,
      .slot_mask = hdr->slot_count - 1U,
      .slot_size = hdr->slot_size,
      .slot_stride = hdr->slot_stride,
  };
}

[[nodiscard]] static uint64_t recover_next_sequence(
    struct shm_channel_view view) {
  uint64_t newest = atomic_load_explicit(&header_of(view.base)->published,
                                         memory_order_acquire);
  const uint64_t slot_count = (uint64_t)view.slot_mask + 1ULL;
  for (uint64_t sequence = 1ULL; sequence <= slot_count; ++sequence) {
    const uint64_t lock = atomic_load_explicit(
        &slot_for(view, sequence).header->seq, memory_order_acquire);
    const uint64_t committed = lock / 2ULL;
    if (committed > newest) {
      newest = committed;
    }
  }
  return newest + 1ULL;
}

static void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
  __asm__ volatile("yield" ::: "memory");
#else
  atomic_signal_fence(memory_order_seq_cst);
#endif
}

[[nodiscard]] static int64_t monotonic_now_ns() {
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((int64_t)ts.tv_sec * 1'000'000'000LL) + (int64_t)ts.tv_nsec;
}

struct shm_backoff {
  int spins;
  bool yielded;
  int64_t sleep_ns;
};

static void backoff_wait(struct shm_backoff* backoff, int64_t deadline_ns) {
  static constexpr int kSpinLimit = 64;
  static constexpr int64_t kMaxSleepNs = 1'000'000LL;  // 1 ms

  if (backoff->spins < kSpinLimit) {
    ++backoff->spins;
    cpu_relax();
    return;
  }
  if (!backoff->yielded) {
    backoff->yielded = true;
    shm_pubsub_posix_yield();
    return;
  }
  const int64_t now_ns = monotonic_now_ns();
  if (now_ns >= deadline_ns) {
    return;
  }
  const int64_t remaining_ns = deadline_ns - now_ns;
  const int64_t duration_ns =
      backoff->sleep_ns < remaining_ns ? backoff->sleep_ns : remaining_ns;
  shm_pubsub_posix_sleep_ns(duration_ns);
  const int64_t doubled = backoff->sleep_ns * 2LL;
  backoff->sleep_ns = doubled < kMaxSleepNs ? doubled : kMaxSleepNs;
}

enum shm_pubsub_error shm_channel_create(struct shm_channel* channel,
                                         const char* name,
                                         struct shm_pubsub_config config) {
  if (channel == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  *channel = (struct shm_channel){};
  const enum shm_pubsub_error name_err = validate_name(name);
  if (name_err != SHM_PUBSUB_OK) {
    return name_err;
  }
  if (!config_is_valid(config)) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  const enum shm_pubsub_error os_err =
      shm_pubsub_posix_create(channel, name, region_bytes_for(config));
  if (os_err != SHM_PUBSUB_OK) {
    return os_err;
  }
  initialize_layout(channel->base, config);
  return SHM_PUBSUB_OK;
}

enum shm_pubsub_error shm_channel_open(struct shm_channel* channel,
                                       const char* name) {
  if (channel == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  *channel = (struct shm_channel){};
  const enum shm_pubsub_error name_err = validate_name(name);
  if (name_err != SHM_PUBSUB_OK) {
    return name_err;
  }
  const enum shm_pubsub_error os_err = shm_pubsub_posix_open(channel, name);
  if (os_err != SHM_PUBSUB_OK) {
    return os_err;
  }
  if (!layout_is_valid(channel)) {
    shm_pubsub_posix_unmap(channel);
    return SHM_PUBSUB_ERR_LAYOUT_MISMATCH;
  }
  return SHM_PUBSUB_OK;
}

enum shm_pubsub_error shm_channel_unlink(const char* name) {
  const enum shm_pubsub_error name_err = validate_name(name);
  if (name_err != SHM_PUBSUB_OK) {
    return name_err;
  }
  return shm_pubsub_posix_unlink(name);
}

struct shm_pubsub_config shm_channel_config(const struct shm_channel* channel) {
  if (channel == nullptr || channel->base == nullptr) {
    return (struct shm_pubsub_config){};
  }
  const struct shm_channel_header* const hdr = const_header_of(channel->base);
  return (struct shm_pubsub_config){
      .slot_count = hdr->slot_count,
      .slot_size = hdr->slot_size,
  };
}

void shm_channel_close(struct shm_channel* channel) {
  if (channel == nullptr || channel->base == nullptr) {
    return;
  }
  atomic_store_explicit(&header_of(channel->base)->closed, 1U,
                        memory_order_release);
}

bool shm_channel_is_closed(const struct shm_channel* channel) {
  if (channel == nullptr || channel->base == nullptr) {
    return false;
  }
  return atomic_load_explicit(&const_header_of(channel->base)->closed,
                              memory_order_acquire) != 0U;
}

void shm_channel_destroy(struct shm_channel* channel) {
  shm_pubsub_posix_unmap(channel);
}

enum shm_pubsub_error shm_publisher_attach(struct shm_publisher* publisher,
                                           struct shm_channel* channel) {
  if (publisher == nullptr || channel == nullptr || channel->base == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  *publisher = (struct shm_publisher){};
  const struct shm_channel_view view = channel_view(channel);
  struct shm_channel_header* const hdr = header_of(view.base);
  if (atomic_load_explicit(&hdr->closed, memory_order_acquire) != 0U) {
    return SHM_PUBSUB_ERR_CLOSED;
  }

  const int64_t me = shm_pubsub_posix_current_pid();
  int64_t holder = 0;
  if (!atomic_compare_exchange_strong_explicit(&hdr->publisher_pid, &holder, me,
                                               memory_order_acq_rel,
                                               memory_order_acquire)) {
    if (holder == me || shm_pubsub_posix_process_alive(holder)) {
      return SHM_PUBSUB_ERR_PUBLISHER_EXISTS;
    }
    if (!atomic_compare_exchange_strong_explicit(&hdr->publisher_pid, &holder,
                                                 me, memory_order_acq_rel,
                                                 memory_order_acquire)) {
      return SHM_PUBSUB_ERR_PUBLISHER_EXISTS;
    }
  }

  publisher->view = view;
  publisher->next_sequence = recover_next_sequence(view);
  publisher->owner_pid = me;
  return SHM_PUBSUB_OK;
}

enum shm_pubsub_error shm_publisher_publish(struct shm_publisher* publisher,
                                            const void* payload,
                                            size_t payload_size,
                                            uint64_t* out_sequence) {
  if (publisher == nullptr || publisher->view.base == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  if (payload_size > 0U && payload == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  if (payload_size > (size_t)publisher->view.slot_size) {
    return SHM_PUBSUB_ERR_PAYLOAD_TOO_LARGE;
  }
  struct shm_channel_header* const hdr = header_of(publisher->view.base);
  if (atomic_load_explicit(&hdr->closed, memory_order_relaxed) != 0U) {
    return SHM_PUBSUB_ERR_CLOSED;
  }

  const uint64_t sequence = publisher->next_sequence;
  const struct shm_slot_ref slot = slot_for(publisher->view, sequence);

  atomic_store_explicit(&slot.header->seq, (2ULL * sequence) - 1ULL,
                        memory_order_relaxed);
  atomic_thread_fence(memory_order_release);

  store_payload(slot.words, (const uint8_t*)payload, payload_size);
  atomic_store_explicit(&slot.header->size, (uint32_t)payload_size,
                        memory_order_relaxed);

  atomic_store_explicit(&slot.header->seq, 2ULL * sequence,
                        memory_order_release);
  atomic_store_explicit(&hdr->published, sequence, memory_order_release);

  publisher->next_sequence = sequence + 1ULL;
  if (out_sequence != nullptr) {
    *out_sequence = sequence;
  }
  return SHM_PUBSUB_OK;
}

void shm_publisher_destroy(struct shm_publisher* publisher) {
  if (publisher == nullptr || publisher->view.base == nullptr) {
    return;
  }
  if (publisher->owner_pid == shm_pubsub_posix_current_pid()) {
    int64_t expected = publisher->owner_pid;
    (void)atomic_compare_exchange_strong_explicit(
        &header_of(publisher->view.base)->publisher_pid, &expected, 0LL,
        memory_order_release, memory_order_relaxed);
  }
  *publisher = (struct shm_publisher){};
}

enum shm_pubsub_error shm_subscriber_attach(struct shm_subscriber* subscriber,
                                            struct shm_channel* channel,
                                            enum shm_read_mode mode) {
  if (subscriber == nullptr || channel == nullptr || channel->base == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  if (mode != SHM_READ_MODE_IN_ORDER && mode != SHM_READ_MODE_LATEST) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  const struct shm_channel_view view = channel_view(channel);
  const uint64_t newest = atomic_load_explicit(
      &const_header_of(view.base)->published, memory_order_acquire);
  subscriber->view = view;
  subscriber->next_sequence = newest + 1ULL;
  subscriber->mode = mode;
  return SHM_PUBSUB_OK;
}

enum shm_pubsub_error shm_subscriber_try_receive(
    struct shm_subscriber* subscriber, void* buffer, size_t buffer_size,
    struct shm_sample* out_sample) {
  if (subscriber == nullptr || subscriber->view.base == nullptr ||
      out_sample == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  if (buffer_size > 0U && buffer == nullptr) {
    return SHM_PUBSUB_ERR_INVALID_ARGUMENT;
  }
  const struct shm_channel_header* const hdr =
      const_header_of(subscriber->view.base);
  const uint64_t slot_count = (uint64_t)subscriber->view.slot_mask + 1ULL;

  uint64_t cursor = subscriber->next_sequence;
  uint64_t missed = 0ULL;
  bool force_newest = false;

  for (;;) {
    const bool closed =
        atomic_load_explicit(&hdr->closed, memory_order_acquire) != 0U;
    const uint64_t head =
        atomic_load_explicit(&hdr->published, memory_order_acquire);
    if (head < cursor) {
      return closed ? SHM_PUBSUB_ERR_CLOSED : SHM_PUBSUB_ERR_WOULD_BLOCK;
    }

    const uint64_t lag = head - cursor;
    const bool lapped = lag >= slot_count;
    if (lag > 0ULL &&
        (lapped || force_newest || subscriber->mode == SHM_READ_MODE_LATEST)) {
      missed += lag;
      cursor = head;
    }

    uint32_t size = 0U;
    switch (read_slot(subscriber->view, cursor, (uint8_t*)buffer, buffer_size,
                      &size)) {
      case SHM_READ_OK:
        subscriber->next_sequence = cursor + 1ULL;
        *out_sample = (struct shm_sample){
            .sequence = cursor, .size = size, .missed = missed};
        return SHM_PUBSUB_OK;
      case SHM_READ_BUFFER_TOO_SMALL:
        return SHM_PUBSUB_ERR_BUFFER_TOO_SMALL;
      case SHM_READ_NOT_READY:
        return closed ? SHM_PUBSUB_ERR_CLOSED : SHM_PUBSUB_ERR_WOULD_BLOCK;
      case SHM_READ_OVERWRITTEN:
        force_newest = true;
        break;
    }
  }
}

[[nodiscard]] static enum shm_pubsub_error receive_until(
    struct shm_subscriber* subscriber, void* buffer, size_t buffer_size,
    int64_t deadline_ns, struct shm_sample* out_sample) {
  struct shm_backoff backoff = {
      .spins = 0, .yielded = false, .sleep_ns = 50'000LL};
  for (;;) {
    const enum shm_pubsub_error err =
        shm_subscriber_try_receive(subscriber, buffer, buffer_size, out_sample);
    if (err != SHM_PUBSUB_ERR_WOULD_BLOCK) {
      return err;
    }
    if (monotonic_now_ns() >= deadline_ns) {
      return SHM_PUBSUB_ERR_TIMED_OUT;
    }
    backoff_wait(&backoff, deadline_ns);
  }
}

enum shm_pubsub_error shm_subscriber_receive_for(
    struct shm_subscriber* subscriber, void* buffer, size_t buffer_size,
    int64_t timeout_ns, struct shm_sample* out_sample) {
  const int64_t now_ns = monotonic_now_ns();
  int64_t deadline_ns = now_ns;
  if (timeout_ns > 0) {
    if (ckd_add(&deadline_ns, now_ns, timeout_ns)) {
      deadline_ns = INT64_MAX;
    }
  }
  return receive_until(subscriber, buffer, buffer_size, deadline_ns,
                       out_sample);
}

enum shm_pubsub_error shm_subscriber_receive(struct shm_subscriber* subscriber,
                                             void* buffer, size_t buffer_size,
                                             struct shm_sample* out_sample) {
  return receive_until(subscriber, buffer, buffer_size, INT64_MAX, out_sample);
}

void shm_subscriber_destroy(struct shm_subscriber* subscriber) {
  if (subscriber != nullptr) {
    *subscriber = (struct shm_subscriber){};
  }
}
