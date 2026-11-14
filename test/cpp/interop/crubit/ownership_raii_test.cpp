#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "crubit/rs_ownership_raii.h"
#include "interop/crubit/ownership_and_raii.hpp"
#include "interop_test_harness.hpp"
#include "support/rs_std/str_ref.h"

auto main() -> int {
  interop::crubit::test::TestContext ctx;

  // Verify C++ LeaseGuard RAII counter across construction, move-ctor, and
  // move-assign.
  INTEROP_EXPECT_EQ(ctx, interop::crubit::LeaseGuard::active_lease_count(), 0);
  {
    interop::crubit::LeaseGuard guard(7);
    INTEROP_EXPECT_EQ(ctx, interop::crubit::LeaseGuard::active_lease_count(),
                      1);
    INTEROP_EXPECT_EQ(ctx, guard.renew(2), 3);

    interop::crubit::LeaseGuard moved_guard(std::move(guard));
    INTEROP_EXPECT_EQ(ctx, interop::crubit::LeaseGuard::active_lease_count(),
                      1);
    INTEROP_EXPECT_EQ(ctx, moved_guard.lease_id(), 7);
    INTEROP_EXPECT_EQ(ctx, moved_guard.epochs_remaining(), 3);

    interop::crubit::LeaseGuard second_guard(99);
    INTEROP_EXPECT_EQ(ctx, interop::crubit::LeaseGuard::active_lease_count(),
                      2);
    second_guard = std::move(moved_guard);
    INTEROP_EXPECT_EQ(ctx, interop::crubit::LeaseGuard::active_lease_count(),
                      1);
    INTEROP_EXPECT_EQ(ctx, second_guard.lease_id(), 7);
  }
  INTEROP_EXPECT_EQ(ctx, interop::crubit::LeaseGuard::active_lease_count(), 0);

  // Verify Rust TelemetryPacketBuffer RAII Drop counter via stack &
  // std::unique_ptr.
  INTEROP_EXPECT_EQ(ctx, rs_ownership_raii::live_packet_buffers(),
                    static_cast<size_t>(0));
  {
    auto buf = rs_ownership_raii::TelemetryPacketBuffer::create(
        rs::StrRef{"imu_primary"}, 16);
    INTEROP_EXPECT_EQ(ctx, rs_ownership_raii::live_packet_buffers(),
                      static_cast<size_t>(1));
    INTEROP_EXPECT_EQ(ctx, buf.label_len(), static_cast<size_t>(11));
    INTEROP_EXPECT_EQ(ctx, buf.append_byte(0xAAU), static_cast<size_t>(1));
    INTEROP_EXPECT_EQ(ctx, buf.append_byte(0x55U), static_cast<size_t>(2));
    INTEROP_EXPECT_EQ(ctx, buf.checksum(), static_cast<uint32_t>(0xFFU));

    // TelemetryPacketBuffer has deleted C++ move ctor, so std::make_unique
    // cannot be used.
    std::unique_ptr<rs_ownership_raii::TelemetryPacketBuffer> heap_buf(
        new rs_ownership_raii::TelemetryPacketBuffer(
            rs_ownership_raii::TelemetryPacketBuffer::create(rs::StrRef{"aux"},
                                                             8)));
    INTEROP_EXPECT_EQ(ctx, rs_ownership_raii::live_packet_buffers(),
                      static_cast<size_t>(2));
    INTEROP_EXPECT_EQ(ctx, heap_buf->label_len(), static_cast<size_t>(3));
    heap_buf.reset();
    INTEROP_EXPECT_EQ(ctx, rs_ownership_raii::live_packet_buffers(),
                      static_cast<size_t>(1));
  }
  INTEROP_EXPECT_EQ(ctx, rs_ownership_raii::live_packet_buffers(),
                    static_cast<size_t>(0));

  return ctx.exit_code();
}
