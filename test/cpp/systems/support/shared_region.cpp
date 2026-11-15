/// @file shared_region.cpp
/// @brief Anonymous shared mapping used by `SharedRegion<T>`.

#if defined(SYSTEMS_POSIX_SUPPORTED)

#include "systems_test/shared_region.hpp"

#include <sys/mman.h>

#include <cstddef>

#include "systems/platform.hpp"

static_assert(systems::platform::kIsSupportedTarget,
              "shared_region.cpp compiled on an unsupported target");

namespace systems_test::detail {

AnonymousSharedMapping::AnonymousSharedMapping(size_t bytes) noexcept
    : bytes_{bytes == 0uz ? 1uz : bytes} {
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

#else
#error \
    "shared_region.cpp requires a supported POSIX target (Linux x86_64/aarch64/riscv64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
