/// @file systems_c23_test.c
/// @brief Pure ISO C23 (-std=c23) CTest executable verifying that all public C
/// headers in src/include/systems/*.h compile and execute cleanly in a native
/// C23 translation unit without C++ compiler leakage.

#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "systems/bit_pattern.h"
#include "systems/bitmap_flip.h"
#include "systems/bounded_fifo.h"
#include "systems/huffman_tree.h"
#include "systems/platform.h"
#include "systems/priority_scheduler.h"
#include "systems/process_rendezvous.h"
#include "systems/shm_pubsub.h"

static_assert(__STDC_VERSION__ == 202311L,
              "Must be compiled as strict ISO C23");
static_assert(sizeof(nullptr_t) == sizeof(void*),
              "C23 nullptr_t must match pointer width");
static_assert(sizeof(bool) == 1U, "C23 native bool must be 1 byte");
static_assert(systems_cache_line_size >= 64U,
              "Cache line size must be at least 64 bytes");
static_assert(sizeof(enum systems_os_family) == 1U,
              "systems_os_family must have 8-bit underlying type");
static_assert(sizeof(enum systems_arch_family) == 1U,
              "systems_arch_family must have 8-bit underlying type");
static_assert(sizeof(enum FoundError) == 4U,
              "FoundError must have 32-bit underlying type");
static_assert(sizeof(enum bitmap_flip_error) == 4U,
              "bitmap_flip_error must have 32-bit underlying type");
static_assert(sizeof(enum bounded_fifo_status) == 4U,
              "bounded_fifo_status must have 32-bit underlying type");
static_assert(sizeof(enum huffman_error) == 4U,
              "huffman_error must have 32-bit underlying type");
static_assert(sizeof(enum priority_scheduler_error) == 4U,
              "priority_scheduler_error must have 32-bit underlying type");
static_assert(sizeof(enum process_rendezvous_error) == 4U,
              "process_rendezvous_error must have 32-bit underlying type");
static_assert(sizeof(enum shm_pubsub_error) == 4U,
              "shm_pubsub_error must have 32-bit underlying type");
static_assert(sizeof(enum shm_read_mode) == 1U,
              "shm_read_mode must have 8-bit underlying type");
static_assert(HUFFMAN_MAX_SYMBOLS == 256U,
              "Huffman alphabet must cover all 256 byte values");
static_assert(HUFFMAN_MAX_CODE_LENGTH == 32U,
              "Huffman max code length must be 32 bits");
static_assert(BIT_PATTERN_BITS_PER_BYTE == 8U,
              "Bit pattern bits per byte must be 8");
static_assert(BIT_PATTERN_MAX_PATTERN_BITS == 1024U,
              "Bit pattern max pattern bits must be 1024");
static_assert(FOUND_PATTERN == 0, "FOUND_PATTERN must equal 0");
static_assert(PATTERN_NOT_FOUND == 1, "PATTERN_NOT_FOUND must equal 1");
static_assert(PRIORITY_SCHEDULER_MAX_TASKS == 64U,
              "Priority scheduler max tasks must be 64");
static_assert(PRIORITY_HIGHEST == 0U, "Highest priority must be 0");
static_assert(PRIORITY_LOWEST == 255U, "Lowest priority must be 255");

[[nodiscard]] static bool test_platform_c23() {
  const enum systems_os_family os = systems_get_os_family();
  const enum systems_arch_family arch = systems_get_arch_family();
  if (os == SYSTEMS_OS_OTHER || arch == SYSTEMS_ARCH_OTHER ||
      !systems_is_supported_target) {
    return false;
  }
  uint32_t sum = 0U;
  uint32_t diff = 0U;
  uint32_t prod = 0U;
  if (ckd_add(&sum, 100U, 200U) || sum != 300U ||
      !ckd_add(&sum, UINT32_MAX, 1U) || ckd_sub(&diff, 500U, 200U) ||
      diff != 300U || !ckd_sub(&diff, 0U, 1U) || ckd_mul(&prod, 16U, 32U) ||
      prod != 512U || !ckd_mul(&prod, UINT32_MAX, 2U)) {
    return false;
  }
  return (bool)(stdc_count_ones_uc(0xAAU) == 4U &&
                stdc_count_ones_ui(0xF0F0'0F0FU) == 16U &&
                stdc_count_ones_ull(0x8000'0000'0000'0001ULL) == 2U &&
                stdc_leading_zeros_uc(0U) == 8U &&
                stdc_leading_zeros_uc(0x10U) == 3U &&
                stdc_leading_zeros_ui(0U) == 32U &&
                stdc_leading_zeros_ull(0U) == 64U &&
                stdc_trailing_zeros_uc(0U) == 8U &&
                stdc_trailing_zeros_uc(0x08U) == 3U &&
                stdc_trailing_zeros_ui(0U) == 32U &&
                stdc_trailing_zeros_ull(0U) == 64U);
}

[[nodiscard]] static bool test_bounded_fifo_c23() {
  struct bounded_fifo fifo = {};
  uint32_t storage[4] = {};
  if (bounded_fifo_init(&fifo, storage, 4U, sizeof(uint32_t)) !=
      BOUNDED_FIFO_OK) {
    return false;
  }
  const uint32_t in_val = 0x1234'5678U;
  uint32_t out_val = 0U;
  bool ok = false;
  if (bounded_fifo_try_push(&fifo, &in_val) == BOUNDED_FIFO_OK &&
      bounded_fifo_size(&fifo) == 1U &&
      bounded_fifo_try_pop(&fifo, &out_val) == BOUNDED_FIFO_OK &&
      out_val == in_val && bounded_fifo_is_empty(&fifo)) {
    ok = true;
  }
  bounded_fifo_close(&fifo);
  bounded_fifo_destroy(&fifo);
  return ok;
}

[[nodiscard]] static bool test_bitmap_flip_c23() {
  uint8_t row[2] = {0x01U, 0x00U};
  if (bitmap_flip_aligned(row, sizeof(row), 16U, 1U) != BITMAP_FLIP_OK) {
    return false;
  }
  if (row[0] != 0x00U || row[1] != 0x80U ||
      bitmap_reverse_bits(0x01U) != 0x80U) {
    return false;
  }
  if (bitmap_flip_unaligned_optimized(row, sizeof(row), 16U, 1U) !=
      BITMAP_FLIP_OK) {
    return false;
  }
  if (row[0] != 0x01U || row[1] != 0x00U) {
    return false;
  }
  if (bitmap_flip_unaligned(row, sizeof(row), 16U, 1U) != BITMAP_FLIP_OK ||
      row[0] != 0x00U || row[1] != 0x80U) {
    return false;
  }
  if (bitmap_flip_strided(row, sizeof(row), 16U, 1U, 2U) != BITMAP_FLIP_OK ||
      row[0] != 0x01U || row[1] != 0x00U) {
    return false;
  }
  const struct bitmap_roi roi = {.x = 0U, .y = 0U, .width = 16U, .height = 1U};
  if (bitmap_flip_roi(row, sizeof(row), 16U, 1U, 2U, roi) != BITMAP_FLIP_OK ||
      row[0] != 0x00U || row[1] != 0x80U ||
      bitmap_flip_error_to_string(BITMAP_FLIP_OK) == nullptr) {
    return false;
  }
  return bitmap_reverse_bits_lookup(0x01U) == 0x80U;
}

[[nodiscard]] static bool test_process_rendezvous_and_shm_pubsub_c23() {
  char rdv_name[32] = {};
  char shm_name[32] = {};
  // NOLINTNEXTLINE(google-runtime-int): snprintf %ld matches long.
  const long pid = (long)getpid();
  (void)snprintf(rdv_name, sizeof(rdv_name), "/prvc23_%ld", pid);
  (void)snprintf(shm_name, sizeof(shm_name), "/shmc23_%ld", pid);
  (void)process_rendezvous_unlink(rdv_name);
  (void)shm_channel_unlink(shm_name);

  struct process_rendezvous rdv = {};
  if (process_rendezvous_create(&rdv, rdv_name) != PROCESS_RENDEZVOUS_OK) {
    return false;
  }
  bool rdv_ok = false;
  if (process_rendezvous_signal_producer_ready(&rdv) == PROCESS_RENDEZVOUS_OK &&
      process_rendezvous_producer_is_ready(&rdv)) {
    rdv_ok = true;
  }
  process_rendezvous_destroy(&rdv);
  (void)process_rendezvous_unlink(rdv_name);

  struct shm_channel ch = {};
  const struct shm_pubsub_config cfg = {.slot_count = 4U, .slot_size = 16U};
  if (shm_channel_create(&ch, shm_name, cfg) != SHM_PUBSUB_OK) {
    return false;
  }
  struct shm_publisher pub = {};
  struct shm_subscriber sub = {};
  bool shm_ok = false;
  if (rdv_ok && shm_publisher_attach(&pub, &ch) == SHM_PUBSUB_OK &&
      shm_subscriber_attach(&sub, &ch, SHM_READ_MODE_IN_ORDER) ==
          SHM_PUBSUB_OK) {
    const uint64_t payload = 0xCAFE'BABE'DEAD'BEEFULL;
    uint64_t seq = 0ULL;
    uint64_t recv_payload = 0ULL;
    struct shm_sample sample = {};
    if (shm_publisher_publish(&pub, &payload, sizeof(payload), &seq) ==
            SHM_PUBSUB_OK &&
        seq == 1ULL &&
        shm_subscriber_try_receive(&sub, &recv_payload, sizeof(recv_payload),
                                   &sample) == SHM_PUBSUB_OK &&
        recv_payload == payload && sample.sequence == 1ULL) {
      shm_ok = true;
    }
  }
  shm_subscriber_destroy(&sub);
  shm_publisher_destroy(&pub);
  shm_channel_destroy(&ch);
  (void)shm_channel_unlink(shm_name);
  return shm_ok;
}

[[nodiscard]] static bool test_huffman_tree_c23() {
  const struct huffman_symbol_length entries[3] = {
      {.symbol = (uint8_t)'b', .length = 2U},
      {.symbol = (uint8_t)'e', .length = 1U},
      {.symbol = (uint8_t)'z', .length = 2U},
  };
  struct huffman_tree tree = {};
  struct huffman_tree opt_tree = {};
  if (huffman_tree_build(&tree, entries, 3U) != HUFFMAN_OK ||
      huffman_tree_build_optimized(&opt_tree, entries, 3U) != HUFFMAN_OK) {
    return false;
  }
  bool is_full = false;
  bool opt_is_full = false;
  if (huffman_node_is_full(tree.root, &is_full) != HUFFMAN_OK || !is_full ||
      huffman_node_is_full(opt_tree.root, &opt_is_full) != HUFFMAN_OK ||
      !opt_is_full) {
    return false;
  }
  const uint8_t bitstream[1] = {0x58U};
  uint8_t decoded[4] = {};
  size_t written = 0U;
  if (huffman_tree_decode(&opt_tree, bitstream, 6U, decoded, sizeof(decoded),
                          &written) != HUFFMAN_OK) {
    return false;
  }
  return (bool)(written == 4U && decoded[0] == (uint8_t)'e' &&
                decoded[1] == (uint8_t)'b' && decoded[2] == (uint8_t)'z' &&
                decoded[3] == (uint8_t)'e');
}

[[nodiscard]] static bool test_bit_pattern_c23() {
  const uint8_t data[2] = {0xAAU, 0x55U};
  const uint8_t pattern[1] = {0xAAU};
  const enum FoundError res = found(data, 16U, pattern, 8U);
  const char* const msg = found_error_to_string(res);
  const uint8_t unaligned_data[2] = {0x35U, 0x80U};
  const uint8_t unaligned_pat[1] = {0xACU};
  return (bool)(msg != nullptr && res == FOUND_PATTERN &&
                bit_pattern_find_byte_aligned(data, 16U, pattern, 8U) ==
                    FOUND_PATTERN &&
                bit_pattern_find_naive(data, 16U, pattern, 8U) ==
                    FOUND_PATTERN &&
                bit_pattern_find_kmp(unaligned_data, 10U, unaligned_pat, 6U) ==
                    FOUND_PATTERN);
}

static void record_c23_task_tick(void* const user_data, const uint64_t now_ns) {
  uint64_t* const last_tick_ns = (uint64_t*)user_data;
  *last_tick_ns = now_ns;
}

[[nodiscard]] static bool test_priority_scheduler_c23() {
  struct priority_queue pq = {};
  if (priority_queue_init(&pq) != PRIORITY_SCHEDULER_OK) {
    return false;
  }
  const struct scheduled_task pq_entry = {
      .next_deadline_ns = 5'000ULL,
      .period_ns = 5'000ULL,
      .sequence_id = 1ULL,
      .executions = 0ULL,
      .deadline_misses = 0ULL,
      .task_id = 1U,
      .priority = PRIORITY_HIGHEST,
      .callback = record_c23_task_tick,
      .user_data = nullptr,
  };
  struct scheduled_task popped = {};
  if (!priority_queue_is_empty(&pq) ||
      priority_queue_push(&pq, &pq_entry) != PRIORITY_SCHEDULER_OK ||
      priority_queue_size(&pq) != 1U ||
      priority_queue_pop(&pq, &popped) != PRIORITY_SCHEDULER_OK ||
      popped.next_deadline_ns != 5'000ULL) {
    return false;
  }

  struct priority_scheduler sched = {};
  if (priority_scheduler_init(&sched) != PRIORITY_SCHEDULER_OK) {
    return false;
  }
  uint64_t ran_at_ns = 0ULL;
  const struct priority_task_config cfg = {
      .task_id = 1U,
      .priority = PRIORITY_HIGHEST,
      .period_ns = 10'000'000ULL,
      .first_deadline_ns = 10'000'000ULL,
      .callback = record_c23_task_tick,
      .user_data = &ran_at_ns,
  };
  struct priority_dispatch_result res = {};
  uint64_t next_dl = 0ULL;
  uint32_t next_id = 0U;
  const char* const ok_str =
      priority_scheduler_error_to_string(PRIORITY_SCHEDULER_OK);
  const bool ok =
      (bool)(ok_str != nullptr &&
             priority_scheduler_schedule(&sched, &cfg) ==
                 PRIORITY_SCHEDULER_OK &&
             priority_scheduler_step(&sched, 10'000'000ULL, &res) ==
                 PRIORITY_SCHEDULER_OK &&
             ran_at_ns == 10'000'000ULL && res.task_id == 1U &&
             !res.missed_deadline && res.next_deadline_ns == 20'000'000ULL &&
             priority_scheduler_peek_next_deadline(
                 &sched, &next_dl, &next_id) == PRIORITY_SCHEDULER_OK &&
             next_dl == 20'000'000ULL && next_id == 1U &&
             priority_scheduler_cancel(&sched, 1U) == PRIORITY_SCHEDULER_OK &&
             priority_scheduler_size(&sched) == 0U);
  priority_scheduler_destroy(&sched);
  return ok;
}

struct c23_test_case {
  const char* name;
  bool (*fn)();
};

static const struct c23_test_case k_c23_tests[] = {
    {.name = "platform", .fn = test_platform_c23},
    {.name = "bounded_fifo", .fn = test_bounded_fifo_c23},
    {.name = "bitmap_flip", .fn = test_bitmap_flip_c23},
    {.name = "huffman_tree", .fn = test_huffman_tree_c23},
    {.name = "bit_pattern", .fn = test_bit_pattern_c23},
    {.name = "priority_scheduler", .fn = test_priority_scheduler_c23},
    {.name = "process_rendezvous_and_shm_pubsub",
     .fn = test_process_rendezvous_and_shm_pubsub_c23},
};

int main(int argc, char* argv[]) {
  constexpr size_t k_test_count = sizeof(k_c23_tests) / sizeof(k_c23_tests[0]);
  if (argc == 1) {
    for (size_t i = 0U; i < k_test_count; ++i) {
      if (!k_c23_tests[i].fn()) {
        return 1;
      }
    }
    return 0;
  }

  if (argc == 2 && argv != nullptr && strcmp(argv[1], "--list-tests") == 0) {
    for (size_t i = 0U; i < k_test_count; ++i) {
      if (printf("%s\n", k_c23_tests[i].name) < 0) {
        return 1;
      }
    }
    return 0;
  }

  if (argc == 3 && argv != nullptr && strcmp(argv[1], "--run-test") == 0) {
    const char* const target_name = argv[2];
    for (size_t i = 0U; i < k_test_count; ++i) {
      if (strcmp(k_c23_tests[i].name, target_name) == 0) {
        return k_c23_tests[i].fn() ? 0 : 1;
      }
    }
    (void)fprintf(stderr, "Unknown C23 test case: %s\n", target_name);
    return 2;
  }

  const char* const prog = (argc > 0 && argv != nullptr && argv[0] != nullptr)
                               ? argv[0]
                               : "systems_c23_test";
  (void)fprintf(stderr, "Usage: %s [--list-tests | --run-test <name>]\n", prog);
  return 2;
}
