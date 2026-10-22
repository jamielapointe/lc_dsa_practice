/**
 * @file shm_pubsub_c_test.cpp
 * @brief Google Test suite for ISO C23 shm_pubsub component.
 * Traceability: SHMPS-REQ-001 through SHMPS-REQ-020, SHMPS-REQ-023 through
 * SHMPS-REQ-027.
 */

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <latch>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <systems/platform.h>
#include <systems/shm_pubsub.h>
}

#include "systems_test/allocation_counter.hpp"
#include "systems_test/process.hpp"
#include "systems_test/shared_region.hpp"
#include "systems_test/shm_name.hpp"

namespace {

constexpr struct shm_pubsub_config kSmallConfig = {
    .slot_count = 8U,
    .slot_size = 64U,
};
constexpr auto kChildWait = std::chrono::seconds{15};

class ScopedChannel {
 public:
  ScopedChannel() = default;
  ~ScopedChannel() { shm_channel_destroy(&ch_); }
  ScopedChannel(const ScopedChannel&) = delete;
  auto operator=(const ScopedChannel&) -> ScopedChannel& = delete;
  ScopedChannel(ScopedChannel&& other) noexcept {
    ch_ = other.ch_;
    other.ch_ = {};
  }
  auto operator=(ScopedChannel&& other) noexcept -> ScopedChannel& {
    if (this != &other) {
      shm_channel_destroy(&ch_);
      ch_ = other.ch_;
      other.ch_ = {};
    }
    return *this;
  }

  [[nodiscard]] auto get() noexcept -> struct shm_channel* { return &ch_; }
  [[nodiscard]] auto get() const noexcept -> const struct shm_channel* {
    return &ch_;
  }

 private:
  struct shm_channel ch_{};
};

class ScopedPublisher {
 public:
  ScopedPublisher() = default;
  ~ScopedPublisher() { shm_publisher_destroy(&pub_); }
  ScopedPublisher(const ScopedPublisher&) = delete;
  auto operator=(const ScopedPublisher&) -> ScopedPublisher& = delete;
  ScopedPublisher(ScopedPublisher&&) = delete;
  auto operator=(ScopedPublisher&&) -> ScopedPublisher& = delete;

  [[nodiscard]] auto get() noexcept -> struct shm_publisher* { return &pub_; }

 private:
  struct shm_publisher pub_{};
};

class ScopedSubscriber {
 public:
  ScopedSubscriber() = default;
  ~ScopedSubscriber() { shm_subscriber_destroy(&sub_); }
  ScopedSubscriber(const ScopedSubscriber&) = delete;
  auto operator=(const ScopedSubscriber&) -> ScopedSubscriber& = delete;
  ScopedSubscriber(ScopedSubscriber&&) = delete;
  auto operator=(ScopedSubscriber&&) -> ScopedSubscriber& = delete;

  [[nodiscard]] auto get() noexcept -> struct shm_subscriber* { return &sub_; }

 private:
  struct shm_subscriber sub_{};
};

class TestChannelName {
 public:
  TestChannelName()
      : name_{systems_test::unique_shm_name("shmc")}, guard_{name_} {}
  [[nodiscard]] auto c_str() const -> const char* { return name_.c_str(); }
  [[nodiscard]] auto str() const -> const std::string& { return name_; }

 private:
  std::string name_;
  systems_test::ScopedShmUnlink guard_;
};

auto make_payload(std::uint64_t tag, std::size_t size)
    -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> bytes(size);
  std::array<std::uint8_t, sizeof(tag)> tag_bytes{};
  std::memcpy(tag_bytes.data(), &tag, sizeof(tag));
  for (std::size_t i = 0U; i < size; ++i) {
    bytes[i] = i < tag_bytes.size()
                   ? tag_bytes[i]
                   : static_cast<std::uint8_t>(((tag * 31ULL) + i) & 0xFFULL);
  }
  return bytes;
}

auto read_tag(const std::uint8_t* data, std::size_t size) -> std::uint64_t {
  std::uint64_t tag = 0ULL;
  std::memcpy(&tag, data, std::min(size, sizeof(tag)));
  return tag;
}

auto payload_is_consistent(const std::uint8_t* data, std::size_t size) -> bool {
  if (size < sizeof(std::uint64_t)) {
    return true;
  }
  const auto expected = make_payload(read_tag(data, size), size);
  return std::memcmp(data, expected.data(), size) == 0;
}

// ============================================================================
// Tier 1: Functional & Boundary
// ============================================================================

TEST(ShmPubSubCTest, CreateThenOpenAndErrorStrings) {
  RecordProperty("requirements", "SHMPS-REQ-001,SHMPS-REQ-003,SHMPS-REQ-004");
  const TestChannelName name;
  ScopedChannel created;
  ASSERT_EQ(shm_channel_create(created.get(), name.c_str(), kSmallConfig),
            SHM_PUBSUB_OK);
  const auto cfg = shm_channel_config(created.get());
  EXPECT_EQ(cfg.slot_count, kSmallConfig.slot_count);
  EXPECT_EQ(cfg.slot_size, kSmallConfig.slot_size);
  EXPECT_FALSE(shm_channel_is_closed(created.get()));

  ScopedChannel duplicate;
  EXPECT_EQ(shm_channel_create(duplicate.get(), name.c_str(), kSmallConfig),
            SHM_PUBSUB_ERR_ALREADY_EXISTS);

  ScopedChannel opened;
  ASSERT_EQ(shm_channel_open(opened.get(), name.c_str()), SHM_PUBSUB_OK);
  const auto opened_cfg = shm_channel_config(opened.get());
  EXPECT_EQ(opened_cfg.slot_count, kSmallConfig.slot_count);
  EXPECT_EQ(opened_cfg.slot_size, kSmallConfig.slot_size);

  for (int code = SHM_PUBSUB_OK; code <= SHM_PUBSUB_ERR_OS_ERROR + 1; ++code) {
    const char* text =
        shm_pubsub_error_to_string(static_cast<enum shm_pubsub_error>(code));
    ASSERT_NE(text, nullptr);
    EXPECT_GT(std::strlen(text), 0U);
  }
}

TEST(ShmPubSubCTest, ValidateConfigAndChannelNames) {
  RecordProperty("requirements", "SHMPS-REQ-001,SHMPS-REQ-002");
  const std::vector<struct shm_pubsub_config> bad_configs = {
      {.slot_count = 0U, .slot_size = 64U},
      {.slot_count = 1U, .slot_size = 64U},
      {.slot_count = 3U, .slot_size = 64U},
      {.slot_count = SHM_MAX_SLOT_COUNT * 2U, .slot_size = 64U},
      {.slot_count = 8U, .slot_size = 0U},
      {.slot_count = 8U, .slot_size = SHM_MAX_SLOT_SIZE + 1U},
  };
  for (const auto& bad : bad_configs) {
    const TestChannelName name;
    ScopedChannel ch;
    EXPECT_EQ(shm_channel_create(ch.get(), name.c_str(), bad),
              SHM_PUBSUB_ERR_INVALID_ARGUMENT);
  }

  ScopedChannel ch;
  EXPECT_EQ(shm_channel_create(nullptr, "/valid", kSmallConfig),
            SHM_PUBSUB_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(shm_channel_create(ch.get(), nullptr, kSmallConfig),
            SHM_PUBSUB_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(shm_channel_create(ch.get(), "", kSmallConfig),
            SHM_PUBSUB_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(shm_channel_create(ch.get(), "/", kSmallConfig),
            SHM_PUBSUB_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(shm_channel_create(ch.get(), "noslash", kSmallConfig),
            SHM_PUBSUB_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(shm_channel_create(ch.get(), "/two/slashes", kSmallConfig),
            SHM_PUBSUB_ERR_INVALID_ARGUMENT);

  const std::string too_long =
      "/" + std::string(SYSTEMS_MAX_SHM_NAME_LENGTH, 'x');
  EXPECT_EQ(shm_channel_create(ch.get(), too_long.c_str(), kSmallConfig),
            SHM_PUBSUB_ERR_NAME_TOO_LONG);
  EXPECT_EQ(shm_channel_open(ch.get(), too_long.c_str()),
            SHM_PUBSUB_ERR_NAME_TOO_LONG);
  EXPECT_EQ(shm_channel_unlink(too_long.c_str()), SHM_PUBSUB_ERR_NAME_TOO_LONG);
}

TEST(ShmPubSubCTest, OpenRejectsMissingOrIncompatibleSegmentsAndUnlinkWorks) {
  RecordProperty("requirements", "SHMPS-REQ-004,SHMPS-REQ-005,SHMPS-REQ-006");
  const TestChannelName missing;
  ScopedChannel ch;
  EXPECT_EQ(shm_channel_open(ch.get(), missing.c_str()),
            SHM_PUBSUB_ERR_NOT_FOUND);
  EXPECT_EQ(shm_channel_unlink(missing.c_str()), SHM_PUBSUB_ERR_NOT_FOUND);

  // Create a raw small segment that is not a valid channel
  const TestChannelName corrupt;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): Darwin shm_open vararg.
  const int fd = shm_open(corrupt.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(ftruncate(fd, 16), 0);
  (void)close(fd);
  EXPECT_EQ(shm_channel_open(ch.get(), corrupt.c_str()),
            SHM_PUBSUB_ERR_LAYOUT_MISMATCH);
  EXPECT_EQ(shm_channel_unlink(corrupt.c_str()), SHM_PUBSUB_OK);

  // Unlink keeps existing mapping alive
  const TestChannelName live_name;
  ScopedChannel live;
  ASSERT_EQ(shm_channel_create(live.get(), live_name.c_str(), kSmallConfig),
            SHM_PUBSUB_OK);
  EXPECT_EQ(shm_channel_unlink(live_name.c_str()), SHM_PUBSUB_OK);
  ScopedChannel late;
  EXPECT_EQ(shm_channel_open(late.get(), live_name.c_str()),
            SHM_PUBSUB_ERR_NOT_FOUND);

  ScopedPublisher pub;
  ASSERT_EQ(shm_publisher_attach(pub.get(), live.get()), SHM_PUBSUB_OK);
  std::uint64_t seq = 0ULL;
  const std::uint32_t val = 42U;
  EXPECT_EQ(shm_publisher_publish(pub.get(), &val, sizeof(val), &seq),
            SHM_PUBSUB_OK);
  EXPECT_EQ(seq, 1ULL);
}

TEST(ShmPubSubCTest, SinglePublisherAndInOrderAndLatestSubscribers) {
  RecordProperty("requirements", "SHMPS-REQ-007,SHMPS-REQ-008,SHMPS-REQ-009");
  RecordProperty("requirements", "SHMPS-REQ-010,SHMPS-REQ-011,SHMPS-REQ-012");
  RecordProperty("requirements", "SHMPS-REQ-017,SHMPS-REQ-019,SHMPS-REQ-027");
  const TestChannelName name;
  ScopedChannel ch;
  const struct shm_pubsub_config cfg = {.slot_count = 4U, .slot_size = 32U};
  ASSERT_EQ(shm_channel_create(ch.get(), name.c_str(), cfg), SHM_PUBSUB_OK);

  ScopedPublisher pub1;
  ASSERT_EQ(shm_publisher_attach(pub1.get(), ch.get()), SHM_PUBSUB_OK);

  ScopedPublisher pub2;
  EXPECT_EQ(shm_publisher_attach(pub2.get(), ch.get()),
            SHM_PUBSUB_ERR_PUBLISHER_EXISTS);

  // Release pub1 and re-attach pub2
  shm_publisher_destroy(pub1.get());
  ASSERT_EQ(shm_publisher_attach(pub2.get(), ch.get()), SHM_PUBSUB_OK);

  ScopedSubscriber sub_in_order;
  ScopedSubscriber sub_latest;
  ASSERT_EQ(shm_subscriber_attach(sub_in_order.get(), ch.get(),
                                  SHM_READ_MODE_IN_ORDER),
            SHM_PUBSUB_OK);
  ASSERT_EQ(
      shm_subscriber_attach(sub_latest.get(), ch.get(), SHM_READ_MODE_LATEST),
      SHM_PUBSUB_OK);

  std::array<std::uint8_t, 32> buf{};
  struct shm_sample sample{};
  EXPECT_EQ(shm_subscriber_try_receive(sub_in_order.get(), buf.data(),
                                       buf.size(), &sample),
            SHM_PUBSUB_ERR_WOULD_BLOCK);

  // Oversized payload rejected
  std::array<std::uint8_t, 64> oversize{};
  std::uint64_t seq = 0ULL;
  EXPECT_EQ(
      shm_publisher_publish(pub2.get(), oversize.data(), oversize.size(), &seq),
      SHM_PUBSUB_ERR_PAYLOAD_TOO_LARGE);

  // Publish 3 samples (within ring capacity 4)
  for (std::uint64_t i = 1ULL; i <= 3ULL; ++i) {
    const auto payload = make_payload(i, 16U);
    ASSERT_EQ(
        shm_publisher_publish(pub2.get(), payload.data(), payload.size(), &seq),
        SHM_PUBSUB_OK);
    EXPECT_EQ(seq, i);
  }

  // Buffer too small does not consume sample
  EXPECT_EQ(
      shm_subscriber_try_receive(sub_in_order.get(), buf.data(), 4U, &sample),
      SHM_PUBSUB_ERR_BUFFER_TOO_SMALL);

  // sub_in_order receives 1, 2, 3 in sequence
  for (std::uint64_t i = 1ULL; i <= 3ULL; ++i) {
    ASSERT_EQ(shm_subscriber_try_receive(sub_in_order.get(), buf.data(),
                                         buf.size(), &sample),
              SHM_PUBSUB_OK);
    EXPECT_EQ(sample.sequence, i);
    EXPECT_EQ(sample.size, 16U);
    EXPECT_EQ(sample.missed, 0ULL);
    EXPECT_EQ(read_tag(buf.data(), sample.size), i);
  }

  // sub_latest jumps straight to sample 3 with missed == 2
  ASSERT_EQ(shm_subscriber_try_receive(sub_latest.get(), buf.data(), buf.size(),
                                       &sample),
            SHM_PUBSUB_OK);
  EXPECT_EQ(sample.sequence, 3ULL);
  EXPECT_EQ(sample.missed, 2ULL);
  EXPECT_EQ(read_tag(buf.data(), sample.size), 3ULL);

  // Now publish 6 more samples (sequences 4..9), lapping sub_in_order
  // (slot_count = 4)
  for (std::uint64_t i = 4ULL; i <= 9ULL; ++i) {
    const auto payload = make_payload(i, 16U);
    ASSERT_EQ(
        shm_publisher_publish(pub2.get(), payload.data(), payload.size(), &seq),
        SHM_PUBSUB_OK);
  }
  ASSERT_EQ(shm_subscriber_try_receive(sub_in_order.get(), buf.data(),
                                       buf.size(), &sample),
            SHM_PUBSUB_OK);
  EXPECT_EQ(sample.sequence, 9ULL);
  EXPECT_EQ(sample.missed, 5ULL);
  EXPECT_EQ(read_tag(buf.data(), sample.size), 9ULL);
}

TEST(ShmPubSubCTest, TimedReceiveAndCloseDrainAndWake) {
  RecordProperty("requirements", "SHMPS-REQ-013,SHMPS-REQ-014");
  const TestChannelName name;
  ScopedChannel ch;
  ASSERT_EQ(shm_channel_create(ch.get(), name.c_str(), kSmallConfig),
            SHM_PUBSUB_OK);

  ScopedPublisher pub;
  ScopedSubscriber sub;
  ASSERT_EQ(shm_publisher_attach(pub.get(), ch.get()), SHM_PUBSUB_OK);
  ASSERT_EQ(shm_subscriber_attach(sub.get(), ch.get(), SHM_READ_MODE_IN_ORDER),
            SHM_PUBSUB_OK);

  std::array<std::uint8_t, 64> buf{};
  struct shm_sample sample{};
  const auto t0 = std::chrono::steady_clock::now();
  EXPECT_EQ(shm_subscriber_receive_for(sub.get(), buf.data(), buf.size(),
                                       15'000'000LL, &sample),
            SHM_PUBSUB_ERR_TIMED_OUT);
  const auto elapsed = std::chrono::steady_clock::now() - t0;
  EXPECT_GE(elapsed, std::chrono::milliseconds{10});

  // Publish one sample, then close: subscriber drains sample 1, then gets
  // CLOSED
  const auto payload = make_payload(77ULL, 16U);
  std::uint64_t seq = 0ULL;
  ASSERT_EQ(
      shm_publisher_publish(pub.get(), payload.data(), payload.size(), &seq),
      SHM_PUBSUB_OK);
  shm_channel_close(ch.get());
  EXPECT_TRUE(shm_channel_is_closed(ch.get()));

  EXPECT_EQ(
      shm_publisher_publish(pub.get(), payload.data(), payload.size(), &seq),
      SHM_PUBSUB_ERR_CLOSED);

  ASSERT_EQ(shm_subscriber_receive(sub.get(), buf.data(), buf.size(), &sample),
            SHM_PUBSUB_OK);
  EXPECT_EQ(sample.sequence, 1ULL);
  EXPECT_EQ(shm_subscriber_receive(sub.get(), buf.data(), buf.size(), &sample),
            SHM_PUBSUB_ERR_CLOSED);
}

// ============================================================================
// Tier 2: Resource & Memory Residency
// ============================================================================

TEST(ShmPubSubCTest, NoHeapAllocationAfterInitialization) {
  RecordProperty("requirements", "SHMPS-REQ-018,SHMPS-REQ-023");
  const TestChannelName name;
  ScopedChannel ch;
  ASSERT_EQ(shm_channel_create(ch.get(), name.c_str(), kSmallConfig),
            SHM_PUBSUB_OK);

  ScopedPublisher pub;
  ScopedSubscriber sub;
  ASSERT_EQ(shm_publisher_attach(pub.get(), ch.get()), SHM_PUBSUB_OK);
  ASSERT_EQ(shm_subscriber_attach(sub.get(), ch.get(), SHM_READ_MODE_IN_ORDER),
            SHM_PUBSUB_OK);

  std::array<std::uint8_t, 64> send_buf{};
  std::array<std::uint8_t, 64> recv_buf{};
  struct shm_sample sample{};
  std::uint64_t seq = 0ULL;

  const systems_test::AllocationScope scope;
  for (std::uint64_t i = 1ULL; i <= 16ULL; ++i) {
    send_buf[0] = static_cast<std::uint8_t>(i);
    ASSERT_EQ(shm_publisher_publish(pub.get(), send_buf.data(), 16U, &seq),
              SHM_PUBSUB_OK);
    ASSERT_EQ(shm_subscriber_try_receive(sub.get(), recv_buf.data(),
                                         recv_buf.size(), &sample),
              SHM_PUBSUB_OK);
  }
  shm_channel_close(ch.get());
  (void)shm_channel_is_closed(ch.get());
  EXPECT_EQ(scope.allocations(), 0U);
}

TEST(ShmPubSubCTest, MemoryLockFailureLeavesNothingBehind) {
  RecordProperty("requirements", "SHMPS-REQ-024");
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
  GTEST_SKIP() << "Sanitizers intercept mlock() as a no-op";
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
  GTEST_SKIP() << "Sanitizers intercept mlock() as a no-op";
#endif
#endif
  if (!systems_is_linux || ::geteuid() == 0U) {
    GTEST_SKIP() << "RLIMIT_MEMLOCK=0 enforcement test requires non-root Linux";
  }
  const TestChannelName name;
  auto child = systems_test::ChildProcess::spawn([&] -> int {
    const struct rlimit zero_lock = {.rlim_cur = 0, .rlim_max = 0};
    if (setrlimit(RLIMIT_MEMLOCK, &zero_lock) != 0) {
      return 2;
    }
    struct shm_channel ch{};
    const auto err = shm_channel_create(&ch, name.c_str(), kSmallConfig);
    shm_channel_destroy(&ch);
    return err == SHM_PUBSUB_ERR_MEMORY_LOCK_FAILED ? 0 : 1;
  });
  const auto status = child.wait_for(kChildWait);
  EXPECT_TRUE(status.succeeded());
  EXPECT_FALSE(systems_test::shm_exists(name.str()));
}

// ============================================================================
// Tier 3: Concurrency & Multi-Process & Fault Injection
// ============================================================================

TEST(ShmPubSubCTest, ConcurrentPublishAndReceiveNeverTears) {
  RecordProperty("requirements", "SHMPS-REQ-015,SHMPS-REQ-026");
  const TestChannelName name;
  ScopedChannel ch;
  const struct shm_pubsub_config cfg = {.slot_count = 4U, .slot_size = 64U};
  ASSERT_EQ(shm_channel_create(ch.get(), name.c_str(), cfg), SHM_PUBSUB_OK);

  ScopedPublisher pub;
  ScopedSubscriber sub;
  ASSERT_EQ(shm_publisher_attach(pub.get(), ch.get()), SHM_PUBSUB_OK);
  ASSERT_EQ(shm_subscriber_attach(sub.get(), ch.get(), SHM_READ_MODE_IN_ORDER),
            SHM_PUBSUB_OK);

  constexpr std::uint64_t kTotalSamples = 2000ULL;
  std::atomic<bool> torn_detected{false};
  std::latch start_latch{2};

  std::thread writer([&] {
    start_latch.arrive_and_wait();
    for (std::uint64_t s = 1ULL; s <= kTotalSamples; ++s) {
      const auto payload = make_payload(s, 64U);
      std::uint64_t seq = 0ULL;
      if (shm_publisher_publish(pub.get(), payload.data(), payload.size(),
                                &seq) != SHM_PUBSUB_OK) {
        torn_detected.store(true);
        break;
      }
    }
    shm_channel_close(ch.get());
  });

  std::thread reader([&] {
    start_latch.arrive_and_wait();
    std::array<std::uint8_t, 64> buf{};
    struct shm_sample sample{};
    std::uint64_t last_seq = 0ULL;
    for (;;) {
      const auto err = shm_subscriber_receive_for(
          sub.get(), buf.data(), buf.size(), 2'000'000'000LL, &sample);
      if (err == SHM_PUBSUB_ERR_CLOSED) {
        break;
      }
      if (err != SHM_PUBSUB_OK) {
        torn_detected.store(true);
        break;
      }
      if (sample.sequence <= last_seq ||
          read_tag(buf.data(), sample.size) != sample.sequence ||
          !payload_is_consistent(buf.data(), sample.size)) {
        torn_detected.store(true);
        break;
      }
      last_seq = sample.sequence;
    }
  });

  writer.join();
  reader.join();
  EXPECT_FALSE(torn_detected.load());
}

struct CrashSharedState {
  std::atomic<bool> child_attached{false};
  std::atomic<bool> child_published{false};
};

TEST(ShmPubSubCTest, CrossProcessAndDeadPublisherTakeover) {
  RecordProperty("requirements", "SHMPS-REQ-016,SHMPS-REQ-020,SHMPS-REQ-025");
  const TestChannelName name;
  ScopedChannel ch;
  ASSERT_EQ(shm_channel_create(ch.get(), name.c_str(), kSmallConfig),
            SHM_PUBSUB_OK);

  ScopedSubscriber sub;
  ASSERT_EQ(shm_subscriber_attach(sub.get(), ch.get(), SHM_READ_MODE_IN_ORDER),
            SHM_PUBSUB_OK);

  systems_test::SharedRegion<CrashSharedState> shared;

  auto child = systems_test::ChildProcess::spawn([&] -> int {
    struct shm_channel child_ch{};
    if (shm_channel_open(&child_ch, name.c_str()) != SHM_PUBSUB_OK) {
      return 1;
    }
    struct shm_publisher child_pub{};
    if (shm_publisher_attach(&child_pub, &child_ch) != SHM_PUBSUB_OK) {
      return 2;
    }
    shared->child_attached.store(true, std::memory_order_release);
    for (std::uint64_t i = 1ULL; i <= 3ULL; ++i) {
      const auto payload = make_payload(i, 32U);
      std::uint64_t seq = 0ULL;
      if (shm_publisher_publish(&child_pub, payload.data(), payload.size(),
                                &seq) != SHM_PUBSUB_OK) {
        return 3;
      }
    }
    shared->child_published.store(true, std::memory_order_release);
    // Block until parent SIGKILLs us to test dead-publisher takeover
    for (;;) {
      pause();
    }
    return 0;
  });

  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{5};
  while (!shared->child_published.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds{2});
  }
  ASSERT_TRUE(shared->child_published.load());

  EXPECT_TRUE(child.kill(SIGKILL).killed_by(SIGKILL));

  // Replacement publisher takes over and continues numbering past 3
  ScopedPublisher recovery_pub;
  ASSERT_EQ(shm_publisher_attach(recovery_pub.get(), ch.get()), SHM_PUBSUB_OK);
  const auto payload4 = make_payload(4ULL, 32U);
  std::uint64_t next_seq = 0ULL;
  ASSERT_EQ(shm_publisher_publish(recovery_pub.get(), payload4.data(),
                                  payload4.size(), &next_seq),
            SHM_PUBSUB_OK);
  EXPECT_EQ(next_seq, 4ULL);

  std::array<std::uint8_t, 64> buf{};
  struct shm_sample sample{};
  for (std::uint64_t expected = 1ULL; expected <= 4ULL; ++expected) {
    ASSERT_EQ(
        shm_subscriber_try_receive(sub.get(), buf.data(), buf.size(), &sample),
        SHM_PUBSUB_OK);
    EXPECT_EQ(sample.sequence, expected);
    EXPECT_TRUE(payload_is_consistent(buf.data(), sample.size));
  }
}

}  // namespace
