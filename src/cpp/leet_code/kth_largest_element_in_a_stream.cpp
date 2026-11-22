#include "leet_code/kth_largest_element_in_a_stream.hpp"

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

namespace leet_code {

KthLargestStream::KthLargestStream(size_t k) noexcept : k_(k) {
}

auto KthLargestStream::create(int k, std::span<const int> nums)
    -> std::expected<KthLargestStream, KthLargestError> {
  if (k < 1) {
    return std::unexpected(KthLargestError::kInvalidK);
  }
  auto kz = static_cast<size_t>(k);
  if (nums.size() + 1 < kz) {
    return std::unexpected(KthLargestError::kInsufficientInitialElements);
  }
  auto kth_largest_stream = KthLargestStream(kz);
  kth_largest_stream.min_heap_ =
      std::priority_queue<int, std::vector<int>, std::greater<>>{nums.begin(),
                                                                 nums.end()};
  while (kth_largest_stream.min_heap_.size() > kz) {
    kth_largest_stream.min_heap_.pop();
  }
  return kth_largest_stream;
};

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): TDD stub.
auto KthLargestStream::add(int val) -> int {
  min_heap_.push(val);
  if (min_heap_.size() > k_) {
    min_heap_.pop();
  }
  return min_heap_.top();
}

auto KthLargestStream::k() const noexcept -> size_t {
  return k_;
}

auto KthLargestStream::size() const noexcept -> size_t {
  return min_heap_.size();
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
