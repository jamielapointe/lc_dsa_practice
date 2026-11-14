#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#include "crubit/rs_traits_errors.h"
#include "interop/crubit/traits_and_errors.hpp"
#include "interop/crubit/traits_and_errors_expected.hpp"
#include "interop_test_harness.hpp"
#include "support/rs_std/result.h"
#include "support/rs_std/str_ref.h"

auto main() -> int {
  interop::crubit::test::TestContext ctx;

  // Test C++ CalibrationOutcome + std::expected companion.
  const auto ok_exp = interop::crubit::to_expected(
      interop::crubit::calibrate_pd_controller(10.0, 0.7));
  INTEROP_EXPECT_TRUE(ctx, ok_exp.has_value());
  INTEROP_EXPECT_TRUE(ctx, ok_exp->kp > 0.0);
  const auto bad_exp = interop::crubit::to_expected(
      interop::crubit::calibrate_pd_controller(-1.0, 0.7));
  INTEROP_EXPECT_TRUE(ctx, !bad_exp.has_value());
  INTEROP_EXPECT_EQ(ctx, bad_exp.error(),
                    interop::crubit::CalibrationCode::kOutOfRange);

  // Test Rust PidScheduleId traits: Default, PartialEq, Display (operator<<).
  const rs_traits_errors::PidScheduleId default_id;
  INTEROP_EXPECT_EQ(ctx, default_id.controller_id, static_cast<uint32_t>(0));
  INTEROP_EXPECT_EQ(ctx, default_id.profile_slot, static_cast<uint16_t>(0));

  const auto parsed_ok =
      rs_traits_errors::parse_schedule_id(rs::StrRef{"42:3"});
  INTEROP_EXPECT_TRUE(ctx, parsed_ok.has_value());
  const rs_traits_errors::PidScheduleId expected_id{.controller_id = 42,
                                                    .profile_slot = 3};
  INTEROP_EXPECT_TRUE(ctx, parsed_ok.value() == expected_id);

  std::ostringstream oss;
  oss << parsed_ok.value();
  INTEROP_EXPECT_EQ(ctx, oss.str(), std::string{"42:3"});

  // Test std::optional<double> from lookup_nominal_gain.
  const std::optional<double> valid_gain =
      rs_traits_errors::lookup_nominal_gain(3);
  INTEROP_EXPECT_TRUE(ctx, valid_gain.has_value());
  INTEROP_EXPECT_TRUE(ctx, *valid_gain > 0.0);

  const std::optional<double> invalid_gain =
      rs_traits_errors::lookup_nominal_gain(99);
  INTEROP_EXPECT_TRUE(ctx, !invalid_gain.has_value());

  // Test rs::Result error variants.
  const auto empty_err = rs_traits_errors::parse_schedule_id(rs::StrRef{""});
  INTEROP_EXPECT_TRUE(ctx, !empty_err.has_value());
  INTEROP_EXPECT_TRUE(
      ctx, empty_err.error() ==
               rs_traits_errors::ParseScheduleError::EmptyInput::Make());

  const auto sep_err = rs_traits_errors::parse_schedule_id(rs::StrRef{"42_3"});
  INTEROP_EXPECT_TRUE(ctx, !sep_err.has_value());
  INTEROP_EXPECT_TRUE(
      ctx,
      sep_err.error() ==
          rs_traits_errors::ParseScheduleError::MalformedSeparator::Make());

  const auto oob_err =
      rs_traits_errors::parse_schedule_id(rs::StrRef{"42:99999"});
  INTEROP_EXPECT_TRUE(ctx, !oob_err.has_value());
  INTEROP_EXPECT_TRUE(
      ctx, oob_err.error() ==
               rs_traits_errors::ParseScheduleError::OutOfBounds::Make());

  return ctx.exit_code();
}
