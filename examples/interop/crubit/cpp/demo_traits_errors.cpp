#include <optional>
#include <print>

#include "crubit/rs_traits_errors.h"
#include "interop/crubit/traits_and_errors.hpp"
#include "interop/crubit/traits_and_errors_expected.hpp"
#include "support/rs_std/result.h"
#include "support/rs_std/str_ref.h"

auto main() -> int {
  const auto cal = interop::crubit::to_expected(
      interop::crubit::calibrate_pd_controller(5.0, 0.707));
  if (!cal.has_value()) {
    return 1;
  }

  const auto parsed = rs_traits_errors::parse_schedule_id(rs::StrRef{"7:2"});
  if (!parsed.has_value()) {
    return 1;
  }
  const auto& schedule = *parsed;

  const std::optional<double> nominal =
      rs_traits_errors::lookup_nominal_gain(schedule.profile_slot);
  if (!nominal.has_value()) {
    return 1;
  }

  std::println(
      "[CRUBIT_CPP_DEMO] module=traits_errors status=ok schedule={}:{} "
      "nominal_gain={} kp={}",
      schedule.controller_id, schedule.profile_slot, *nominal, cal->kp);
  return 0;
}
