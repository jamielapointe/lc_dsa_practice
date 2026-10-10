/**
 * @file bounded_fifo.cpp
 * @brief Explicit instantiations of systems::BoundedFifo so the template is
 * always compiled and linted.
 */

#include "systems/bounded_fifo.hpp"

#include <cstddef>
#include <cstdint>

namespace systems {

// Common instantiations used by the tests; keeps the template compiling under
// -Werror on every build.
template class BoundedFifo<int, 16>;
template class BoundedFifo<std::uint8_t, 64>;

}  // namespace systems
