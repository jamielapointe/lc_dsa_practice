/// @file systems_c23_test.c
/// @brief Pure ISO C23 (-std=c23) CTest executable verifying that all public C
/// headers in src/include/systems/*.h compile and execute cleanly in a native
/// C23 translation unit without C++ compiler leakage.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "systems/bitmap_flip.h"
#include "systems/bounded_fifo.h"
#include "systems/platform.h"
#include "systems/process_rendezvous.h"
#include "systems/shm_pubsub.h"

static_assert(__STDC_VERSION__ == 202311L,
              "Must be compiled as strict ISO C23");
static_assert(systems_cache_line_size >= 64U,
              "Cache line size must be at least 64 bytes");

[[nodiscard]] static bool test_platform_c23() {
  const enum systems_os_family os = systems_get_os_family();
  const enum systems_arch_family arch = systems_get_arch_family();
  if (os == SYSTEMS_OS_OTHER || arch == SYSTEMS_ARCH_OTHER ||
      !systems_is_supported_target) {
    return false;
  }
  return stdc_count_ones_uc(0xAAU) == 4U;
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

int main() {
  if (!test_platform_c23() || !test_bounded_fifo_c23() ||
      !test_bitmap_flip_c23() ||
      !test_process_rendezvous_and_shm_pubsub_c23()) {
    return 1;
  }
  return 0;
}
