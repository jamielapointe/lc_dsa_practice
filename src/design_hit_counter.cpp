#include "lc_dsa/design_hit_counter.hpp"

namespace lc_dsa {

auto HitCounter::hit(int timestamp) -> void {
  static_cast<void>(this);
  static_cast<void>(timestamp);
}

auto HitCounter::getHits(int timestamp) -> int {
  static_cast<void>(this);
  static_cast<void>(timestamp);
  return 0;
}

}  // namespace lc_dsa
