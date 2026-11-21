#include "leet_code/kth_largest_element_in_a_stream.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace leet_code {

KthLargestStream::KthLargestStream(size_t k) noexcept : k_(k) {
}

auto KthLargestStream::create(int k, std::span<const int> nums)
    -> std::expected<KthLargestStream, KthLargestError> {
  static_cast<void>(k);
  static_cast<void>(nums);
  return std::unexpected(KthLargestError::kNotImplemented);
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): TDD stub.
auto KthLargestStream::add(int val) -> int {
  static_cast<void>(val);
  return 0;
}

auto KthLargestStream::k() const noexcept -> size_t {
  return k_;
}

auto KthLargestStream::size() const noexcept -> size_t {
  return heap_.size();
}

KthLargest::KthLargest(int k, std::vector<int>& nums) {
  auto stream = KthLargestStream::create(k, nums);
  if (stream.has_value()) {
    stream_.emplace(*std::move(stream));
  }
}

auto KthLargest::add(int val) -> int {
  if (stream_.has_value()) {
    return stream_->add(val);
  }
  return 0;
}

}  // namespace leet_code
