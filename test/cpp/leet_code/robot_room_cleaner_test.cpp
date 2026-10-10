#include "leet_code/robot_room_cleaner.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <utility>
#include <vector>

#include "leet_code/robot.hpp"

namespace {

using leet_code::CellValue;
using leet_code::DefaultRobot;
using leet_code::Point;

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

// Tier 1: Canonical Feature Coverage
TEST(RobotRoomCleanerTest, Example1) {
  std::vector<std::vector<int>> room = {
      {1, 1, 1, 1, 1, 0, 1, 1}, {1, 1, 1, 1, 1, 0, 1, 1},
      {1, 0, 1, 1, 1, 1, 1, 1}, {0, 0, 0, 1, 0, 0, 0, 0},
      {1, 1, 1, 1, 1, 1, 1, 1},
  };

  // Make a copy of the grid because Robot moves it internally
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 1, .col = 3});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(RobotRoomCleanerTest, SingleCell) {
  std::vector<std::vector<int>> room = {
      {1},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 0, .col = 0});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

TEST(RobotRoomCleanerTest, TrappedRobot) {
  std::vector<std::vector<int>> room = {
      {0, 0, 0},
      {0, 1, 0},
      {0, 0, 0},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 1, .col = 1});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

TEST(RobotRoomCleanerTest, HorizontalHallway) {
  std::vector<std::vector<int>> room = {
      {1, 1, 1, 1, 1},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 0, .col = 2});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

TEST(RobotRoomCleanerTest, VerticalHallway) {
  std::vector<std::vector<int>> room = {
      {1}, {1}, {1}, {1}, {1},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 2, .col = 0});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

// Tier 3: Pathological & Complex Topologies
TEST(RobotRoomCleanerTest, SpiralRoom) {
  std::vector<std::vector<int>> room = {
      {1, 1, 1, 1, 1}, {0, 0, 0, 0, 1}, {1, 1, 1, 0, 1},
      {1, 0, 0, 0, 1}, {1, 1, 1, 1, 1},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 2, .col = 0});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

TEST(RobotRoomCleanerTest, OpenRoom) {
  std::vector<std::vector<int>> room = {
      {1, 1, 1, 1},
      {1, 1, 1, 1},
      {1, 1, 1, 1},
      {1, 1, 1, 1},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 1, .col = 1});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

TEST(RobotRoomCleanerTest, DonutRoom) {
  std::vector<std::vector<int>> room = {
      {1, 1, 1, 1, 1}, {1, 0, 0, 0, 1}, {1, 0, 0, 0, 1},
      {1, 0, 0, 0, 1}, {1, 1, 1, 1, 1},
  };
  auto grid = make_grid(room);
  DefaultRobot robot(std::move(grid), Point{.row = 0, .col = 0});

  leet_code::robot_room_cleaner(robot);

  EXPECT_TRUE(all_cleaned(room, robot));
}

}  // namespace
