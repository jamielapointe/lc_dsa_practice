/// @file shm_pubsub_producer.cpp
/// @brief Standalone camera-frame producer process for the `shm_pubsub`
/// multi-process example.
///
/// Coordinates startup with the consumer process via
/// `systems::ProcessRendezvous`, waits until the consumer has attached its
/// `ShmSubscriber` (`ShmReadMode::kLatest`), and then publishes a finite number
/// of synthetic 32-bit monochrome camera frames with periodic sleeps and no
/// further inter-process coordination during the main loop.

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

#include "camera_frame.hpp"
#include "systems/process_rendezvous.hpp"
#include "systems/shm_pubsub.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using systems::ProcessRendezvous;
using systems::ShmChannel;
using systems::ShmPublisher;
using systems::ShmPubSubConfig;
using systems::examples::CameraFramePayload;

struct ProducerOptions {
  std::string_view channel_name{"/shmps_cam_demo"};
  std::string_view sync_name{"/shmps_syn_demo"};
  uint32_t frames{20};
  uint32_t slots{4};
  uint32_t sleep_ms{25};
  uint32_t seed{0xC0FF'EE01U};
  uint32_t timeout_ms{10000};
};

[[nodiscard]] auto parse_u32(std::string_view text) noexcept
    -> std::optional<uint32_t> {
  if (text.empty()) {
    return std::nullopt;
  }
  int base = 10;
  if (text.size() > 2uz && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2uz);
    base = 16;
  }
  uint32_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value, base);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] auto parse_args(std::span<char*> args) noexcept
    -> std::optional<ProducerOptions> {
  ProducerOptions opts{};
  for (size_t i = 1uz; i < args.size(); ++i) {
    const std::string_view flag{args[i]};
    auto next_arg = [&] -> std::optional<std::string_view> {
      if (i + 1uz >= args.size()) {
        return std::nullopt;
      }
      ++i;
      return std::string_view{args[i]};
    };

    if (flag == "--channel") {
      const auto val = next_arg();
      if (!val) {
        return std::nullopt;
      }
      opts.channel_name = *val;
    } else if (flag == "--sync") {
      const auto val = next_arg();
      if (!val) {
        return std::nullopt;
      }
      opts.sync_name = *val;
    } else if (flag == "--frames") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed || *parsed == 0) {
        return std::nullopt;
      }
      opts.frames = *parsed;
    } else if (flag == "--slots") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed) {
        return std::nullopt;
      }
      opts.slots = *parsed;
    } else if (flag == "--sleep-ms") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed) {
        return std::nullopt;
      }
      opts.sleep_ms = *parsed;
    } else if (flag == "--seed") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed) {
        return std::nullopt;
      }
      opts.seed = *parsed;
    } else if (flag == "--timeout-ms") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed || *parsed == 0) {
        return std::nullopt;
      }
      opts.timeout_ms = *parsed;
    } else {
      return std::nullopt;
    }
  }
  return opts;
}

auto run_producer(const ProducerOptions& opts) -> int {
  const auto timeout = std::chrono::milliseconds{opts.timeout_ms};

  // Remove any leftover channel segment from a crashed previous run before
  // creating a fresh ring buffer.
  static_cast<void>(ShmChannel::unlink(opts.channel_name));

  const ShmPubSubConfig config{
      .slot_count = opts.slots,
      .slot_size = static_cast<uint32_t>(sizeof(CameraFramePayload)),
  };
  auto channel = ShmChannel::create(opts.channel_name, config);
  if (!channel) {
    std::println(stderr,
                 "[PRODUCER] event=error stage=channel_create reason=\"{}\"",
                 systems::to_string(channel.error()));
    return 1;
  }

  auto publisher = ShmPublisher::attach(*channel);
  if (!publisher) {
    std::println(stderr,
                 "[PRODUCER] event=error stage=publisher_attach reason=\"{}\"",
                 systems::to_string(publisher.error()));
    return 1;
  }

  auto rendezvous = ProcessRendezvous::attach(opts.sync_name, timeout);
  if (!rendezvous) {
    std::println(stderr,
                 "[PRODUCER] event=error stage=rendezvous_attach reason=\"{}\"",
                 systems::to_string(rendezvous.error()));
    return 1;
  }

  std::println(
      "[PRODUCER] event=channel_created channel={} slots={} slot_size={}",
      opts.channel_name, config.slot_count, config.slot_size);
  static_cast<void>(std::fflush(stdout));

  if (auto sig = rendezvous->signal_producer_ready(); !sig) {
    std::println(
        stderr,
        "[PRODUCER] event=error stage=signal_producer_ready reason=\"{}\"",
        systems::to_string(sig.error()));
    return 1;
  }

  std::println("[PRODUCER] event=waiting_for_consumer sync={}", opts.sync_name);
  static_cast<void>(std::fflush(stdout));

  if (auto wait_res = rendezvous->wait_consumer_ready_for(timeout); !wait_res) {
    std::println(
        stderr,
        "[PRODUCER] event=error stage=wait_consumer_ready reason=\"{}\"",
        systems::to_string(wait_res.error()));
    return 1;
  }

  std::println("[PRODUCER] event=consumer_ready_starting_loop total_frames={}",
               opts.frames);
  static_cast<void>(std::fflush(stdout));

  // Main sensor production loop: zero inter-process coordination here.
  std::mt19937 rng{opts.seed};
  CameraFramePayload frame{};
  for (uint32_t idx = 1; idx <= opts.frames; ++idx) {
    frame.frame_id = idx;
    frame.timestamp_ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
    frame.width = systems::examples::kCameraFrameWidth;
    frame.height = systems::examples::kCameraFrameHeight;
    for (uint32_t& pixel : frame.pixels) {
      pixel = static_cast<uint32_t>(rng());
    }
    frame.pixel_crc32 = systems::examples::compute_pixel_crc32(frame.pixels);

    const auto payload_bytes =
        std::as_bytes(std::span<const CameraFramePayload, 1uz>{&frame, 1uz});
    const auto published = publisher->publish(payload_bytes);
    if (!published) {
      std::println(
          stderr, "[PRODUCER] event=error stage=publish frame={} reason=\"{}\"",
          idx, systems::to_string(published.error()));
      return 1;
    }

    std::println(
        "[PRODUCER] event=frame_published frame={} seq={} size={} "
        "crc32=0x{:08x}",
        idx, *published, payload_bytes.size(), frame.pixel_crc32);
    static_cast<void>(std::fflush(stdout));

    std::this_thread::sleep_for(std::chrono::milliseconds{opts.sleep_ms});
  }

  channel->close();
  rendezvous->close();
  std::println("[PRODUCER] event=shutdown published_count={}", opts.frames);
  static_cast<void>(std::fflush(stdout));
  return 0;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  const std::span<char*> args{argv, static_cast<size_t>(argc)};
  const auto opts = parse_args(args);
  if (!opts) {
    std::println(stderr,
                 "Usage: shm_pubsub_producer [--channel /name] [--sync /name] "
                 "[--frames N] [--slots N] [--sleep-ms MS] [--seed SEED] "
                 "[--timeout-ms MS]");
    return 2;
  }
  return run_producer(*opts);
}
