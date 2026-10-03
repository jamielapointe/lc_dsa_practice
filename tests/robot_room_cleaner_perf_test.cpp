#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <utility>
#include <vector>

#include "lc_dsa/robot.hpp"
#include "lc_dsa/robot_room_cleaner.hpp"

namespace {

using lc_dsa::CellValue;
using lc_dsa::DefaultRobot;
using lc_dsa::Point;

auto make_grid(const std::vector<std::vector<int>>& int_grid)
    -> DefaultRobot::Grid {
  DefaultRobot::Grid grid;
  grid.reserve(int_grid.size());
  for (const auto& row : int_grid) {
    std::vector<CellValue> grid_row;
    grid_row.reserve(row.size());
    for (const auto& val : row) {
      grid_row.push_back(static_cast<CellValue>(val));
    }
    grid.push_back(grid_row);
  }
  return grid;
}

bool all_cleaned(const std::vector<std::vector<int>>& original_room,
                 const DefaultRobot& robot) {
  const auto& robot_grid = robot.grid();
  for (std::size_t i = 0; i < original_room.size(); ++i) {
    for (std::size_t j = 0; j < original_room[0].size(); ++j) {
      if (original_room[i][j] == 1 &&
          robot_grid[i][j] != CellValue::OpenClean) {
        return false;
      }
    }
  }
  return true;
}

// Tier 4: Performance & Stress Workloads
TEST(RobotRoomCleanerPerfTest, LargeGrid) {
  std::vector<std::vector<int>> room(100, std::vector<int>(200, 1));
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 50, .col = 100});

  auto start_time = std::chrono::steady_clock::now();
  lc_dsa::robot_room_cleaner(robot);
  auto end_time = std::chrono::steady_clock::now();

  EXPECT_TRUE(all_cleaned(room, robot));
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);
  EXPECT_LT(duration.count(), 2000);  // Expect it to run fast
}

}  // namespace
