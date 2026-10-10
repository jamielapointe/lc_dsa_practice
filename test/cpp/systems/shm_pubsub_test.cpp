/// @file shm_pubsub_test.cpp
/// @brief Requirements-based tests for the shared-memory pub/sub channel
/// (`docs/systems/shm_pubsub.md`, prefix SHMPS).
///
/// Tiers: functional and boundary (`ShmPubSubTest`), resource
/// (`ShmPubSubTest.NoHeap...`), multi-thread (`ShmPubSubConcurrencyTest`),
/// multi-process (`ShmPubSubMultiProcessTest`), and fault injection
/// (`ShmPubSubFaultTest`). Every loop is bounded by a deadline so the TDD red
/// state fails cleanly instead of hanging.

#include "systems/shm_pubsub.hpp"

#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <latch>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "systems/platform.hpp"
#include "systems_test/allocation_counter.hpp"
#include "systems_test/process.hpp"
#include "systems_test/shared_region.hpp"
#include "systems_test/shm_name.hpp"

namespace {

using systems::ShmChannel;
using systems::ShmPublisher;
using systems::ShmPubSubConfig;
using systems::ShmPubSubError;
using systems::ShmSample;
using systems::ShmSubscriber;
using Clock = std::chrono::steady_clock;

constexpr ShmPubSubConfig kSmall{.slot_count = 8, .slot_size = 64};
constexpr auto kChildWait = std::chrono::seconds{15};

// ----------------------------------------------------- compile-time contract
// SHMPS-REQ-019: move-only handles with noexcept moves.
static_assert(!std::is_copy_constructible_v<ShmChannel>);
static_assert(std::is_nothrow_move_constructible_v<ShmChannel>);
static_assert(std::is_nothrow_move_assignable_v<ShmChannel>);
static_assert(!std::is_copy_constructible_v<ShmPublisher>);
static_assert(std::is_nothrow_move_constructible_v<ShmPublisher>);
static_assert(!std::is_copy_constructible_v<ShmSubscriber>);
static_assert(std::is_nothrow_move_constructible_v<ShmSubscriber>);
static_assert(noexcept(ShmChannel::create(std::string_view{}, kSmall)));
static_assert(noexcept(std::declval<ShmPublisher&>().publish({})));
static_assert(noexcept(std::declval<ShmSubscriber&>().try_receive({})));
static_assert(noexcept(std::declval<ShmChannel&>().close()));

// ------------------------------------------------------------------- helpers

/// Unique channel name that is unlinked before and after the test.
class TestName {
 public:
  TestName() : name_{systems_test::unique_shm_name("shmps")}, guard_{name_} {}
  [[nodiscard]] auto str() const -> const std::string& { return name_; }

 private:
  std::string name_;
  systems_test::ScopedShmUnlink guard_;
};

/// Payload whose bytes are all derived from `tag`, so any mix of two
/// publications (a torn read) is detectable. The first 8 bytes hold the tag.
auto make_payload(std::uint64_t tag, std::size_t size)
    -> std::vector<std::byte> {
  std::vector<std::byte> bytes(size);
  std::array<std::byte, sizeof(tag)> tag_bytes{};
  std::memcpy(tag_bytes.data(), &tag, sizeof(tag));
  for (std::size_t i = 0; i < size; ++i) {
    bytes[i] = i < tag_bytes.size()
                   ? tag_bytes[i]
                   : static_cast<std::byte>(((tag * 31U) + i) & 0xFFU);
  }
  return bytes;
}

/// Reads the tag of a payload produced by `make_payload` (size >= 8).
auto read_tag(std::span<const std::byte> bytes) -> std::uint64_t {
  std::uint64_t tag = 0;
  std::memcpy(&tag, bytes.data(), std::min(bytes.size(), sizeof(tag)));
  return tag;
}

/// True if every byte matches the pattern implied by the embedded tag.
auto payload_is_consistent(std::span<const std::byte> bytes) -> bool {
  if (bytes.size() < sizeof(std::uint64_t)) {
    return true;
  }
  const auto expected = make_payload(read_tag(bytes), bytes.size());
  return std::equal(bytes.begin(), bytes.end(), expected.begin());
}

auto error_text(ShmPubSubError error) -> std::string {
  return std::string{systems::to_string(error)};
}

// ===================================================== Tier 1-3: functional

TEST(ShmPubSubTest, CreateThenOpenReportsSameConfig) {
  RecordProperty("requirements", "SHMPS-REQ-001,SHMPS-REQ-004");
  const TestName name;
  auto created = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(created.has_value()) << error_text(created.error());
  EXPECT_EQ(created->config(), kSmall);
  EXPECT_TRUE(systems_test::shm_exists(name.str()));

  auto opened = ShmChannel::open(name.str());
  ASSERT_TRUE(opened.has_value()) << error_text(opened.error());
  EXPECT_EQ(opened->config(), kSmall);
  EXPECT_FALSE(opened->is_closed());
}

TEST(ShmPubSubTest, CreateRejectsInvalidConfig) {
  RecordProperty("requirements", "SHMPS-REQ-001");
  const std::vector<ShmPubSubConfig> invalid{
      {.slot_count = 0, .slot_size = 64},
      {.slot_count = 1, .slot_size = 64},
      {.slot_count = 3, .slot_size = 64},
      {.slot_count = 12, .slot_size = 64},
      {.slot_count = systems::kShmMaxSlotCount * 2, .slot_size = 64},
      {.slot_count = 8, .slot_size = 0},
      {.slot_count = 8, .slot_size = systems::kShmMaxSlotSize + 1},
  };
  for (const auto& config : invalid) {
    const TestName name;
    const auto result = ShmChannel::create(name.str(), config);
    ASSERT_FALSE(result.has_value())
        << "slots=" << config.slot_count << " size=" << config.slot_size;
    EXPECT_EQ(result.error(), ShmPubSubError::kInvalidArgument)
        << error_text(result.error());
    EXPECT_FALSE(systems_test::shm_exists(name.str()));
  }
}

TEST(ShmPubSubTest, CreateAcceptsBoundaryConfigs) {
  RecordProperty("requirements", "SHMPS-REQ-001");
  const std::vector<ShmPubSubConfig> valid{
      {.slot_count = systems::kShmMinSlotCount, .slot_size = 1},
      {.slot_count = systems::kShmMaxSlotCount, .slot_size = 1},
      {.slot_count = systems::kShmMinSlotCount,
       .slot_size = systems::kShmMaxSlotSize},
  };
  for (const auto& config : valid) {
    const TestName name;
    const auto result = ShmChannel::create(name.str(), config);
    ASSERT_TRUE(result.has_value()) << error_text(result.error());
    EXPECT_EQ(result->config(), config);
  }
}

TEST(ShmPubSubTest, RejectsMalformedAndOverlongNames) {
  RecordProperty("requirements", "SHMPS-REQ-002");
  for (const char* bad : {"", "noslash", "/", "/a/b"}) {
    const auto result = ShmChannel::create(bad, kSmall);
    ASSERT_FALSE(result.has_value()) << '"' << bad << '"';
    EXPECT_EQ(result.error(), ShmPubSubError::kInvalidArgument) << '"' << bad;
    const auto opened = ShmChannel::open(bad);
    ASSERT_FALSE(opened.has_value());
    EXPECT_EQ(opened.error(), ShmPubSubError::kInvalidArgument);
  }

  const std::string longest =
      "/" + std::string(systems::platform::kMaxShmNameLength - 1, 'n');
  const systems_test::ScopedShmUnlink cleanup{longest};
  const auto ok = ShmChannel::create(longest, kSmall);
  EXPECT_TRUE(ok.has_value()) << error_text(ok.error());

  const std::string too_long = longest + "x";
  const auto rejected = ShmChannel::create(too_long, kSmall);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), ShmPubSubError::kNameTooLong);
}

TEST(ShmPubSubTest, CreateFailsWhenNameExists) {
  RecordProperty("requirements", "SHMPS-REQ-003");
  const TestName name;
  const auto first = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(first.has_value()) << error_text(first.error());

  const auto second =
      ShmChannel::create(name.str(), {.slot_count = 16, .slot_size = 8});
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ShmPubSubError::kAlreadyExists);

  const auto opened = ShmChannel::open(name.str());
  ASSERT_TRUE(opened.has_value()) << error_text(opened.error());
  EXPECT_EQ(opened->config(), kSmall);  // untouched by the failed create
}

TEST(ShmPubSubTest, OpenMissingChannelReturnsNotFound) {
  RecordProperty("requirements", "SHMPS-REQ-004");
  const TestName name;
  const auto result = ShmChannel::open(name.str());
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ShmPubSubError::kNotFound);
}

TEST(ShmPubSubTest, OpenRejectsForeignSegments) {
  RecordProperty("requirements", "SHMPS-REQ-005");
  for (const std::size_t size : {std::size_t{0}, std::size_t{4096}}) {
    const TestName name;
    // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic.
    const int fd =
        ::shm_open(name.str().c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    // NOLINTEND(*-vararg)
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::ftruncate(fd, static_cast<off_t>(size)), 0);
    if (size > 0) {
      void* base =
          ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
      // NOLINTNEXTLINE(*-cstyle-cast,performance-no-int-to-ptr): MAP_FAILED.
      ASSERT_NE(base, MAP_FAILED);
      std::memset(base, 0xAB, size);
      ASSERT_EQ(::munmap(base, size), 0);
    }
    ASSERT_EQ(::close(fd), 0);

    const auto result = ShmChannel::open(name.str());
    ASSERT_FALSE(result.has_value()) << "size " << size;
    EXPECT_EQ(result.error(), ShmPubSubError::kLayoutMismatch)
        << error_text(result.error());
  }
}

TEST(ShmPubSubTest, UnlinkRemovesNameButKeepsMappingUsable) {
  RecordProperty("requirements", "SHMPS-REQ-006");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  EXPECT_TRUE(ShmChannel::unlink(name.str()).has_value());
  EXPECT_FALSE(systems_test::shm_exists(name.str()));
  const auto reopened = ShmChannel::open(name.str());
  ASSERT_FALSE(reopened.has_value());
  EXPECT_EQ(reopened.error(), ShmPubSubError::kNotFound);

  const auto payload = make_payload(42, 16);
  ASSERT_TRUE(publisher->publish(payload).has_value());
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto sample = subscriber->try_receive(buffer);
  ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
  EXPECT_EQ(sample->size, payload.size());

  const auto again = ShmChannel::unlink(name.str());
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error(), ShmPubSubError::kNotFound);
}

TEST(ShmPubSubTest, SecondPublisherIsRejectedUntilFirstIsDestroyed) {
  RecordProperty("requirements", "SHMPS-REQ-007");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto other_handle = ShmChannel::open(name.str());
  ASSERT_TRUE(other_handle.has_value()) << error_text(other_handle.error());
  {
    auto first = ShmPublisher::attach(*channel);
    ASSERT_TRUE(first.has_value()) << error_text(first.error());
    const auto second = ShmPublisher::attach(*channel);
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error(), ShmPubSubError::kPublisherExists);
    const auto via_other = ShmPublisher::attach(*other_handle);
    ASSERT_FALSE(via_other.has_value());
    EXPECT_EQ(via_other.error(), ShmPubSubError::kPublisherExists);
  }
  const auto after = ShmPublisher::attach(*other_handle);
  EXPECT_TRUE(after.has_value()) << error_text(after.error());
}

TEST(ShmPubSubTest, PublishAssignsSequencesStartingAtOne) {
  RecordProperty("requirements", "SHMPS-REQ-008");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  for (std::uint64_t expected = 1; expected <= 3 * kSmall.slot_count;
       ++expected) {
    const auto sequence = publisher->publish(make_payload(expected, 8));
    ASSERT_TRUE(sequence.has_value()) << error_text(sequence.error());
    EXPECT_EQ(*sequence, expected);
  }
}

TEST(ShmPubSubTest, PublishEnforcesPayloadSizeLimits) {
  RecordProperty("requirements", "SHMPS-REQ-008,SHMPS-REQ-010");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  const auto too_big =
      publisher->publish(make_payload(1, kSmall.slot_size + 1));
  ASSERT_FALSE(too_big.has_value());
  EXPECT_EQ(too_big.error(), ShmPubSubError::kPayloadTooLarge);

  ASSERT_TRUE(publisher->publish({}).has_value());
  const auto full = make_payload(7, kSmall.slot_size);
  ASSERT_TRUE(publisher->publish(full).has_value());

  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto empty_sample = subscriber->try_receive(buffer);
  ASSERT_TRUE(empty_sample.has_value()) << error_text(empty_sample.error());
  EXPECT_EQ(empty_sample->size, 0U);
  EXPECT_EQ(empty_sample->sequence, 1U);  // the oversized publish used none
  const auto full_sample = subscriber->try_receive(buffer);
  ASSERT_TRUE(full_sample.has_value()) << error_text(full_sample.error());
  EXPECT_EQ(full_sample->size, kSmall.slot_size);
  EXPECT_TRUE(std::equal(full.begin(), full.end(), buffer.begin()));
}

TEST(ShmPubSubTest, PublishNeverBlocksOnIdleSubscribers) {
  RecordProperty("requirements", "SHMPS-REQ-009");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto idle = ShmSubscriber::attach(*channel);  // never reads
  ASSERT_TRUE(idle.has_value()) << error_text(idle.error());

  const auto payload = make_payload(9, 32);
  const auto start = Clock::now();
  int published = 0;
  for (int i = 0; i < 100 * static_cast<int>(kSmall.slot_count); ++i) {
    if (publisher->publish(payload).has_value()) {
      ++published;
    }
  }
  EXPECT_EQ(published, 100 * static_cast<int>(kSmall.slot_count));
  EXPECT_LT(Clock::now() - start, std::chrono::seconds{5});
}

TEST(ShmPubSubTest, SubscriberReceivesInOrderWithExactBytes) {
  RecordProperty("requirements", "SHMPS-REQ-010");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  const std::vector<std::size_t> sizes{8, 1, 64, 17, 33};
  std::vector<std::vector<std::byte>> sent;
  for (std::size_t i = 0; i < sizes.size(); ++i) {
    sent.push_back(make_payload(100 + i, sizes[i]));
    ASSERT_TRUE(publisher->publish(sent.back()).has_value());
  }
  std::vector<std::byte> buffer(kSmall.slot_size);
  for (std::size_t i = 0; i < sent.size(); ++i) {
    const auto sample = subscriber->try_receive(buffer);
    ASSERT_TRUE(sample.has_value()) << i << ": " << error_text(sample.error());
    EXPECT_EQ(sample->sequence, i + 1);
    EXPECT_EQ(sample->missed, 0U);
    ASSERT_EQ(sample->size, sent[i].size());
    EXPECT_TRUE(std::equal(sent[i].begin(), sent[i].end(), buffer.begin()));
  }
}

TEST(ShmPubSubTest, SubscriberStartsAtNextPublishedSample) {
  RecordProperty("requirements", "SHMPS-REQ-010");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  ASSERT_TRUE(publisher->publish(make_payload(1, 8)).has_value());
  ASSERT_TRUE(publisher->publish(make_payload(2, 8)).has_value());

  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  ASSERT_TRUE(publisher->publish(make_payload(3, 8)).has_value());

  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto sample = subscriber->try_receive(buffer);
  ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
  EXPECT_EQ(sample->sequence, 3U);
  EXPECT_EQ(read_tag(buffer), 3U);
  const auto none = subscriber->try_receive(buffer);
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error(), ShmPubSubError::kWouldBlock);
}

TEST(ShmPubSubTest, LaggingSubscriberSkipsAheadAndReportsMissed) {
  RecordProperty("requirements", "SHMPS-REQ-011,SHMPS-REQ-009");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  constexpr std::uint64_t kPublished = 20;
  for (std::uint64_t seq = 1; seq <= kPublished; ++seq) {
    ASSERT_TRUE(publisher->publish(make_payload(seq, 16)).has_value());
  }
  // Slots hold sequences 13..20; 1..12 were overwritten.
  const std::uint64_t oldest = kPublished - kSmall.slot_count + 1;
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto first = subscriber->try_receive(buffer);
  ASSERT_TRUE(first.has_value()) << error_text(first.error());
  EXPECT_EQ(first->sequence, oldest);
  EXPECT_EQ(first->missed, oldest - 1);
  EXPECT_EQ(read_tag(buffer), oldest);
  for (std::uint64_t seq = oldest + 1; seq <= kPublished; ++seq) {
    const auto sample = subscriber->try_receive(buffer);
    ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
    EXPECT_EQ(sample->sequence, seq);
    EXPECT_EQ(sample->missed, 0U);
  }
}

TEST(ShmPubSubTest, TryReceiveWouldBlockWhenNothingNew) {
  RecordProperty("requirements", "SHMPS-REQ-012");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto result = subscriber->try_receive(buffer);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ShmPubSubError::kWouldBlock);
}

TEST(ShmPubSubTest, BufferTooSmallDoesNotConsumeTheSample) {
  RecordProperty("requirements", "SHMPS-REQ-012");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  ASSERT_TRUE(publisher->publish(make_payload(5, 32)).has_value());

  std::vector<std::byte> small(16);
  const auto rejected = subscriber->try_receive(small);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), ShmPubSubError::kBufferTooSmall);

  std::vector<std::byte> large(kSmall.slot_size);
  const auto sample = subscriber->try_receive(large);
  ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
  EXPECT_EQ(sample->sequence, 1U);
  EXPECT_EQ(sample->size, 32U);
}

TEST(ShmPubSubTest, ReceiveForTimesOutNoEarlierThanTimeout) {
  RecordProperty("requirements", "SHMPS-REQ-013");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  constexpr auto kTimeout = std::chrono::milliseconds{50};
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto start = Clock::now();
  const auto result = subscriber->receive_for(buffer, kTimeout);
  const auto elapsed = Clock::now() - start;
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ShmPubSubError::kTimedOut);
  EXPECT_GE(elapsed, kTimeout);
  EXPECT_LT(elapsed, std::chrono::seconds{2});
}

TEST(ShmPubSubTest, ReceiveForWakesSoonAfterPublish) {
  RecordProperty("requirements", "SHMPS-REQ-013");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  std::atomic<Clock::rep> published_at{0};
  std::jthread producer{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    published_at.store(Clock::now().time_since_epoch().count());
    static_cast<void>(publisher->publish(make_payload(1, 8)));
  }};
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto result = subscriber->receive_for(buffer, std::chrono::seconds{5});
  const auto returned_at = Clock::now();
  producer.join();
  ASSERT_TRUE(result.has_value()) << error_text(result.error());
  EXPECT_EQ(result->sequence, 1U);
  const auto latency =
      returned_at - Clock::time_point{Clock::duration{published_at.load()}};
  // Spec: 100 ms. Allow 5x for sanitizer builds.
  EXPECT_LT(latency, std::chrono::milliseconds{500});
}

TEST(ShmPubSubTest, BlockingReceiveReturnsPublishedSample) {
  RecordProperty("requirements", "SHMPS-REQ-013");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  std::expected<ShmSample, ShmPubSubError> result =
      std::unexpected(ShmPubSubError::kNotImplemented);
  std::atomic<bool> done{false};
  {
    std::jthread reader{[&] {
      std::vector<std::byte> buffer(kSmall.slot_size);
      result = subscriber->receive(buffer);
      done.store(true);
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    static_cast<void>(publisher->publish(make_payload(11, 8)));
    if (!systems_test::wait_until([&] { return done.load(); },
                                  std::chrono::seconds{5})) {
      channel->close();  // unblock a reader that missed the sample
    }
  }
  ASSERT_TRUE(result.has_value()) << error_text(result.error());
  EXPECT_EQ(result->sequence, 1U);
}

TEST(ShmPubSubTest, CloseStopsPublishingAndDrainsSubscribers) {
  RecordProperty("requirements", "SHMPS-REQ-014");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  ASSERT_TRUE(publisher->publish(make_payload(1, 8)).has_value());
  ASSERT_TRUE(publisher->publish(make_payload(2, 8)).has_value());

  auto other = ShmChannel::open(name.str());
  ASSERT_TRUE(other.has_value()) << error_text(other.error());
  other->close();
  other->close();  // idempotent
  EXPECT_TRUE(channel->is_closed());

  const auto rejected = publisher->publish(make_payload(3, 8));
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error(), ShmPubSubError::kClosed);

  std::vector<std::byte> buffer(kSmall.slot_size);
  for (std::uint64_t seq = 1; seq <= 2; ++seq) {
    const auto sample = subscriber->try_receive(buffer);
    ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
    EXPECT_EQ(sample->sequence, seq);
  }
  const auto drained = subscriber->try_receive(buffer);
  ASSERT_FALSE(drained.has_value());
  EXPECT_EQ(drained.error(), ShmPubSubError::kClosed);
  const auto timed = subscriber->receive_for(buffer, std::chrono::seconds{5});
  ASSERT_FALSE(timed.has_value());
  EXPECT_EQ(timed.error(), ShmPubSubError::kClosed);
}

TEST(ShmPubSubTest, CloseWakesBlockedReceiversInThreads) {
  RecordProperty("requirements", "SHMPS-REQ-014");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());

  constexpr int kReaders = 4;
  std::vector<ShmSubscriber> subscribers;
  for (int i = 0; i < kReaders; ++i) {
    auto subscriber = ShmSubscriber::attach(*channel);
    ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
    subscribers.push_back(std::move(*subscriber));
  }
  std::atomic<int> closed_results{0};
  std::atomic<Clock::rep> last_return{0};
  std::latch started{kReaders + 1};
  Clock::time_point closed_at;
  {
    std::vector<std::jthread> readers;
    readers.reserve(kReaders);
    for (int i = 0; i < kReaders; ++i) {
      readers.emplace_back([&, i] {
        std::vector<std::byte> buffer(kSmall.slot_size);
        started.arrive_and_wait();
        const auto result =
            (i % 2 == 0)
                ? subscribers[static_cast<std::size_t>(i)].receive(buffer)
                : subscribers[static_cast<std::size_t>(i)].receive_for(
                      buffer, std::chrono::seconds{10});
        if (!result.has_value() && result.error() == ShmPubSubError::kClosed) {
          closed_results.fetch_add(1);
        }
        last_return.store(Clock::now().time_since_epoch().count());
      });
    }
    started.arrive_and_wait();
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    closed_at = Clock::now();
    channel->close();
  }
  EXPECT_EQ(closed_results.load(), kReaders);
  const auto wake =
      Clock::time_point{Clock::duration{last_return.load()}} - closed_at;
  EXPECT_LT(wake, std::chrono::milliseconds{500});  // spec 100 ms, 5x slack
}

TEST(ShmPubSubTest, NoHeapAllocationAfterInitialization) {
  RecordProperty("requirements", "SHMPS-REQ-018");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  const auto payload = make_payload(3, 48);
  std::vector<std::byte> buffer(kSmall.slot_size);

  int publish_ok = 0;
  int receive_ok = 0;
  std::size_t allocations = 0;
  {
    const systems_test::AllocationScope scope;
    for (int i = 0; i < 1000; ++i) {
      publish_ok += publisher->publish(payload).has_value() ? 1 : 0;
      receive_ok += subscriber->try_receive(buffer).has_value() ? 1 : 0;
    }
    static_cast<void>(
        subscriber->receive_for(buffer, std::chrono::milliseconds{1}));
    static_cast<void>(channel->is_closed());
    channel->close();
    static_cast<void>(subscriber->receive(buffer));
    allocations = scope.allocations();
  }
  EXPECT_EQ(allocations, 0U);
  EXPECT_EQ(publish_ok, 1000);
  EXPECT_EQ(receive_ok, 1000);
}

TEST(ShmPubSubTest, DestroyingHandleKeepsNameAndMovedHandlesWork) {
  RecordProperty("requirements", "SHMPS-REQ-019,SHMPS-REQ-007");
  const TestName name;
  {
    auto created = ShmChannel::create(name.str(), kSmall);
    ASSERT_TRUE(created.has_value()) << error_text(created.error());
  }
  EXPECT_TRUE(systems_test::shm_exists(name.str()));

  auto opened = ShmChannel::open(name.str());
  ASSERT_TRUE(opened.has_value()) << error_text(opened.error());
  ShmChannel channel{std::move(*opened)};
  EXPECT_EQ(channel.config(), kSmall);

  auto attached = ShmPublisher::attach(channel);
  ASSERT_TRUE(attached.has_value()) << error_text(attached.error());
  ShmPublisher publisher{std::move(*attached)};
  {
    // Destroying the moved-from handle must not release the role.
    const auto moved_from = std::move(attached);
  }
  const auto second = ShmPublisher::attach(channel);
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ShmPubSubError::kPublisherExists);

  auto subscribed = ShmSubscriber::attach(channel);
  ASSERT_TRUE(subscribed.has_value()) << error_text(subscribed.error());
  ShmSubscriber subscriber{std::move(*subscribed)};
  ASSERT_TRUE(publisher.publish(make_payload(1, 8)).has_value());
  std::vector<std::byte> buffer(kSmall.slot_size);
  EXPECT_TRUE(subscriber.try_receive(buffer).has_value());
}

// ================================================= Tier 4: multi-thread

TEST(ShmPubSubConcurrencyTest, ConcurrentReadersNeverSeeTornSamples) {
  RecordProperty("requirements",
                 "SHMPS-REQ-015,SHMPS-REQ-017,SHMPS-REQ-011,SHMPS-REQ-009");
  constexpr ShmPubSubConfig kTiny{.slot_count = 4, .slot_size = 256};
  constexpr int kReaders = 4;
  constexpr std::uint64_t kMaxPublishes = 200'000;
  constexpr auto kBudget = std::chrono::milliseconds{1500};

  const TestName name;
  auto channel = ShmChannel::create(name.str(), kTiny);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  std::vector<ShmSubscriber> subscribers;
  for (int i = 0; i < kReaders; ++i) {
    auto subscriber = ShmSubscriber::attach(*channel);
    ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
    subscribers.push_back(std::move(*subscriber));
  }

  std::atomic<bool> publishing_done{false};
  std::atomic<std::uint64_t> published{0};
  std::atomic<std::uint64_t> received{0};
  std::atomic<std::uint64_t> torn{0};
  std::atomic<std::uint64_t> order_violations{0};
  std::latch start{kReaders + 2};
  {
    std::vector<std::jthread> threads;
    threads.reserve(kReaders + 1);
    for (int r = 0; r < kReaders; ++r) {
      threads.emplace_back([&, r] {
        auto& subscriber = subscribers[static_cast<std::size_t>(r)];
        std::vector<std::byte> buffer(kTiny.slot_size);
        std::uint64_t previous = 0;
        start.arrive_and_wait();
        const auto deadline = Clock::now() + kBudget + std::chrono::seconds{2};
        while (Clock::now() < deadline) {
          const auto sample = subscriber.try_receive(buffer);
          if (!sample.has_value()) {
            if (sample.error() == ShmPubSubError::kWouldBlock &&
                !publishing_done.load()) {
              continue;
            }
            break;  // drained after the publisher stopped, or an error
          }
          received.fetch_add(1);
          const auto bytes = std::span{buffer}.first(sample->size);
          if (!payload_is_consistent(bytes) ||
              read_tag(bytes) != sample->sequence) {
            torn.fetch_add(1);
          }
          if (sample->sequence != previous + 1 + sample->missed) {
            order_violations.fetch_add(1);
          }
          previous = sample->sequence;
        }
      });
    }
    threads.emplace_back([&] {
      start.arrive_and_wait();
      const auto deadline = Clock::now() + kBudget;
      for (std::uint64_t seq = 1;
           seq <= kMaxPublishes && Clock::now() < deadline; ++seq) {
        const auto size = 8 + (seq % (kTiny.slot_size - 8));
        if (publisher->publish(make_payload(seq, size)).has_value()) {
          published.fetch_add(1);
        }
      }
      publishing_done.store(true);
    });
    start.arrive_and_wait();
  }
  EXPECT_GT(published.load(), 1000U);
  EXPECT_GT(received.load(), 0U);
  EXPECT_EQ(torn.load(), 0U);
  EXPECT_EQ(order_violations.load(), 0U);
}

TEST(ShmPubSubConcurrencyTest, IndependentSubscribersEachReceiveEverything) {
  RecordProperty("requirements", "SHMPS-REQ-017");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  constexpr int kSubscribers = 3;
  std::vector<ShmSubscriber> subscribers;
  for (int i = 0; i < kSubscribers; ++i) {
    auto subscriber = ShmSubscriber::attach(*channel);
    ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
    subscribers.push_back(std::move(*subscriber));
  }
  std::vector<std::byte> buffer(kSmall.slot_size);
  for (std::uint64_t seq = 1; seq <= 100; ++seq) {
    ASSERT_TRUE(publisher->publish(make_payload(seq, 24)).has_value());
    for (auto& subscriber : subscribers) {
      const auto sample = subscriber.try_receive(buffer);
      ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
      EXPECT_EQ(sample->sequence, seq);
      EXPECT_EQ(sample->missed, 0U);
    }
  }
}

// =============================================== Tier 5: multi-process

struct ProcessTally {
  std::atomic<std::uint32_t> ready{0};
  std::atomic<std::uint32_t> go{0};
  std::atomic<std::uint64_t> received{0};
  std::atomic<std::uint64_t> torn{0};
  std::atomic<std::uint64_t> last_sequence{0};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

/// Child body: open `name`, subscribe, signal ready, receive until `count`
/// samples, close, or the deadline; record into `tally`.
auto child_subscriber(const std::string& name, ProcessTally& tally,
                      std::uint64_t count) -> int {
  auto channel = ShmChannel::open(name);
  if (!channel) {
    return 1;
  }
  auto subscriber = ShmSubscriber::attach(*channel);
  if (!subscriber) {
    return 2;
  }
  tally.ready.fetch_add(1);
  std::vector<std::byte> buffer(channel->config().slot_size);
  const auto deadline = Clock::now() + std::chrono::seconds{10};
  while (tally.received.load() < count && Clock::now() < deadline) {
    const auto sample =
        subscriber->receive_for(buffer, std::chrono::milliseconds{100});
    if (!sample) {
      if (sample.error() == ShmPubSubError::kTimedOut) {
        continue;
      }
      break;
    }
    tally.received.fetch_add(1);
    const auto bytes = std::span{buffer}.first(sample->size);
    if (!payload_is_consistent(bytes) || read_tag(bytes) != sample->sequence) {
      tally.torn.fetch_add(1);
    }
    tally.last_sequence.store(sample->sequence);
  }
  return tally.torn.load() == 0 ? systems_test::kChildOk : 3;
}

TEST(ShmPubSubMultiProcessTest, ChildSubscriberReceivesParentSamples) {
  RecordProperty("requirements", "SHMPS-REQ-016,SHMPS-REQ-010");
  constexpr std::uint64_t kCount = 500;
  constexpr ShmPubSubConfig kRoomy{.slot_count = 1024, .slot_size = 64};
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kRoomy);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  systems_test::SharedRegion<ProcessTally> tally;
  ASSERT_TRUE(tally.valid());

  auto child = systems_test::ChildProcess::spawn(
      [&] { return child_subscriber(name.str(), *tally, kCount); });
  ASSERT_TRUE(systems_test::wait_until([&] { return tally->ready.load() == 1; },
                                       std::chrono::seconds{5}));
  for (std::uint64_t seq = 1; seq <= kCount; ++seq) {
    ASSERT_TRUE(
        publisher->publish(make_payload(seq, 8 + (seq % 56))).has_value());
  }
  const auto result = child.wait_for(kChildWait);
  EXPECT_TRUE(result.succeeded()) << result;
  EXPECT_EQ(tally->received.load(), kCount);
  EXPECT_EQ(tally->last_sequence.load(), kCount);
  EXPECT_EQ(tally->torn.load(), 0U);
}

TEST(ShmPubSubMultiProcessTest, ChildPublisherBlocksParentPublisherRole) {
  RecordProperty("requirements", "SHMPS-REQ-016,SHMPS-REQ-007");
  constexpr std::uint64_t kCount = 200;
  constexpr ShmPubSubConfig kRoomy{.slot_count = 256, .slot_size = 64};
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kRoomy);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  systems_test::SharedRegion<ProcessTally> tally;
  ASSERT_TRUE(tally.valid());

  auto child = systems_test::ChildProcess::spawn([&] -> int {
    auto opened = ShmChannel::open(name.str());
    if (!opened) {
      return 1;
    }
    auto pub = ShmPublisher::attach(*opened);
    if (!pub) {
      return 2;
    }
    tally->ready.store(1);
    if (!systems_test::wait_until([&] { return tally->go.load() == 1; },
                                  std::chrono::seconds{5})) {
      return 3;
    }
    for (std::uint64_t seq = 1; seq <= kCount; ++seq) {
      if (!pub->publish(make_payload(seq, 32))) {
        return 4;
      }
    }
    return systems_test::kChildOk;
  });
  ASSERT_TRUE(systems_test::wait_until([&] { return tally->ready.load() == 1; },
                                       std::chrono::seconds{5}));
  const auto blocked = ShmPublisher::attach(*channel);
  ASSERT_FALSE(blocked.has_value());
  EXPECT_EQ(blocked.error(), ShmPubSubError::kPublisherExists);
  tally->go.store(1);

  std::vector<std::byte> buffer(kRoomy.slot_size);
  std::uint64_t received = 0;
  const auto deadline = Clock::now() + std::chrono::seconds{10};
  while (received < kCount && Clock::now() < deadline) {
    const auto sample =
        subscriber->receive_for(buffer, std::chrono::milliseconds{100});
    if (sample) {
      ++received;
      EXPECT_EQ(sample->sequence, received);
      EXPECT_TRUE(payload_is_consistent(std::span{buffer}.first(sample->size)));
    } else if (sample.error() != ShmPubSubError::kTimedOut) {
      break;
    }
  }
  EXPECT_EQ(received, kCount);
  EXPECT_TRUE(child.wait_for(kChildWait).succeeded());
}

TEST(ShmPubSubMultiProcessTest, CloseWakesReceiverInAnotherProcess) {
  RecordProperty("requirements", "SHMPS-REQ-014,SHMPS-REQ-016");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  systems_test::SharedRegion<ProcessTally> tally;
  ASSERT_TRUE(tally.valid());

  auto child = systems_test::ChildProcess::spawn([&] -> int {
    auto opened = ShmChannel::open(name.str());
    if (!opened) {
      return 1;
    }
    auto sub = ShmSubscriber::attach(*opened);
    if (!sub) {
      return 2;
    }
    std::vector<std::byte> buffer(kSmall.slot_size);
    tally->ready.store(1);
    const auto result = sub->receive(buffer);  // blocks until close
    return (!result && result.error() == ShmPubSubError::kClosed) ? 0 : 3;
  });
  ASSERT_TRUE(systems_test::wait_until([&] { return tally->ready.load() == 1; },
                                       std::chrono::seconds{5}));
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  const auto closed_at = Clock::now();
  channel->close();
  const auto result = child.wait_for(std::chrono::seconds{5});
  EXPECT_TRUE(result.succeeded()) << result;
  EXPECT_LT(Clock::now() - closed_at, std::chrono::seconds{1});
}

TEST(ShmPubSubMultiProcessTest, ManySubscriberProcessesNeverSeeTornSamples) {
  RecordProperty("requirements", "SHMPS-REQ-015,SHMPS-REQ-016,SHMPS-REQ-017");
  constexpr int kChildren = 3;
  constexpr ShmPubSubConfig kTiny{.slot_count = 4, .slot_size = 512};
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kTiny);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  std::array<systems_test::SharedRegion<ProcessTally>, kChildren> tallies;
  std::vector<systems_test::ChildProcess> children;
  for (auto& tally : tallies) {
    ASSERT_TRUE(tally.valid());
    children.push_back(systems_test::ChildProcess::spawn([&] {
      return child_subscriber(name.str(), *tally,
                              std::numeric_limits<std::uint64_t>::max());
    }));
  }
  ASSERT_TRUE(systems_test::wait_until(
      [&] {
        return std::ranges::all_of(
            tallies, [](const auto& t) { return t->ready.load() == 1; });
      },
      std::chrono::seconds{5}));
  const auto deadline = Clock::now() + std::chrono::milliseconds{1500};
  std::uint64_t seq = 0;
  while (Clock::now() < deadline) {
    ++seq;
    ASSERT_TRUE(
        publisher->publish(make_payload(seq, 8 + (seq % 500))).has_value());
  }
  channel->close();
  for (std::size_t i = 0; i < children.size(); ++i) {
    const auto result = children[i].wait_for(kChildWait);
    EXPECT_TRUE(result.succeeded()) << "child " << i << ": " << result;
    EXPECT_GT(tallies[i]->received.load(), 0U);
    EXPECT_EQ(tallies[i]->torn.load(), 0U);
  }
}

// =============================================== Tier 6: fault injection

TEST(ShmPubSubFaultTest, PublisherTakeoverRequiresDeadOwner) {
  RecordProperty("requirements", "SHMPS-REQ-007,SHMPS-REQ-020");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  systems_test::SharedRegion<ProcessTally> tally;
  ASSERT_TRUE(tally.valid());

  auto child = systems_test::ChildProcess::spawn([&] -> int {
    auto opened = ShmChannel::open(name.str());
    if (!opened) {
      return 1;
    }
    auto pub = ShmPublisher::attach(*opened);
    if (!pub) {
      return 2;
    }
    tally->ready.store(1);
    std::this_thread::sleep_for(std::chrono::seconds{30});  // hold the role
    return 0;
  });
  ASSERT_TRUE(systems_test::wait_until([&] { return tally->ready.load() == 1; },
                                       std::chrono::seconds{5}));
  const auto while_alive = ShmPublisher::attach(*channel);
  ASSERT_FALSE(while_alive.has_value());
  EXPECT_EQ(while_alive.error(), ShmPubSubError::kPublisherExists);

  EXPECT_TRUE(child.kill(SIGKILL).killed_by(SIGKILL));
  const auto after_death = ShmPublisher::attach(*channel);
  EXPECT_TRUE(after_death.has_value()) << error_text(after_death.error());
}

TEST(ShmPubSubFaultTest, PublisherKilledAtRandomPointsNeverCorruptsReaders) {
  RecordProperty("requirements", "SHMPS-REQ-020,SHMPS-REQ-015,SHMPS-REQ-016");
  constexpr int kRounds = 12;
  constexpr ShmPubSubConfig kTiny{.slot_count = 4, .slot_size = 1024};
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kTiny);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
  systems_test::SharedRegion<ProcessTally> tally;
  ASSERT_TRUE(tally.valid());

  std::vector<std::byte> buffer(kTiny.slot_size);
  std::uint64_t highest_seen = 0;
  std::uint64_t torn = 0;
  const auto drain = [&] {
    for (int i = 0; i < 64; ++i) {
      const auto sample = subscriber->try_receive(buffer);
      if (!sample) {
        return;
      }
      // Self-consistency only: after a takeover the publisher may skip
      // sequence numbers, so the tag need not equal the sequence.
      if (!payload_is_consistent(std::span{buffer}.first(sample->size))) {
        ++torn;
      }
      highest_seen = std::max(highest_seen, sample->sequence);
    }
  };

  for (int round = 0; round < kRounds; ++round) {
    tally->ready.store(0);
    auto victim = systems_test::ChildProcess::spawn([&] -> int {
      auto opened = ShmChannel::open(name.str());
      if (!opened) {
        return 1;
      }
      auto pub = ShmPublisher::attach(*opened);
      if (!pub) {
        return 2;
      }
      tally->ready.store(1);
      const auto deadline = Clock::now() + std::chrono::seconds{20};
      while (Clock::now() < deadline) {  // publish until killed
        const auto next = tally->last_sequence.load() + 1;
        auto sequence = pub->publish(make_payload(next, 1024));
        if (sequence) {
          tally->last_sequence.store(*sequence);
        }
      }
      return 0;
    });
    ASSERT_TRUE(systems_test::wait_until(
        [&] { return tally->ready.load() == 1; }, std::chrono::seconds{5}))
        << "round " << round;
    // Vary the kill point: 0..2.75 ms into the publishing loop.
    std::this_thread::sleep_for(std::chrono::microseconds{250 * (round % 12)});
    drain();
    EXPECT_TRUE(victim.kill(SIGKILL).killed_by(SIGKILL));
    drain();

    auto replacement = ShmPublisher::attach(*channel);
    ASSERT_TRUE(replacement.has_value())
        << "round " << round << ": " << error_text(replacement.error());
    const auto next = std::max(highest_seen, tally->last_sequence.load()) + 1;
    const auto sequence = replacement->publish(make_payload(next, 1024));
    ASSERT_TRUE(sequence.has_value()) << error_text(sequence.error());
    EXPECT_GT(*sequence, highest_seen);
    EXPECT_GT(*sequence, tally->last_sequence.load());
    tally->last_sequence.store(*sequence);
    drain();
  }  // replacement is released at the end of each round
  EXPECT_EQ(torn, 0U);
  EXPECT_GT(highest_seen, static_cast<std::uint64_t>(kRounds));
}

}  // namespace
