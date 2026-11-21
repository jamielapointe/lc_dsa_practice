#include "leet_code/summary_ranges.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
#include <functional>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace leet_code {

[[nodiscard]] static auto format_range(int first, int end) -> std::string {
  constexpr size_t kMaxIntChars =
      static_cast<size_t>(std::numeric_limits<int>::digits10) + 2uz;
  constexpr size_t kBufferSize = 32uz;
  static_assert(kBufferSize >= (2uz * kMaxIntChars) + 2uz);

  std::array<char, kBufferSize> buffer{};
  char* const begin = buffer.data();
  char* const limit = begin + buffer.size();

  char* out = std::to_chars(begin, limit, first).ptr;
  if (first != end) {
    *out = '-';
    ++out;
    *out = '>';
    ++out;
    out = std::to_chars(out, limit, end).ptr;
  }
  return {begin, static_cast<size_t>(out - begin)};
}

[[nodiscard]] auto summary_ranges(std::span<const int> nums)
    -> std::expected<std::vector<std::string>, SummaryRangesError> {
  if (nums.empty()) {
    return std::vector<std::string>{};
  }
  if (!std::ranges::is_sorted(nums, std::less_equal<int>{})) {
    return std::unexpected(SummaryRangesError::kUnsortedOrDuplicateInput);
  }

  int first_num = *nums.begin();
  int prev_num = first_num;
  auto rest = nums | std::views::drop(1);
  std::vector<std::string> result;
  for (int num : rest) {
    if (num != prev_num + 1) {
      result.push_back(format_range(first_num, prev_num));
      first_num = num;
    }
    prev_num = num;
  }
  result.push_back(format_range(first_num, prev_num));
  return result;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static): LeetCode API.
auto Solution::summaryRanges(std::vector<int>& nums)
    -> std::vector<std::string> {
  auto result = summary_ranges(nums);
  if (result.has_value()) {
    return *std::move(result);
  }
  return {};
}

}  // namespace leet_code
