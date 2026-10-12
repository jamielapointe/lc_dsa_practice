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
#include <sanitizer/asan_interface.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fstream>
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
using systems::ShmReadMode;
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
  // The subscriber is 20 samples behind an 8-slot ring: it was lapped. A
  // lapped reader jumps to the NEWEST sample (latest data wins) and is told
  // it skipped 1..19.
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto first = subscriber->try_receive(buffer);
  ASSERT_TRUE(first.has_value()) << error_text(first.error());
  EXPECT_EQ(first->sequence, kPublished);
  EXPECT_EQ(first->missed, kPublished - 1);
  EXPECT_EQ(read_tag(buffer), kPublished);
  const auto none = subscriber->try_receive(buffer);
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error(), ShmPubSubError::kWouldBlock);

  // Back in step: the next samples arrive in order with nothing missed.
  for (std::uint64_t seq = kPublished + 1; seq <= kPublished + 3; ++seq) {
    ASSERT_TRUE(publisher->publish(make_payload(seq, 16)).has_value());
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

TEST(ShmPubSubTest, InOrderModeIsDefault) {
  RecordProperty("requirements", "SHMPS-REQ-027,SHMPS-REQ-010");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto implicit_mode = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(implicit_mode.has_value()) << error_text(implicit_mode.error());
  auto explicit_mode = ShmSubscriber::attach(*channel, ShmReadMode::kInOrder);
  ASSERT_TRUE(explicit_mode.has_value()) << error_text(explicit_mode.error());

  constexpr std::uint64_t kPublished = 5;  // fewer than slot_count: no lap
  for (std::uint64_t seq = 1; seq <= kPublished; ++seq) {
    ASSERT_TRUE(publisher->publish(make_payload(seq, 16)).has_value());
  }
  std::vector<std::byte> buffer(kSmall.slot_size);
  for (ShmSubscriber* subscriber : {&*implicit_mode, &*explicit_mode}) {
    for (std::uint64_t seq = 1; seq <= kPublished; ++seq) {
      const auto sample = subscriber->try_receive(buffer);
      ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
      EXPECT_EQ(sample->sequence, seq);
      EXPECT_EQ(sample->missed, 0U);
      EXPECT_EQ(read_tag(buffer), seq);
    }
    const auto none = subscriber->try_receive(buffer);
    ASSERT_FALSE(none.has_value());
    EXPECT_EQ(none.error(), ShmPubSubError::kWouldBlock);
  }
}

TEST(ShmPubSubTest, LatestModeAlwaysReturnsNewest) {
  RecordProperty("requirements", "SHMPS-REQ-027,SHMPS-REQ-011");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel, ShmReadMode::kLatest);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  std::uint64_t next = 1;
  const auto publish_through = [&](std::uint64_t last) {
    for (; next <= last; ++next) {
      ASSERT_TRUE(publisher->publish(make_payload(next, 16)).has_value());
    }
  };
  std::vector<std::byte> buffer(kSmall.slot_size);
  const auto expect_newest = [&](std::uint64_t sequence, std::uint64_t missed) {
    const auto sample = subscriber->try_receive(buffer);
    ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
    EXPECT_EQ(sample->sequence, sequence);
    EXPECT_EQ(sample->missed, missed);
    EXPECT_EQ(read_tag(buffer), sequence);
  };

  publish_through(5);  // not lapped, yet kLatest still skips 1..4
  expect_newest(5, 4);
  const auto none = subscriber->try_receive(buffer);
  ASSERT_FALSE(none.has_value());
  EXPECT_EQ(none.error(), ShmPubSubError::kWouldBlock);
  publish_through(6);  // exactly one new sample: nothing skipped
  expect_newest(6, 0);
  publish_through(8);  // two new samples: 7 is skipped
  expect_newest(8, 1);
  publish_through(30);  // lapped several times: 9..29 skipped
  expect_newest(30, 21);
}

TEST(ShmPubSubTest, ReceiveForWithHugeTimeoutStillWakesOnClose) {
  RecordProperty("requirements", "SHMPS-REQ-013,SHMPS-REQ-014");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  // `now() + nanoseconds::max()` overflows; the deadline must saturate
  // instead of wrapping into the past (which would time out at once).
  std::vector<std::byte> buffer(kSmall.slot_size);
  std::jthread closer{[&] {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    channel->close();
  }};
  const auto start = Clock::now();
  const auto result =
      subscriber->receive_for(buffer, std::chrono::nanoseconds::max());
  const auto elapsed = Clock::now() - start;
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ShmPubSubError::kClosed);
  EXPECT_GE(elapsed, std::chrono::milliseconds{40});
  EXPECT_LT(elapsed, std::chrono::seconds{2});
}

TEST(ShmPubSubTest, MovedFromHandlesReportInvalidArgument) {
  RecordProperty("requirements", "SHMPS-REQ-019");
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  auto subscriber = ShmSubscriber::attach(*channel);
  ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());

  ShmChannel live_channel{std::move(*channel)};
  ShmPublisher live_publisher{std::move(*publisher)};
  ShmSubscriber live_subscriber{std::move(*subscriber)};
  std::vector<std::byte> buffer(kSmall.slot_size);

  // NOLINTBEGIN(bugprone-use-after-move,hicpp-invalid-access-moved): the
  // documented moved-from behavior is exactly what this test checks.
  const auto published = publisher->publish(make_payload(1, 8));
  ASSERT_FALSE(published.has_value());
  EXPECT_EQ(published.error(), ShmPubSubError::kInvalidArgument);
  const auto received = subscriber->try_receive(buffer);
  ASSERT_FALSE(received.has_value());
  EXPECT_EQ(received.error(), ShmPubSubError::kInvalidArgument);
  const auto timed =
      subscriber->receive_for(buffer, std::chrono::milliseconds{1});
  ASSERT_FALSE(timed.has_value());
  EXPECT_EQ(timed.error(), ShmPubSubError::kInvalidArgument);
  EXPECT_EQ(channel->config(), ShmPubSubConfig{});
  EXPECT_FALSE(channel->is_closed());
  channel->close();  // harmless no-op on a moved-from handle
  const auto no_publisher = ShmPublisher::attach(*channel);
  ASSERT_FALSE(no_publisher.has_value());
  EXPECT_EQ(no_publisher.error(), ShmPubSubError::kInvalidArgument);
  const auto no_subscriber = ShmSubscriber::attach(*channel);
  ASSERT_FALSE(no_subscriber.has_value());
  EXPECT_EQ(no_subscriber.error(), ShmPubSubError::kInvalidArgument);
  // NOLINTEND(bugprone-use-after-move,hicpp-invalid-access-moved)

  // The moved-to handles carry on as if nothing happened.
  EXPECT_FALSE(live_channel.is_closed());
  const auto sequence = live_publisher.publish(make_payload(1, 8));
  ASSERT_TRUE(sequence.has_value()) << error_text(sequence.error());
  EXPECT_EQ(*sequence, 1U);
  const auto sample = live_subscriber.try_receive(buffer);
  ASSERT_TRUE(sample.has_value()) << error_text(sample.error());
  EXPECT_EQ(sample->sequence, 1U);
}

// ============================================ Tier 3b: memory residency

/// KiB of this process's memory locked into RAM (`VmLck` in
/// /proc/self/status), or -1 if the field is unavailable.
auto locked_kib() -> std::int64_t {
  std::ifstream status{"/proc/self/status"};
  std::string token;
  while (status >> token) {
    if (token == "VmLck:") {
      std::int64_t kib = -1;
      status >> kib;
      return kib;
    }
  }
  return -1;
}

/// Page faults (minor + major) taken so far by the calling thread, or -1 if
/// the platform has no per-thread accounting.
auto page_faults_this_thread() -> std::int64_t {
#if defined(RUSAGE_THREAD)
  rusage usage{};
  if (::getrusage(RUSAGE_THREAD, &usage) != 0) {
    return -1;
  }
  return static_cast<std::int64_t>(usage.ru_minflt) +
         static_cast<std::int64_t>(usage.ru_majflt);
#else
  return -1;
#endif
}

/// Sanitizer detection. ASan and TSan intercept `mlock()` / `mlockall()` and
/// turn them into no-ops that report success (locking would also pin their
/// terabyte-sized shadow mappings), so tests that observe real locking run
/// only in non-sanitizer and UBSan builds. TSan also maps its shadow memory
/// lazily, so instrumented accesses can fault on TSan's own pages even when
/// the ring itself is resident.
#if defined(__has_feature)
constexpr bool kUnderAddressSanitizer = __has_feature(address_sanitizer);
constexpr bool kUnderThreadSanitizer = __has_feature(thread_sanitizer);
#else
constexpr bool kUnderAddressSanitizer = false;
constexpr bool kUnderThreadSanitizer = false;
#endif
constexpr bool kMlockIsIntercepted =
    kUnderAddressSanitizer || kUnderThreadSanitizer;

/// Moves this thread's stack frames off ASan's "fake stack" while alive. A
/// no-op in builds without ASan.
///
/// ASan's use-after-return detection (on by default in current clang) places
/// every frame that has an address-taken local on a separate fake stack and
/// hands out the NEXT fake frame on every call, walking through megabytes of
/// lazily mapped memory. Each first touch of a new fake-stack page is a page
/// fault taken by the ASan runtime itself, about 50 of them over two laps,
/// which would hide what the residency test measures (faults on the ring).
/// Inside this scope frames use the real stack, still with ASan redzones, so
/// every other ASan check stays active.
class RealStackFramesScope {
 public:
  RealStackFramesScope() noexcept {
    if constexpr (kUnderAddressSanitizer) {
      __asan_suppress_fake_stack();
    }
  }
  ~RealStackFramesScope() {
    if constexpr (kUnderAddressSanitizer) {
      __asan_unsuppress_fake_stack();
    }
  }
  RealStackFramesScope(const RealStackFramesScope&) = delete;
  RealStackFramesScope(RealStackFramesScope&&) = delete;
  auto operator=(const RealStackFramesScope&) -> RealStackFramesScope& = delete;
  auto operator=(RealStackFramesScope&&) -> RealStackFramesScope& = delete;
};

/// Outcome of one `measure_laps` run.
struct LapResult {
  int delivered{0};        ///< Iterations where publish AND receive succeeded.
  std::int64_t faults{0};  ///< Page faults this thread took during the loop.
};

/// Publishes and receives `laps` samples and counts the calling thread's page
/// faults around exactly that loop.
///
/// Kept out of line on purpose, with the fault counter read inside: the
/// warm-up and every measured run then execute the very same machine code. As
/// an inlined lambda, each call site had its own copy of the loop, and the
/// first run of a measured copy could fault in a not-yet-mapped page of the
/// test executable's code. That fault has nothing to do with the ring, and
/// whether it happened depended on where ASLR placed the executable, so the
/// test failed intermittently.
[[gnu::noinline]] auto measure_laps(ShmPublisher& publisher,
                                    ShmSubscriber& subscriber,
                                    std::span<const std::byte> payload,
                                    std::span<std::byte> buffer, int laps)
    -> LapResult {
  const RealStackFramesScope real_stack;
  const auto before = page_faults_this_thread();
  int delivered = 0;
  for (int i = 0; i < laps; ++i) {
    const bool sent = publisher.publish(payload).has_value();
    const bool got = subscriber.try_receive(buffer).has_value();
    delivered += (sent && got) ? 1 : 0;
  }
  return {.delivered = delivered, .faults = page_faults_this_thread() - before};
}

// Geometry whose ring spans many pages: 64 slots x (4096 B payload +
// header) is a little over 256 KiB.
constexpr ShmPubSubConfig kPaged{.slot_count = 64, .slot_size = 4096};
constexpr std::int64_t kPagedRingKib = 256;

TEST(ShmPubSubResidencyTest, CreateAndOpenLockTheMapping) {
  RecordProperty("requirements", "SHMPS-REQ-023");
  if constexpr (!systems::platform::kIsLinux) {
    GTEST_SKIP() << "VmLck accounting is Linux-specific";
  }
  if constexpr (kMlockIsIntercepted) {
    GTEST_SKIP() << "ASan/TSan turn mlock() into a no-op";
  }
  const auto baseline = locked_kib();
  ASSERT_GE(baseline, 0) << "VmLck missing from /proc/self/status";
  const TestName name;
  {
    auto created = ShmChannel::create(name.str(), kPaged);
    ASSERT_TRUE(created.has_value()) << error_text(created.error());
    const auto after_create = locked_kib();
    EXPECT_GE(after_create - baseline, kPagedRingKib);

    // Each mapping is locked separately, so a second handle in the same
    // process locks the ring again (as a separate subscriber process would).
    auto opened = ShmChannel::open(name.str());
    ASSERT_TRUE(opened.has_value()) << error_text(opened.error());
    EXPECT_GE(locked_kib() - after_create, kPagedRingKib);
  }
  // munmap drops the locks: nothing stays pinned after the handles go.
  EXPECT_EQ(locked_kib(), baseline);
}

TEST(ShmPubSubResidencyTest, PublishAndReceiveTakeNoPageFaults) {
  RecordProperty("requirements", "SHMPS-REQ-023");
  if (kUnderThreadSanitizer) {
    GTEST_SKIP() << "TSan's lazily mapped shadow memory adds page faults";
  }
  if (page_faults_this_thread() < 0) {
    GTEST_SKIP() << "no per-thread page-fault accounting (RUSAGE_THREAD)";
  }
  const auto payload = make_payload(7, kPaged.slot_size);
  std::vector<std::byte> buffer(kPaged.slot_size);  // zero-filled: resident

  // Control: prove the probe can see faults at all. A plain shared mapping
  // without pre-faulting takes a fault on the first touch of each page.
  {
    constexpr std::size_t kControlBytes = std::size_t{64} * 4096;
    const TestName raw;
    // NOLINTBEGIN(*-vararg): POSIX shm_open is variadic.
    const int fd =
        ::shm_open(raw.str().c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    // NOLINTEND(*-vararg)
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::ftruncate(fd, static_cast<off_t>(kControlBytes)), 0);
    void* base = ::mmap(nullptr, kControlBytes, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    // NOLINTNEXTLINE(*-cstyle-cast,performance-no-int-to-ptr): MAP_FAILED.
    ASSERT_NE(base, MAP_FAILED);
    const std::span bytes{static_cast<std::byte*>(base), kControlBytes};
    const auto before = page_faults_this_thread();
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4096) {
      bytes[offset] = std::byte{1};
    }
    const auto control_faults = page_faults_this_thread() - before;
    ASSERT_EQ(::munmap(base, kControlBytes), 0);
    ASSERT_EQ(::close(fd), 0);
    EXPECT_GT(control_faults, 0) << "the fault probe is not working";
  }

  // Two full laps of the ring: every slot is written and read twice.
  constexpr int kLaps = 2 * static_cast<int>(kPaged.slot_count);

  // Warm-up on a separate channel: the first calls fault in code, stack, and
  // libc pages that have nothing to do with the ring.
  {
    const TestName warm;
    auto channel = ShmChannel::create(warm.str(), kPaged);
    ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
    auto publisher = ShmPublisher::attach(*channel);
    ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
    auto subscriber = ShmSubscriber::attach(*channel);
    ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
    EXPECT_EQ(
        measure_laps(*publisher, *subscriber, payload, buffer, kLaps).delivered,
        kLaps);
  }

  const TestName name;
  auto created = ShmChannel::create(name.str(), kPaged);
  ASSERT_TRUE(created.has_value()) << error_text(created.error());
  {
    // A freshly created ring: create() wrote every page, so the very first
    // publish into each slot finds it resident.
    auto publisher = ShmPublisher::attach(*created);
    ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
    auto subscriber = ShmSubscriber::attach(*created);
    ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
    const LapResult result =
        measure_laps(*publisher, *subscriber, payload, buffer, kLaps);
    EXPECT_EQ(result.delivered, kLaps);
    EXPECT_EQ(result.faults, 0) << "page faults on a create()d channel";
  }
  {
    // A second mapping of the same ring via open(): MAP_POPULATE + mlock +
    // read-touch must have filled this mapping's page-table entries too.
    auto opened = ShmChannel::open(name.str());
    ASSERT_TRUE(opened.has_value()) << error_text(opened.error());
    auto publisher = ShmPublisher::attach(*opened);
    ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
    auto subscriber = ShmSubscriber::attach(*opened);
    ASSERT_TRUE(subscriber.has_value()) << error_text(subscriber.error());
    const LapResult result =
        measure_laps(*publisher, *subscriber, payload, buffer, kLaps);
    EXPECT_EQ(result.delivered, kLaps);
    EXPECT_EQ(result.faults, 0) << "page faults on an open()ed channel";
  }
}

TEST(ShmPubSubResidencyTest, MemoryLockFailureIsFatal) {
  RecordProperty("requirements", "SHMPS-REQ-024");
  if constexpr (kMlockIsIntercepted) {
    GTEST_SKIP() << "ASan/TSan turn mlock() into a no-op that never fails";
  }
  if (::geteuid() == 0) {
    GTEST_SKIP() << "root bypasses RLIMIT_MEMLOCK (CAP_IPC_LOCK)";
  }
  const TestName existing;
  auto channel = ShmChannel::create(existing.str(), kSmall);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  const TestName fresh;

  // The rlimit is per process, so the experiment runs in a child.
  auto child = systems_test::ChildProcess::spawn([&] -> int {
    const rlimit no_locking{.rlim_cur = 0, .rlim_max = 0};
    if (::setrlimit(RLIMIT_MEMLOCK, &no_locking) != 0) {
      return 1;
    }
    const auto created = ShmChannel::create(fresh.str(), kSmall);
    if (created.has_value() ||
        created.error() != ShmPubSubError::kMemoryLockFailed) {
      return 2;
    }
    if (systems_test::shm_exists(fresh.str())) {
      return 3;  // a failed create() must leave no object behind
    }
    const auto opened = ShmChannel::open(existing.str());
    if (opened.has_value() ||
        opened.error() != ShmPubSubError::kMemoryLockFailed) {
      return 4;
    }
    return systems_test::kChildOk;
  });
  const auto result = child.wait_for(kChildWait);
  EXPECT_TRUE(result.succeeded()) << result;
  EXPECT_FALSE(systems_test::shm_exists(fresh.str()));
  // A failed open() must never unlink a channel that someone else owns.
  EXPECT_TRUE(systems_test::shm_exists(existing.str()));
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

// kLatest readers jump straight to the newest slot, which on a 4-slot ring
// is very often the slot the publisher is about to overwrite next. This is
// the worst case for the seqlock's torn-read detection.
TEST(ShmPubSubConcurrencyTest, LatestReadersNeverSeeTornSamples) {
  RecordProperty("requirements", "SHMPS-REQ-015,SHMPS-REQ-026,SHMPS-REQ-027");
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
    auto subscriber = ShmSubscriber::attach(*channel, ShmReadMode::kLatest);
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
            break;
          }
          received.fetch_add(1);
          const auto bytes = std::span{buffer}.first(sample->size);
          if (!payload_is_consistent(bytes) ||
              read_tag(bytes) != sample->sequence) {
            torn.fetch_add(1);
          }
          // Strictly increasing, and `missed` accounts for every gap.
          if (sample->sequence <= previous ||
              sample->sequence != previous + 1 + sample->missed) {
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

// A subscriber process frozen with SIGSTOP (as if preempted forever, or
// stopped in a debugger) must not slow the publisher down: the publisher
// never reads subscriber state, so it has nothing to wait for. After SIGCONT
// the subscriber resynchronizes to the newest sample on its own.
TEST(ShmPubSubMultiProcessTest,
     PublisherProgressesWhileSubscriberProcessIsStopped) {
  RecordProperty("requirements", "SHMPS-REQ-025,SHMPS-REQ-016");
  constexpr ShmPubSubConfig kRing{.slot_count = 64, .slot_size = 64};
  constexpr std::uint64_t kWhileStopped = 50'000;
  constexpr std::uint64_t kWarmupSamples = 10;
  const TestName name;
  auto channel = ShmChannel::create(name.str(), kRing);
  ASSERT_TRUE(channel.has_value()) << error_text(channel.error());
  auto publisher = ShmPublisher::attach(*channel);
  ASSERT_TRUE(publisher.has_value()) << error_text(publisher.error());
  systems_test::SharedRegion<ProcessTally> tally;
  ASSERT_TRUE(tally.valid());

  auto child = systems_test::ChildProcess::spawn([&] {
    return child_subscriber(name.str(), *tally,
                            std::numeric_limits<std::uint64_t>::max());
  });
  ASSERT_TRUE(systems_test::wait_until([&] { return tally->ready.load() == 1; },
                                       std::chrono::seconds{5}));

  // Feed the child until it is demonstrably mid-stream.
  std::uint64_t seq = 0;
  const auto feed_deadline = Clock::now() + std::chrono::seconds{5};
  while (tally->received.load() < kWarmupSamples &&
         Clock::now() < feed_deadline) {
    ++seq;
    ASSERT_TRUE(publisher->publish(make_payload(seq, 32)).has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  ASSERT_GE(tally->received.load(), kWarmupSamples);

  // Freeze the subscriber wherever it happens to be (possibly in the middle
  // of reading a slot) and lap the ring hundreds of times.
  ASSERT_TRUE(child.send_signal(SIGSTOP));
  std::uint64_t failures = 0;
  const auto start = Clock::now();
  for (std::uint64_t i = 0; i < kWhileStopped; ++i) {
    ++seq;
    failures += publisher->publish(make_payload(seq, 32)).has_value() ? 0 : 1;
  }
  const auto elapsed = Clock::now() - start;
  ASSERT_TRUE(child.send_signal(SIGCONT));
  EXPECT_EQ(failures, 0U);
  // ~50k small copies take a few ms; 5 s only fails if publish waited.
  EXPECT_LT(elapsed, std::chrono::seconds{5});

  // The resumed subscriber skips what it missed and reaches the newest.
  ++seq;
  ASSERT_TRUE(publisher->publish(make_payload(seq, 32)).has_value());
  EXPECT_TRUE(systems_test::wait_until(
      [&] { return tally->last_sequence.load() == seq; },
      std::chrono::seconds{5}));
  channel->close();
  const auto result = child.wait_for(kChildWait);
  EXPECT_TRUE(result.succeeded()) << result;
  EXPECT_EQ(tally->torn.load(), 0U);
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
