/// @file shm_pubsub_consumer.c
/// @brief Standalone camera-frame consumer process for the ISO C23 shm_pubsub
/// multi-process example.

#include <errno.h>  // IWYU pragma: keep
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>  // IWYU pragma: keep

#include "camera_frame.h"
#include "systems/process_rendezvous.h"
#include "systems/shm_pubsub.h"

struct consumer_options {
  const char* channel_name;
  const char* sync_name;
  uint32_t base_sleep_ms;
  uint32_t lag_sleep_ms;
  uint32_t timeout_ms;
  bool cleanup;
};

[[nodiscard]] static bool parse_u32(const char* text, uint32_t* out) {
  if (text == nullptr || text[0] == '\0' || text[0] == '-') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  // NOLINTNEXTLINE(google-runtime-int): strtoul returns unsigned long.
  const unsigned long val = strtoul(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' ||
      val > (unsigned long)UINT32_MAX) {
    return false;
  }
  *out = (uint32_t)val;
  return true;
}

[[nodiscard]] static bool parse_args(int argc, char** argv,
                                     struct consumer_options* opts) {
  *opts = (struct consumer_options){
      .channel_name = "/shmps_cam_demo",
      .sync_name = "/shmps_syn_demo",
      .base_sleep_ms = 28U,
      .lag_sleep_ms = 65U,
      .timeout_ms = 10000U,
      .cleanup = false,
  };

  for (int i = 1; i < argc; ++i) {
    const char* flag = argv[i];
    if (strcmp(flag, "--cleanup") == 0) {
      opts->cleanup = true;
      continue;
    }
    if (i + 1 >= argc) {
      return false;
    }
    const char* val = argv[++i];
    if (strcmp(flag, "--channel") == 0) {
      opts->channel_name = val;
    } else if (strcmp(flag, "--sync") == 0) {
      opts->sync_name = val;
    } else if (strcmp(flag, "--base-sleep-ms") == 0) {
      if (!parse_u32(val, &opts->base_sleep_ms)) {
        return false;
      }
    } else if (strcmp(flag, "--lag-sleep-ms") == 0) {
      if (!parse_u32(val, &opts->lag_sleep_ms)) {
        return false;
      }
    } else if (strcmp(flag, "--timeout-ms") == 0) {
      if (!parse_u32(val, &opts->timeout_ms) || opts->timeout_ms == 0U) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

static uint32_t consumer_delay_ms(const struct consumer_options* opts,
                                  uint64_t receive_index) {
  static constexpr uint32_t kJitterMs[5] = {0U, 4U, 8U, 2U, 6U};
  const uint32_t jitter = kJitterMs[receive_index % 5ULL];
  if (receive_index % 3ULL == 2ULL) {
    return opts->lag_sleep_ms + jitter;
  }
  return opts->base_sleep_ms + jitter;
}

static void sleep_ms(uint32_t ms) {
  const uint64_t total_ns = (uint64_t)ms * 1'000'000ULL;
  struct timespec req = {
      .tv_sec = (time_t)(total_ns / 1'000'000'000ULL),
      .tv_nsec = (long)(total_ns % 1'000'000'000ULL),
  };
  nanosleep(&req, nullptr);
}

static int run_consumer(const struct consumer_options* opts) {
  const int64_t timeout_ns = (int64_t)opts->timeout_ms * 1'000'000LL;

  struct process_rendezvous rendezvous = {};
  const enum process_rendezvous_error rdv_err =
      process_rendezvous_attach(&rendezvous, opts->sync_name, timeout_ns);
  if (rdv_err != PROCESS_RENDEZVOUS_OK) {
    (void)fprintf(
        stderr,
        "[CONSUMER] event=error stage=rendezvous_attach reason=\"%s\"\n",
        process_rendezvous_error_to_string(rdv_err));
    return 1;
  }

  (void)printf("[CONSUMER] event=init_waiting_for_channel channel=%s sync=%s\n",
               opts->channel_name, opts->sync_name);
  (void)fflush(stdout);

  const enum process_rendezvous_error wait_prod_err =
      process_rendezvous_wait_producer_ready_for(&rendezvous, timeout_ns);
  if (wait_prod_err != PROCESS_RENDEZVOUS_OK) {
    (void)fprintf(
        stderr,
        "[CONSUMER] event=error stage=wait_producer_ready reason=\"%s\"\n",
        process_rendezvous_error_to_string(wait_prod_err));
    process_rendezvous_destroy(&rendezvous);
    return 1;
  }

  struct shm_channel channel = {};
  const enum shm_pubsub_error open_err =
      shm_channel_open(&channel, opts->channel_name);
  if (open_err != SHM_PUBSUB_OK) {
    (void)fprintf(stderr,
                  "[CONSUMER] event=error stage=channel_open reason=\"%s\"\n",
                  shm_pubsub_error_to_string(open_err));
    process_rendezvous_destroy(&rendezvous);
    return 1;
  }

  struct shm_subscriber subscriber = {};
  const enum shm_pubsub_error sub_err =
      shm_subscriber_attach(&subscriber, &channel, SHM_READ_MODE_LATEST);
  if (sub_err != SHM_PUBSUB_OK) {
    (void)fprintf(
        stderr,
        "[CONSUMER] event=error stage=subscriber_attach reason=\"%s\"\n",
        shm_pubsub_error_to_string(sub_err));
    shm_channel_destroy(&channel);
    process_rendezvous_destroy(&rendezvous);
    return 1;
  }

  const struct shm_pubsub_config cfg = shm_channel_config(&channel);
  (void)printf("[CONSUMER] event=ready_and_waiting mode=kLatest slots=%" PRIu32
               " slot_size=%" PRIu32 "\n",
               cfg.slot_count, cfg.slot_size);
  (void)fflush(stdout);

  const enum process_rendezvous_error sig_err =
      process_rendezvous_signal_consumer_ready(&rendezvous);
  if (sig_err != PROCESS_RENDEZVOUS_OK) {
    (void)fprintf(
        stderr,
        "[CONSUMER] event=error stage=signal_consumer_ready reason=\"%s\"\n",
        process_rendezvous_error_to_string(sig_err));
    shm_subscriber_destroy(&subscriber);
    shm_channel_destroy(&channel);
    process_rendezvous_destroy(&rendezvous);
    return 1;
  }

  static struct camera_frame_payload frame = {};
  uint64_t received_count = 0ULL;
  uint64_t missed_total = 0ULL;
  uint64_t crc_errors = 0ULL;

  for (;;) {
    struct shm_sample sample = {};
    const enum shm_pubsub_error recv_err = shm_subscriber_receive_for(
        &subscriber, &frame, sizeof(frame), timeout_ns, &sample);
    if (recv_err != SHM_PUBSUB_OK) {
      if (recv_err == SHM_PUBSUB_ERR_CLOSED) {
        break;
      }
      (void)fprintf(stderr,
                    "[CONSUMER] event=error stage=receive reason=\"%s\"\n",
                    shm_pubsub_error_to_string(recv_err));
      shm_subscriber_destroy(&subscriber);
      shm_channel_destroy(&channel);
      process_rendezvous_destroy(&rendezvous);
      return 1;
    }

    ++received_count;
    missed_total += sample.missed;

    const uint32_t computed_crc =
        camera_compute_pixel_crc32(frame.pixels, CAMERA_PIXEL_COUNT);
    bool crc_ok = false;
    if (sample.size == sizeof(frame) && sample.sequence == frame.frame_id &&
        computed_crc == frame.pixel_crc32) {
      crc_ok = true;
    }
    if (!crc_ok) {
      ++crc_errors;
    }

    (void)printf("[CONSUMER] event=frame_received seq=%" PRIu64
                 " missed=%" PRIu64 " size=%" PRIu32
                 " header_crc32=0x%08" PRIx32 " computed_crc32=0x%08" PRIx32
                 " crc_ok=%s\n",
                 sample.sequence, sample.missed, sample.size, frame.pixel_crc32,
                 computed_crc, crc_ok ? "true" : "false");
    (void)fflush(stdout);

    const uint32_t delay_ms = consumer_delay_ms(opts, received_count);
    sleep_ms(delay_ms);
  }

  if (opts->cleanup) {
    (void)shm_channel_unlink(opts->channel_name);
    (void)process_rendezvous_unlink(opts->sync_name);
  }

  (void)printf("[CONSUMER] event=shutdown received_count=%" PRIu64
               " missed_total=%" PRIu64 " crc_errors=%" PRIu64 "\n",
               received_count, missed_total, crc_errors);
  (void)fflush(stdout);

  shm_subscriber_destroy(&subscriber);
  shm_channel_destroy(&channel);
  process_rendezvous_destroy(&rendezvous);
  return (received_count > 0ULL && crc_errors == 0ULL) ? 0 : 1;
}

int main(int argc, char* argv[]) {
  struct consumer_options opts = {};
  if (!parse_args(argc, argv, &opts)) {
    (void)fprintf(
        stderr,
        "Usage: shm_pubsub_consumer_c [--channel /name] [--sync /name] "
        "[--base-sleep-ms MS] [--lag-sleep-ms MS] [--timeout-ms MS] "
        "[--cleanup]\n");
    return 2;
  }
  return run_consumer(&opts);
}
