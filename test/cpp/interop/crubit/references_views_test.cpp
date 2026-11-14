#include <array>
#include <span>

#include "crubit/rs_references_views.h"
#include "interop/crubit/references_and_views.hpp"
#include "interop_test_harness.hpp"
#include "support/rs_std/slice_ref.h"
#include "support/rs_std/str_ref.h"

auto main() -> int {
  interop::crubit::test::TestContext ctx;

  // Test C++ SignalWindow & references.
  interop::crubit::SignalWindow win_a(1.5);
  win_a.push_sample(4.5);
  interop::crubit::SignalWindow win_b(2.0);
  const auto& dominant = interop::crubit::select_dominant_window(win_a, win_b);
  INTEROP_EXPECT_NEAR(ctx, dominant.peak_ref(), 4.5, 1e-9);

  // Test Rust slice views (rs::SliceRef<const double> and
  // rs::SliceRef<double>).
  std::array<double, 4> samples{3.0, -3.0, 3.0, -3.0};
  const rs::SliceRef<const double> const_view{std::span<const double>{samples}};
  INTEROP_EXPECT_NEAR(ctx, rs_references_views::rms_slice(const_view), 3.0,
                      1e-9);

  const rs::SliceRef<const double> empty_view{};
  INTEROP_EXPECT_NEAR(ctx, rs_references_views::rms_slice(empty_view), 0.0,
                      1e-9);

  const rs::SliceRef<double> mut_view{std::span<double>{samples}};
  rs_references_views::normalize_slice_in_place(mut_view, 0.5);
  INTEROP_EXPECT_NEAR(ctx, samples[0], 0.5, 1e-9);
  INTEROP_EXPECT_NEAR(ctx, samples[1], -0.5, 1e-9);

  // Test Rust &str view via rs::StrRef.
  INTEROP_EXPECT_EQ(
      ctx, rs_references_views::parse_sensor_tag_id(rs::StrRef{"TAG-42"}), 42);
  INTEROP_EXPECT_EQ(
      ctx, rs_references_views::parse_sensor_tag_id(rs::StrRef{"BAD-99"}), -1);
  INTEROP_EXPECT_EQ(
      ctx, rs_references_views::parse_sensor_tag_id(rs::StrRef{"TAG-"}), -1);

  // Test Rust RingCursor methods.
  auto cursor = rs_references_views::RingCursor::create(5);
  INTEROP_EXPECT_EQ(ctx, cursor.index(),
                    static_cast<decltype(cursor.index())>(0));
  INTEROP_EXPECT_EQ(ctx, cursor.advance(3),
                    static_cast<decltype(cursor.index())>(3));
  INTEROP_EXPECT_EQ(ctx, cursor.advance(4),
                    static_cast<decltype(cursor.index())>(2));
  INTEROP_EXPECT_EQ(ctx, cursor.index(),
                    static_cast<decltype(cursor.index())>(2));

  return ctx.exit_code();
}
