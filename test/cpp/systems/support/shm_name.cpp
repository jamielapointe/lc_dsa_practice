/// @file shm_name.cpp
/// @brief Unique shared-memory names and RAII unlink for tests.

#if defined(SYSTEMS_POSIX_SUPPORTED) &&                                       \
    ((defined(__linux__) && (defined(__x86_64__) || defined(__aarch64__))) || \
     (defined(__APPLE__) && defined(__aarch64__)))

#include "systems_test/shm_name.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "systems/platform.hpp"

namespace systems_test {

auto unique_shm_name(std::string_view prefix) -> std::string {
  static std::atomic<unsigned> counter{0};
  constexpr std::size_t kMaxPrefix = 8;

  std::string clean;
  for (const char c : prefix.substr(0, kMaxPrefix)) {
    const auto uc = static_cast<unsigned char>(c);
    clean.push_back((std::isalnum(uc) != 0 || c == '_') ? c : '_');
  }
  std::string name =
      std::format("/{}_{:x}_{:x}", clean, static_cast<unsigned>(::getpid()),
                  counter.fetch_add(1, std::memory_order_relaxed));
  if (name.size() > systems::platform::kMaxShmNameLength) {
    name.resize(systems::platform::kMaxShmNameLength);
  }
  return name;
}

ScopedShmUnlink::ScopedShmUnlink(std::string name) : name_{std::move(name)} {
  static_cast<void>(::shm_unlink(name_.c_str()));
}

ScopedShmUnlink::~ScopedShmUnlink() {
  static_cast<void>(::shm_unlink(name_.c_str()));
}

auto shm_exists(const std::string& name) -> bool {
  // NOLINTNEXTLINE(*-vararg): POSIX shm_open is variadic.
  const int fd = ::shm_open(name.c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return false;
  }
  static_cast<void>(::close(fd));
  return true;
}

}  // namespace systems_test

#else
#error \
    "shm_name.cpp requires a supported POSIX target (Linux x86_64/aarch64 or macOS arm64) and SYSTEMS_POSIX_SUPPORTED."
#endif
