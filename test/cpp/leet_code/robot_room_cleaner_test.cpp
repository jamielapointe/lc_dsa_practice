#include "leet_code/robot_room_cleaner.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

#include "leet_code/robot.hpp"

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
