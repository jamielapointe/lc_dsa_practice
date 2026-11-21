/// @file math_extra.hpp
/// @brief Checked arithmetic helpers for fixed-width and standard C++ integer
/// types.

#pragma once

#include <concepts>
#include <limits>
#include <optional>
#include <type_traits>

namespace common {

/// @brief Matches standard signed and unsigned arithmetic integer types,
/// excluding `bool` and non-numeric character types (`char`, `wchar_t`,
/// `char8_t`, `char16_t`, `char32_t`).
///
/// Mirrors the C++26 saturation-arithmetic integer constraint while permitting
/// `int8_t` (`signed char`) and `uint8_t` (`unsigned char`).
template <typename T>
concept Integer =
    std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool> &&
    !std::same_as<std::remove_cv_t<T>, char> &&
    !std::same_as<std::remove_cv_t<T>, wchar_t> &&
    !std::same_as<std::remove_cv_t<T>, char8_t> &&
    !std::same_as<std::remove_cv_t<T>, char16_t> &&
    !std::same_as<std::remove_cv_t<T>, char32_t>;

namespace detail {

/// @brief Pure ISO C++23 overflow-checked addition fallback.
///
/// Kept compiled and unit-tested alongside the builtin fast path so fallback
/// logic cannot bit-rot on non-Clang/GCC toolchains.
///
/// @tparam T Arithmetic integer type satisfying `common::Integer`.
/// @param a Left addend.
/// @param b Right addend.
/// @return `a + b` when representable in `T`, or `std::nullopt` on overflow or
///         underflow.
template <Integer T>
[[nodiscard]] constexpr auto checked_add_portable(T a, T b) noexcept
    -> std::optional<T> {
  if constexpr (std::unsigned_integral<T>) {
    if (a > std::numeric_limits<T>::max() - b) {
      return std::nullopt;
    }
  } else {
    if (b > T{0} && a > std::numeric_limits<T>::max() - b) {
      return std::nullopt;
    }
    if (b < T{0} && a < std::numeric_limits<T>::min() - b) {
      return std::nullopt;
    }
  }
  return static_cast<T>(a + b);
}

}  // namespace detail

/// @brief Performs overflow-checked addition on two integers of identical type.
///
/// Uses the compiler's hardware-flag intrinsic (`__builtin_add_overflow`) when
/// available for branchless code generation, and falls back to
/// `detail::checked_add_portable` otherwise. Guaranteed free of signed-overflow
/// undefined behavior and usable in constant expressions.
///
/// @tparam T Arithmetic integer type satisfying `common::Integer`.
/// @param a Left addend.
/// @param b Right addend.
/// @return `a + b` when representable in `T`, or `std::nullopt` on overflow or
///         underflow.
template <Integer T>
[[nodiscard]] constexpr auto checked_add(T a, T b) noexcept
    -> std::optional<T> {
#if defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
  T result{};
  if (__builtin_add_overflow(a, b, &result)) {
    return std::nullopt;
  }
  return result;
#else
  return detail::checked_add_portable(a, b);
#endif
#else
  return detail::checked_add_portable(a, b);
#endif
}

}  // namespace common
