/// @file shm_pubsub_posix.cpp
/// @brief POSIX OS adapter for the shared-memory pub/sub channel (stubs).
///
/// The only translation unit of this component allowed to call the operating
/// system (SHMPS-REQ-021). Porting to another OS means rewriting this file.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

#include "systems/shm_pubsub.hpp"

namespace systems::detail {

SharedMemoryRegion::~SharedMemoryRegion() {
  // TODO(SHMPS-REQ-019): munmap(base_, size_) when base_ != nullptr.
  static_cast<void>(base_);
  static_cast<void>(size_);
}

auto SharedMemoryRegion::create(std::string_view name,
                                std::size_t bytes) noexcept
    -> std::expected<SharedMemoryRegion, ShmPubSubError> {
  // TODO(SHMPS-REQ-003): copy `name` into a NUL-terminated std::array<char>
  //   (no heap), shm_open(O_CREAT | O_EXCL | O_RDWR, 0600); EEXIST ->
  //   kAlreadyExists.
  // TODO(SHMPS-REQ-001): ftruncate to `bytes`, mmap(PROT_READ | PROT_WRITE,
  //   MAP_SHARED), close the descriptor; on failure shm_unlink what you
  //   created and return kOsError.
  static_cast<void>(name);
  static_cast<void>(bytes);
  return std::unexpected(ShmPubSubError::kNotImplemented);
}

auto SharedMemoryRegion::open(std::string_view name) noexcept
    -> std::expected<SharedMemoryRegion, ShmPubSubError> {
  // TODO(SHMPS-REQ-004): shm_open(O_RDWR) (ENOENT -> kNotFound), fstat for
  //   the size, mmap, close the descriptor.
  static_cast<void>(name);
  return std::unexpected(ShmPubSubError::kNotImplemented);
}

auto SharedMemoryRegion::unlink(std::string_view name) noexcept
    -> std::expected<void, ShmPubSubError> {
  // TODO(SHMPS-REQ-006): shm_unlink; ENOENT -> kNotFound.
  static_cast<void>(name);
  return std::unexpected(ShmPubSubError::kNotImplemented);
}

auto SharedMemoryRegion::process_alive(std::int64_t pid) noexcept -> bool {
  // TODO(SHMPS-REQ-020): kill(pid, 0) == 0 || errno == EPERM.
  static_cast<void>(pid);
  return false;
}

auto SharedMemoryRegion::current_process_id() noexcept -> std::int64_t {
  // TODO(SHMPS-REQ-007): return getpid().
  return 0;
}

}  // namespace systems::detail
