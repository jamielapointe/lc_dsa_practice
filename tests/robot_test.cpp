#include "lc_dsa/robot.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace {

using lc_dsa::CellValue;
using lc_dsa::DefaultRobot;
using lc_dsa::Direction;
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

// Tier 1: Canonical Feature Coverage - Initialization
TEST(DefaultRobotTest, InitializationTakesOwnership) {
  auto grid = make_grid({
      {1, 1, 0},
      {1, 0, 1},
      {1, 1, 1},
  });
  Point start{.row = 1, .col = 0};

  // Provide grid by moving it into the constructor.
  // This takes advantage of our `Grid` by-value signature for optimal
  // efficiency.
  DefaultRobot robot(std::move(grid), start);

  // Since it's not fully implemented, we'll verify it doesn't crash on clean
  EXPECT_NO_THROW(robot.clean());

  // Verify it actually cleans
  EXPECT_EQ(robot.grid()[1][0], CellValue::OpenClean);
}

// Tier 1: Canonical Feature Coverage - Move Forward (Blocked)
TEST(DefaultRobotTest, MoveForwardBlocked) {
  auto grid = make_grid({
      {0, 0, 0},
      {0, 1, 0},
      {0, 0, 0},
  });
  // Facing UP, starting at {1, 1}. The cell at {0, 1} is 0 (blocked).
  DefaultRobot robot(std::move(grid), Point{.row = 1, .col = 1});

  EXPECT_FALSE(robot.move());
}

// Tier 1: Canonical Feature Coverage - Move Forward (Open)
TEST(DefaultRobotTest, MoveForwardOpen) {
  auto grid = make_grid({
      {0, 1, 0},
      {0, 1, 0},
      {0, 0, 0},
  });
  // Facing UP, starting at {1, 1}. The cell at {0, 1} is 1 (open).
  DefaultRobot robot(std::move(grid), Point{.row = 1, .col = 1});

  EXPECT_TRUE(robot.move());
  // After moving to {0, 1}, next cell up {-1, 1} is out of bounds.
  EXPECT_FALSE(robot.move());
}

// Tier 1: Canonical Feature Coverage - Turns
TEST(DefaultRobotTest, TurningWorksCorrectly) {
  auto grid = make_grid({
      {1, 1, 1},
      {1, 1, 1},
      {1, 1, 1},
  });
  // Start at center
  DefaultRobot robot(std::move(grid), Point{.row = 1, .col = 1});

  // Turn right (now facing Right)
  EXPECT_NO_THROW(robot.turnRight());

  // Assuming move works, it would move to {1, 2}.
  EXPECT_TRUE(robot.move());

  // Turn left (now facing Up)
  EXPECT_NO_THROW(robot.turnLeft());

  // Move to {0, 2}
  EXPECT_TRUE(robot.move());

  // Blocked at boundary
  EXPECT_FALSE(robot.move());
}

// Tier 2: Boundary, Corner & Edge Cases - Out of Bounds Checks
TEST(DefaultRobotTest, HandlesOutOfBoundsGracefully) {
  auto grid = make_grid({{1}});

  DefaultRobot robot(std::move(grid), Point{.row = 0, .col = 0});
  EXPECT_FALSE(robot.move());  // moving right from {0, 0} goes out of bounds

  robot.turnRight();           // now facing Down
  EXPECT_FALSE(robot.move());  // out of bounds

  robot.turnRight();           // now facing Left
  EXPECT_FALSE(robot.move());  // out of bounds

  robot.turnRight();           // now facing Up
  EXPECT_FALSE(robot.move());  // out of bounds
}

// Tier 1: Canonical Feature Coverage - Point Direction Addition
TEST(PointTest, DirectionalAddition) {
  Point cell{.row = 1, .col = 1};

  EXPECT_EQ(cell + Direction::Up, (Point{.row = 0, .col = 1}));
  EXPECT_EQ(cell + Direction::Right, (Point{.row = 1, .col = 2}));
  EXPECT_EQ(cell + Direction::Down, (Point{.row = 2, .col = 1}));
  EXPECT_EQ(cell + Direction::Left, (Point{.row = 1, .col = 0}));

  cell += Direction::Right;
  EXPECT_EQ(cell, (Point{.row = 1, .col = 2}));
  cell += Direction::Down;
  EXPECT_EQ(cell, (Point{.row = 2, .col = 2}));
}

}  // namespace
