/// @file shared_region.cpp
/// @brief Anonymous shared mapping used by `SharedRegion<T>`.

#if defined(SYSTEMS_POSIX_SUPPORTED) &&                                       \
    ((defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) || \
     (defined(__APPLE__) && defined(__aarch64__)))

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

#else
#error \
    "shared_region.cpp requires a supported POSIX target (Linux x86_64/aarch64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
