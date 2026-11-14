#include <array>
#include <iostream>
#include <span>

#include "crubit/rs_references_views.h"
#include "interop/crubit/references_and_views.hpp"
#include "support/rs_std/slice_ref.h"
#include "support/rs_std/str_ref.h"

auto main() -> int {
  interop::crubit::SignalWindow win_a(1.0);
  win_a.push_sample(5.0);
  interop::crubit::SignalWindow win_b(3.0);
  const auto& dominant = interop::crubit::select_dominant_window(win_a, win_b);

  std::array<double, 4> samples{2.0, -2.0, 2.0, -2.0};
  const double rms_before = rs_references_views::rms_slice(
      rs::SliceRef<const double>{std::span<const double>{samples}});
  rs_references_views::normalize_slice_in_place(
      rs::SliceRef<double>{std::span<double>{samples}}, 0.5);
  const int tag_id =
      rs_references_views::parse_sensor_tag_id(rs::StrRef{"TAG-108"});

  auto cursor = rs_references_views::RingCursor::create(8);
  const auto next_idx = cursor.advance(11);

  if (tag_id != 108 || next_idx != 3) {
    return 1;
  }

  std::cout << "[CRUBIT_CPP_DEMO] module=references_views status=ok peak="
            << dominant.peak_ref() << " rms_before=" << rms_before
            << " tag_id=" << tag_id << " cursor_idx=" << next_idx << '\n';
  return 0;
}
