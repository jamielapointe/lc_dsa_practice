#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

#include "leet_code/robot.hpp"
#include "leet_code/robot_room_cleaner.hpp"

namespace {

using leet_code::CellValue;
using leet_code::DefaultRobot;
using leet_code::Point;

[[nodiscard]] auto make_grid(const std::vector<std::vector<int>>& int_grid)
    -> DefaultRobot::Grid {
  return int_grid | std::views::transform([](const std::vector<int>& row) {
           return row | std::views::transform([](int val) {
                    return static_cast<CellValue>(val);
                  }) |
                  std::ranges::to<std::vector<CellValue>>();
         }) |
         std::ranges::to<DefaultRobot::Grid>();
}

[[nodiscard]] auto all_cleaned(
    const std::vector<std::vector<int>>& original_room,
    const DefaultRobot& robot) -> bool {
  const auto& robot_grid = robot.grid();
  return std::ranges::all_of(
      std::views::iota(0uz, original_room.size()), [&](size_t i) {
        return std::ranges::all_of(
            std::views::iota(0uz, original_room[0uz].size()), [&](size_t j) {
              return original_room[i][j] != 1 ||
                     robot_grid[i][j] == CellValue::OpenClean;
            });
      });
}

// Tier 4: Performance & Stress Workloads
TEST(RobotRoomCleanerPerfTest, LargeGrid) {
  std::vector<std::vector<int>> room(100uz, std::vector<int>(200uz, 1));
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 50, .col = 100});

  auto start_time = std::chrono::steady_clock::now();
  leet_code::robot_room_cleaner(robot);
  auto end_time = std::chrono::steady_clock::now();

  EXPECT_TRUE(all_cleaned(room, robot));
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  EXPECT_LT(duration.count(), 2000);  // Expect it to run fast
}

}  // namespace
