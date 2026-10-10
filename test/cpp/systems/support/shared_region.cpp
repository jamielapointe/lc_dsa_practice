/// @file shared_region.cpp
/// @brief Anonymous shared mapping used by `SharedRegion<T>`.

#include "systems_test/shared_region.hpp"

#include <sys/mman.h>

#include <cstddef>

namespace systems_test::detail {

AnonymousSharedMapping::AnonymousSharedMapping(std::size_t bytes) noexcept
    : bytes_{bytes == 0 ? 1 : bytes} {
  void* const base = ::mmap(nullptr, bytes_, PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANON, -1, 0);
  // NOLINTNEXTLINE(*-pro-type-cstyle-cast,performance-no-int-to-ptr): POSIX.
  if (base != MAP_FAILED) {
    base_ = base;
  }
}

AnonymousSharedMapping::~AnonymousSharedMapping() {
  if (base_ != nullptr) {
    static_cast<void>(::munmap(base_, bytes_));
  }
}

}  // namespace systems_test::detail
