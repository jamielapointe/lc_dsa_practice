/// @file shm_pubsub_producer.c
/// @brief Standalone camera-frame producer process for the ISO C23 shm_pubsub
/// multi-process example.

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "camera_frame.h"
#include "systems/process_rendezvous.h"
#include "systems/shm_pubsub.h"

struct producer_options {
  const char* channel_name;
  const char* sync_name;
  uint32_t frames;
  uint32_t slots;
  uint32_t sleep_ms;
  uint32_t seed;
  uint32_t timeout_ms;
};

[[nodiscard]] static bool parse_u32(const char* text, uint32_t* out) {
  if (text == nullptr || text[0] == '\0' || text[0] == '-') {
    return false;
  }
  errno = 0;
  char* end = nullptr;
  // NOLINTNEXTLINE(google-runtime-int): strtoul returns unsigned long.
  const unsigned long val = strtoul(text, &end, 0);
  if (errno != 0 || end == text || *end != '\0' ||
      val > (unsigned long)UINT32_MAX) {
    return false;
  }
  *out = (uint32_t)val;
  return true;
}

[[nodiscard]] static bool parse_args(int argc, char** argv,
                                     struct producer_options* opts) {
  *opts = (struct producer_options){
      .channel_name = "/shmps_cam_demo",
      .sync_name = "/shmps_syn_demo",
      .frames = 20U,
      .slots = 4U,
      .sleep_ms = 25U,
      .seed = 0xC0FF'EE01U,
      .timeout_ms = 10000U,
  };

  for (int i = 1; i < argc; ++i) {
    const char* flag = argv[i];
    if (i + 1 >= argc) {
      return false;
    }
    const char* val = argv[++i];
    if (strcmp(flag, "--channel") == 0) {
      opts->channel_name = val;
    } else if (strcmp(flag, "--sync") == 0) {
      opts->sync_name = val;
    } else if (strcmp(flag, "--frames") == 0) {
      if (!parse_u32(val, &opts->frames) || opts->frames == 0U) {
        return false;
      }
    } else if (strcmp(flag, "--slots") == 0) {
      if (!parse_u32(val, &opts->slots)) {
        return false;
      }
    } else if (strcmp(flag, "--sleep-ms") == 0) {
      if (!parse_u32(val, &opts->sleep_ms)) {
        return false;
      }
    } else if (strcmp(flag, "--seed") == 0) {
      if (!parse_u32(val, &opts->seed)) {
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

static uint64_t monotonic_now_ns() {
  struct timespec ts = {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ((uint64_t)ts.tv_sec * 1'000'000'000ULL) + (uint64_t)ts.tv_nsec;
}

static void sleep_ms(uint32_t ms) {
  const uint64_t total_ns = (uint64_t)ms * 1'000'000ULL;
  struct timespec req = {
      .tv_sec = (time_t)(total_ns / 1'000'000'000ULL),
      .tv_nsec = (long)(total_ns % 1'000'000'000ULL),
  };
  nanosleep(&req, nullptr);
}

static uint32_t xorshift32(uint32_t* state) {
  uint32_t x = *state;
  if (x == 0U) {
    x = 0xA5A5'5A5AU;
  }
  x ^= x << 13U;
  x ^= x >> 17U;
  x ^= x << 5U;
  *state = x;
  return x;
}

static int run_producer(const struct producer_options* opts) {
  const int64_t timeout_ns = (int64_t)opts->timeout_ms * 1'000'000LL;
  (void)shm_channel_unlink(opts->channel_name);

  const struct shm_pubsub_config config = {
      .slot_count = opts->slots,
      .slot_size = (uint32_t)sizeof(struct camera_frame_payload),
  };

  struct shm_channel channel = {};
  const enum shm_pubsub_error create_err =
      shm_channel_create(&channel, opts->channel_name, config);
  if (create_err != SHM_PUBSUB_OK) {
    (void)fprintf(stderr,
                  "[PRODUCER] event=error stage=channel_create reason=\"%s\"\n",
                  shm_pubsub_error_to_string(create_err));
    return 1;
  }

  struct shm_publisher publisher = {};
  const enum shm_pubsub_error attach_err =
      shm_publisher_attach(&publisher, &channel);
  if (attach_err != SHM_PUBSUB_OK) {
    (void)fprintf(
        stderr, "[PRODUCER] event=error stage=publisher_attach reason=\"%s\"\n",
        shm_pubsub_error_to_string(attach_err));
    shm_channel_destroy(&channel);
    return 1;
  }

  struct process_rendezvous rendezvous = {};
  const enum process_rendezvous_error rdv_err =
      process_rendezvous_attach(&rendezvous, opts->sync_name, timeout_ns);
  if (rdv_err != PROCESS_RENDEZVOUS_OK) {
    (void)fprintf(
        stderr,
        "[PRODUCER] event=error stage=rendezvous_attach reason=\"%s\"\n",
        process_rendezvous_error_to_string(rdv_err));
    shm_publisher_destroy(&publisher);
    shm_channel_destroy(&channel);
    return 1;
  }

  (void)printf("[PRODUCER] event=channel_created channel=%s slots=%" PRIu32
               " slot_size=%" PRIu32 "\n",
               opts->channel_name, config.slot_count, config.slot_size);
  (void)fflush(stdout);

  const enum process_rendezvous_error sig_err =
      process_rendezvous_signal_producer_ready(&rendezvous);
  if (sig_err != PROCESS_RENDEZVOUS_OK) {
    (void)fprintf(
        stderr,
        "[PRODUCER] event=error stage=signal_producer_ready reason=\"%s\"\n",
        process_rendezvous_error_to_string(sig_err));
    process_rendezvous_destroy(&rendezvous);
    shm_publisher_destroy(&publisher);
    shm_channel_destroy(&channel);
    return 1;
  }

  (void)printf("[PRODUCER] event=waiting_for_consumer sync=%s\n",
               opts->sync_name);
  (void)fflush(stdout);

  const enum process_rendezvous_error wait_err =
      process_rendezvous_wait_consumer_ready_for(&rendezvous, timeout_ns);
  if (wait_err != PROCESS_RENDEZVOUS_OK) {
    (void)fprintf(
        stderr,
        "[PRODUCER] event=error stage=wait_consumer_ready reason=\"%s\"\n",
        process_rendezvous_error_to_string(wait_err));
    process_rendezvous_destroy(&rendezvous);
    shm_publisher_destroy(&publisher);
    shm_channel_destroy(&channel);
    return 1;
  }

  (void)printf(
      "[PRODUCER] event=consumer_ready_starting_loop total_frames=%" PRIu32
      "\n",
      opts->frames);
  (void)fflush(stdout);

  uint32_t rng_state = opts->seed;
  static struct camera_frame_payload frame = {};
  for (uint32_t idx = 1U; idx <= opts->frames; ++idx) {
    frame.frame_id = idx;
    frame.timestamp_ns = monotonic_now_ns();
    frame.width = CAMERA_FRAME_WIDTH;
    frame.height = CAMERA_FRAME_HEIGHT;
    for (size_t px = 0U; px < CAMERA_PIXEL_COUNT; ++px) {
      frame.pixels[px] = xorshift32(&rng_state);
    }
    frame.pixel_crc32 =
        camera_compute_pixel_crc32(frame.pixels, CAMERA_PIXEL_COUNT);

    uint64_t seq = 0ULL;
    const enum shm_pubsub_error pub_err =
        shm_publisher_publish(&publisher, &frame, sizeof(frame), &seq);
    if (pub_err != SHM_PUBSUB_OK) {
      (void)fprintf(stderr,
                    "[PRODUCER] event=error stage=publish frame=%" PRIu32
                    " reason=\"%s\"\n",
                    idx, shm_pubsub_error_to_string(pub_err));
      process_rendezvous_destroy(&rendezvous);
      shm_publisher_destroy(&publisher);
      shm_channel_destroy(&channel);
      return 1;
    }

    (void)printf("[PRODUCER] event=frame_published frame=%" PRIu32
                 " seq=%" PRIu64 " size=%zu crc32=0x%08" PRIx32 "\n",
                 idx, seq, sizeof(frame), frame.pixel_crc32);
    (void)fflush(stdout);

    sleep_ms(opts->sleep_ms);
  }

  shm_channel_close(&channel);
  process_rendezvous_close(&rendezvous);
  (void)printf("[PRODUCER] event=shutdown published_count=%" PRIu32 "\n",
               opts->frames);
  (void)fflush(stdout);

  process_rendezvous_destroy(&rendezvous);
  shm_publisher_destroy(&publisher);
  shm_channel_destroy(&channel);
  return 0;
}

int main(int argc, char* argv[]) {
  struct producer_options opts = {};
  if (!parse_args(argc, argv, &opts)) {
    (void)fprintf(
        stderr,
        "Usage: shm_pubsub_producer_c [--channel /name] [--sync /name] "
        "[--frames N] [--slots N] [--sleep-ms MS] [--seed SEED] "
        "[--timeout-ms MS]\n");
    return 2;
  }
  return run_producer(&opts);
}
