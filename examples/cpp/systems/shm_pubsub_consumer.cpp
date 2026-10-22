/// @file shm_pubsub_consumer.cpp
/// @brief Standalone camera-frame consumer process for the `shm_pubsub`
/// multi-process example.
///
/// Starts waiting on `systems::ProcessRendezvous`, opens the `ShmChannel` once
/// created by the producer, attaches a `ShmSubscriber` in
/// `ShmReadMode::kLatest` ("latest data only"), signals readiness so the
/// producer starts publishing, and then consumes frames with variable sleep so
/// it periodically falls 1–2 frames behind while verifying every received
/// frame's IEEE 802.3 CRC-32 checksum.

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>

#include "camera_frame.hpp"
#include "systems/process_rendezvous.hpp"
#include "systems/shm_pubsub.hpp"

namespace {

using systems::ProcessRendezvous;
using systems::ShmChannel;
using systems::ShmPubSubError;
using systems::ShmReadMode;
using systems::ShmSubscriber;
using systems::examples::CameraFramePayload;

struct ConsumerOptions {
  std::string_view channel_name{"/shmps_cam_demo"};
  std::string_view sync_name{"/shmps_syn_demo"};
  std::uint32_t base_sleep_ms{28};
  std::uint32_t lag_sleep_ms{65};
  std::uint32_t timeout_ms{10000};
  bool cleanup{false};
};

[[nodiscard]] auto parse_u32(std::string_view text) noexcept
    -> std::optional<std::uint32_t> {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint32_t value = 0;
  const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value, 10);
  if (ec != std::errc{} || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] auto parse_args(std::span<char*> args) noexcept
    -> std::optional<ConsumerOptions> {
  ConsumerOptions opts{};
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string_view flag{args[i]};
    auto next_arg = [&] -> std::optional<std::string_view> {
      if (i + 1 >= args.size()) {
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
    } else if (flag == "--base-sleep-ms") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed) {
        return std::nullopt;
      }
      opts.base_sleep_ms = *parsed;
    } else if (flag == "--lag-sleep-ms") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed) {
        return std::nullopt;
      }
      opts.lag_sleep_ms = *parsed;
    } else if (flag == "--timeout-ms") {
      const auto val = next_arg();
      const auto parsed = val ? parse_u32(*val) : std::nullopt;
      if (!parsed || *parsed == 0) {
        return std::nullopt;
      }
      opts.timeout_ms = *parsed;
    } else if (flag == "--cleanup") {
      opts.cleanup = true;
    } else {
      return std::nullopt;
    }
  }
  return opts;
}

/// @brief Variable consumer sleep schedule: mostly `base_sleep_ms`, but every
/// few frames pauses for `lag_sleep_ms` so the consumer falls 1–2 frames
/// behind the producer.
[[nodiscard]] constexpr auto consumer_delay_ms(
    const ConsumerOptions& opts, std::uint64_t receive_index) noexcept
    -> std::uint32_t {
  constexpr std::array<std::uint32_t, 5> kJitterMs{0U, 4U, 8U, 2U, 6U};
  const std::uint32_t jitter = kJitterMs[receive_index % kJitterMs.size()];
  if (receive_index % 3U == 2U) {
    return opts.lag_sleep_ms + jitter;
  }
  return opts.base_sleep_ms + jitter;
}

auto run_consumer(const ConsumerOptions& opts) -> int {
  const auto timeout = std::chrono::milliseconds{opts.timeout_ms};

  auto rendezvous = ProcessRendezvous::attach(opts.sync_name, timeout);
  if (!rendezvous) {
    std::cerr << "[CONSUMER] event=error stage=rendezvous_attach reason=\""
              << systems::to_string(rendezvous.error()) << "\"\n";
    return 1;
  }

  std::cout << "[CONSUMER] event=init_waiting_for_channel channel="
            << opts.channel_name << " sync=" << opts.sync_name << '\n'
            << std::flush;

  if (auto wait_prod = rendezvous->wait_producer_ready_for(timeout);
      !wait_prod) {
    std::cerr << "[CONSUMER] event=error stage=wait_producer_ready reason=\""
              << systems::to_string(wait_prod.error()) << "\"\n";
    return 1;
  }

  auto channel = ShmChannel::open(opts.channel_name);
  if (!channel) {
    std::cerr << "[CONSUMER] event=error stage=channel_open reason=\""
              << systems::to_string(channel.error()) << "\"\n";
    return 1;
  }

  auto subscriber = ShmSubscriber::attach(*channel, ShmReadMode::kLatest);
  if (!subscriber) {
    std::cerr << "[CONSUMER] event=error stage=subscriber_attach reason=\""
              << systems::to_string(subscriber.error()) << "\"\n";
    return 1;
  }

  const auto cfg = channel->config();
  std::cout << "[CONSUMER] event=ready_and_waiting mode=kLatest slots="
            << cfg.slot_count << " slot_size=" << cfg.slot_size << '\n'
            << std::flush;

  if (auto sig = rendezvous->signal_consumer_ready(); !sig) {
    std::cerr << "[CONSUMER] event=error stage=signal_consumer_ready reason=\""
              << systems::to_string(sig.error()) << "\"\n";
    return 1;
  }

  // Main consumption loop: zero coordination with producer; reads latest slot
  // via lock-free seqlock until channel is closed and drained.
  CameraFramePayload frame{};
  auto frame_bytes =
      std::as_writable_bytes(std::span<CameraFramePayload, 1>{&frame, 1});
  std::uint64_t received_count = 0;
  std::uint64_t missed_total = 0;
  std::uint64_t crc_errors = 0;

  for (;;) {
    const auto sample = subscriber->receive_for(frame_bytes, timeout);
    if (!sample) {
      if (sample.error() == ShmPubSubError::kClosed) {
        break;
      }
      std::cerr << "[CONSUMER] event=error stage=receive reason=\""
                << systems::to_string(sample.error()) << "\"\n";
      return 1;
    }

    ++received_count;
    missed_total += sample->missed;

    const std::uint32_t computed_crc =
        systems::examples::compute_pixel_crc32(frame.pixels);
    const bool crc_ok = (sample->size == sizeof(CameraFramePayload)) &&
                        (sample->sequence == frame.frame_id) &&
                        (computed_crc == frame.pixel_crc32);
    if (!crc_ok) {
      ++crc_errors;
    }

    std::cout << "[CONSUMER] event=frame_received seq=" << sample->sequence
              << " missed=" << sample->missed << " size=" << sample->size
              << " header_crc32=0x" << std::hex << std::setw(8)
              << std::setfill('0') << frame.pixel_crc32 << " computed_crc32=0x"
              << std::setw(8) << std::setfill('0') << computed_crc << std::dec
              << " crc_ok=" << (crc_ok ? "true" : "false") << '\n'
              << std::flush;

    const std::uint32_t delay_ms = consumer_delay_ms(opts, received_count);
    std::this_thread::sleep_for(std::chrono::milliseconds{delay_ms});
  }

  if (opts.cleanup) {
    static_cast<void>(ShmChannel::unlink(opts.channel_name));
    static_cast<void>(ProcessRendezvous::unlink(opts.sync_name));
  }

  std::cout << "[CONSUMER] event=shutdown received_count=" << received_count
            << " missed_total=" << missed_total << " crc_errors=" << crc_errors
            << '\n'
            << std::flush;

  return (received_count > 0 && crc_errors == 0) ? 0 : 1;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  const std::span<char*> args{argv, static_cast<std::size_t>(argc)};
  const auto opts = parse_args(args);
  if (!opts) {
    std::cerr << "Usage: shm_pubsub_consumer [--channel /name] [--sync /name] "
                 "[--base-sleep-ms MS] [--lag-sleep-ms MS] [--timeout-ms MS] "
                 "[--cleanup]\n";
    return 2;
  }
  return run_consumer(*opts);
}
