/// @file kth_largest_element_in_a_stream.hpp
/// @brief 703. Kth Largest Element in a Stream (Easy)
///
/// https://leetcode.com/problems/kth-largest-element-in-a-stream/
///
/// You are part of a university admissions office and need to keep track of the
/// `k`th highest test score from applicants in real-time. This helps to
/// determine cut-off marks for interviews and admissions dynamically as new
/// applicants submit their scores.
///
/// You are tasked to implement a class which, for a given integer `k`,
/// maintains a stream of test scores and continuously returns the `k`th highest
/// score after a new score has been submitted. More specifically, we are
/// looking for the `k`th highest score in the sorted list of all scores (not
/// the `k`th distinct score).
///
/// Implement the `KthLargest` class:
/// * `KthLargest(int k, int[] nums)` Initializes the object with the integer
///   `k` and the stream of test scores `nums`.
/// * `int add(int val)` Adds a new test score `val` to the stream and returns
///   the element representing the `k`th largest element in the pool of test
///   scores so far.
///
/// **Example 1:**
///
/// Input:
/// `["KthLargest", "add", "add", "add", "add", "add"]`
/// `[[3, [4, 5, 8, 2]], [3], [5], [10], [9], [4]]`
/// Output:
/// `[null, 4, 5, 5, 8, 8]`
/// Explanation:
/// `KthLargest kthLargest = new KthLargest(3, [4, 5, 8, 2]);`
/// `kthLargest.add(3);  // return 4`
/// `kthLargest.add(5);  // return 5`
/// `kthLargest.add(10); // return 5`
/// `kthLargest.add(9);  // return 8`
/// `kthLargest.add(4);  // return 8`
///
/// **Example 2:**
///
/// Input:
/// `["KthLargest", "add", "add", "add", "add"]`
/// `[[4, [7, 7, 7, 7, 8, 3]], [2], [10], [9], [9]]`
/// Output:
/// `[null, 7, 7, 7, 8]`
/// Explanation:
/// `KthLargest kthLargest = new KthLargest(4, [7, 7, 7, 7, 8, 3]);`
/// `kthLargest.add(2);  // return 7`
/// `kthLargest.add(10); // return 7`
/// `kthLargest.add(9);  // return 7`
/// `kthLargest.add(9);  // return 8`
///
/// **Constraints:**
///
/// * `0 <= nums.length <= 10^4`
/// * `1 <= k <= nums.length + 1`
/// * `-10^4 <= nums[i] <= 10^4`
/// * `-10^4 <= val <= 10^4`
/// * At most `10^4` calls will be made to `add`.
///
/// **Target Complexity:**
/// * Initialization (`create` / constructor): `O(n log k)` time (or `O(n)`
///   bounded heap construction), `O(k)` space.
/// * Per `add(val)` call: `O(log k)` time, `O(1)` auxiliary space (bounded
///   min-heap of capacity `k` with zero steady-state heap reallocation).
///
/// **Ownership & Memory Notes:**
/// * `KthLargestStream::create` accepts a non-owning `std::span<const int>`
///   view and never mutates caller storage.
/// * Retains at most `k` elements internally (`size() <= k()`).

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <queue>
#include <span>
#include <vector>

namespace leet_code {

/// @brief Error codes returned by `KthLargestStream::create`.
enum class KthLargestError : uint8_t {
  /// Requested order statistic `k` is non-positive (`k <= 0`).
  kInvalidK,
  /// Initial stream contains fewer than `k - 1` elements
  /// (`nums.size() + 1uz < static_cast<size_t>(k)`).
  kInsufficientInitialElements,
  /// Stream stub has not yet been implemented.
  kNotImplemented,
};

/// @brief Idiomatic C++23 bounded min-heap tracker for the `k`th largest
/// element in a stream.
///
/// Precondition validation order for `create(k, nums)`:
/// 1. If `k <= 0`, returns `std::unexpected(KthLargestError::kInvalidK)`
///    (checked first, even when `nums` is empty).
/// 2. If `nums.size() + 1uz < static_cast<size_t>(k)`, returns
///    `std::unexpected(KthLargestError::kInsufficientInitialElements)`.
/// 3. Starved initialization `nums.size() + 1uz == static_cast<size_t>(k)`
///    (for example `k = 1, nums = {}` or `k = 3, nums = {10, 20}`) is valid;
///    the first `add(val)` brings the retained count to `k`.
class KthLargestStream {
 public:
  /// @brief Constructs a bounded `k`th-largest stream tracker from `nums`.
  ///
  /// @param k Order statistic (`k >= 1`, `nums.size() + 1 >= k`).
  /// @param nums Non-owning read-only view of initial stream elements.
  /// @return Initialized `KthLargestStream`, or `KthLargestError` if a
  ///         precondition is violated or the stub is unimplemented.
  [[nodiscard]] static auto create(int k, std::span<const int> nums)
      -> std::expected<KthLargestStream, KthLargestError>;

  /// @brief Incorporates `val` into the stream and returns the `k`th largest
  /// element seen so far.
  ///
  /// @param val New integer score arriving on the stream.
  /// @return Current `k`th largest element in the stream.
  [[nodiscard]] auto add(int val) -> int;

  /// @brief Returns the configured order statistic `k`.
  [[nodiscard]] auto k() const noexcept -> size_t;

  /// @brief Returns the number of elements currently retained (`<= k()`).
  [[nodiscard]] auto size() const noexcept -> size_t;

 private:
  explicit KthLargestStream(size_t k) noexcept;

  size_t k_{0uz};
  std::priority_queue<int, std::vector<int>, std::greater<>> min_heap_;
};

/// @brief Official LeetCode #703 `KthLargest` compatibility wrapper.
class KthLargest {
 public:
  /// @brief Initializes the tracker with `k` and initial scores `nums`.
  ///
  /// Delegates to `KthLargestStream::create(k, nums)` without mutating `nums`
  /// and without throwing under `-fno-exceptions`.
  ///
  /// @param k Order statistic (`1 <= k <= nums.size() + 1`).
  /// @param nums Initial vector of scores (never mutated).
  KthLargest(int k, std::vector<int>& nums);

  /// @brief Adds `val` to the stream and returns the `k`th largest element so
  /// far.
  ///
  /// @param val New score to insert.
  /// @return Current `k`th largest score (or `0` if uninitialized/stubbed).
  [[nodiscard]] auto add(int val) -> int;

 private:
  std::optional<KthLargestStream> stream_;
};

}  // namespace leet_code
