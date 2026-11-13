#pragma once

/// @file interop_test_harness.hpp
/// @brief Lightweight libc++-only C++23 test assertion harness for Crubit
/// interop tests.

#include <cmath>
#include <iostream>
#include <source_location>

namespace interop::crubit::test {

/// @brief Mutable failure counter scoped to a test binary invocation.
struct TestContext {
  int checks_run = 0;
  int failures = 0;

  /// @brief Records a boolean expectation failure with source location.
  void expect_true(
      bool condition, const char* expr_text,
      std::source_location loc = std::source_location::current()) noexcept {
    ++checks_run;
    if (!condition) {
      ++failures;
      std::cerr << loc.file_name() << ':' << loc.line()
                << ": FAILED: " << expr_text << '\n';
    }
  }

  /// @brief Records an equality expectation failure with source location.
  template <typename Lhs, typename Rhs>
  void expect_eq(
      const Lhs& lhs, const Rhs& rhs, const char* lhs_text,
      const char* rhs_text,
      std::source_location loc = std::source_location::current()) noexcept {
    ++checks_run;
    if (!(lhs == rhs)) {
      ++failures;
      std::cerr << loc.file_name() << ':' << loc.line()
                << ": FAILED: " << lhs_text << " == " << rhs_text << '\n';
    }
  }

  /// @brief Records a floating-point tolerance expectation failure with source
  /// location.
  void expect_near(
      double lhs, double rhs, double eps, const char* lhs_text,
      const char* rhs_text,
      std::source_location loc = std::source_location::current()) noexcept {
    ++checks_run;
    if (std::isnan(lhs) || std::isnan(rhs) || std::abs(lhs - rhs) > eps) {
      ++failures;
      std::cerr << loc.file_name() << ':' << loc.line() << ": FAILED: |"
                << lhs_text << " - " << rhs_text << "| <= " << eps << " (got "
                << lhs << " vs " << rhs << ")\n";
    }
  }

  /// @brief Returns 0 if all checks passed, 1 otherwise.
  [[nodiscard]] auto exit_code() const noexcept -> int {
    return failures == 0 ? 0 : 1;
  }
};

}  // namespace interop::crubit::test

// Function-like macros capture caller expression text and std::source_location
// cleanly. NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INTEROP_EXPECT_TRUE(ctx, expr) \
  (ctx).expect_true(static_cast<bool>(expr), #expr)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INTEROP_EXPECT_EQ(ctx, lhs, rhs) \
  (ctx).expect_eq((lhs), (rhs), #lhs, #rhs)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INTEROP_EXPECT_NEAR(ctx, lhs, rhs, eps) \
  (ctx).expect_near((lhs), (rhs), (eps), #lhs, #rhs)
