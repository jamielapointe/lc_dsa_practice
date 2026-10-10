#pragma once

#include <cstdint>
#include <vector>

namespace leet_code {

/// @file robot.hpp
/// @brief Robot implementation - helper for other Leet Code questions
///
/// The robot has an internal `m x n` grid that it does not expose as part of
/// its API. The room is modeled as an `m x n` grid where `0` represents
/// a wall and non-zero represents an open space (or empty slot).
///
/// The robot starts at an unknown empty and dirty (`1`) location and facing up.
/// Your job in many leet code problems is to explore the entire grid and
/// perform some operation on each cell.

/// @brief Robot's public control interface.
class Robot {
 public:
  Robot() = default;

  Robot(const Robot&) = delete;
  Robot& operator=(const Robot&) = delete;
  Robot(Robot&&) = delete;
  Robot& operator=(Robot&&) = delete;

  virtual ~Robot() = default;

  /// Returns true if the cell in front is open and robot moves into the cell.
  /// Returns false if the cell in front is blocked and robot stays in the
  /// current cell.
  virtual bool move() = 0;

  /// Robot will stay in the same cell after calling turnLeft/turnRight.
  /// Each turn will be 90 degrees.
  virtual void turnLeft() = 0;
  virtual void turnRight() = 0;

  /// Clean the current cell.
  virtual void clean() = 0;
};

/// @brief Represents a Direction for movement
enum class Direction : std::uint8_t {
  Up,
  Right,
  Down,
  Left,
};

/// @brief Represents the possible states of a cell in the grid
enum class CellValue : std::int8_t {
  Wall = 0,
  OpenDirty = 1,
  OpenClean = 2,
};

/// @brief Represents a Cell in the grid
struct Point {
  int row{0};
  int col{0};

  constexpr auto operator+=(Direction dir) noexcept -> Point& {
    switch (dir) {
      case Direction::Up:
        --row;
        break;
      case Direction::Right:
        ++col;
        break;
      case Direction::Down:
        ++row;
        break;
      case Direction::Left:
        --col;
        break;
    }
    return *this;
  }

  friend constexpr auto operator+(Point cell, Direction dir) noexcept -> Point {
    cell += dir;
    return cell;
  }

  auto operator<=>(const Point&) const = default;
};

struct PointHash {
  auto operator()(const Point& cell) const -> std::size_t {
    return std::hash<int>()(cell.row) ^ (std::hash<int>()(cell.col) << 1U);
  }
};

/// @brief Specific / Default implementation of the Robot Interface
///
/// The room is modeled as an `m x n` grid where `0` represents a wall and non-
/// zero represents an open space (or empty slot).
///   - `0` == wall
///   - `1` == open and dirty
///   - `2` == open and clean
///
/// The robot starts at an unknown location in the room that is guaranteed to
/// be empty, and you do not have access to the grid, but you can move the
/// robot using the given API `Robot`.
///
/// All open slots are initially dirty (`1`).
///
class DefaultRobot final : public Robot {
 public:
  using Grid = std::vector<std::vector<CellValue>>;

  ~DefaultRobot() override = default;

  DefaultRobot(const DefaultRobot&) = delete;
  DefaultRobot& operator=(const DefaultRobot&) = delete;
  DefaultRobot(DefaultRobot&&) = delete;
  DefaultRobot& operator=(DefaultRobot&&) = delete;

  /// @brief Constructor
  /// @param grid Assumed to be properly initialized and have a size >= 1x1
  /// @param start_cell Assumed to be within grid bounds and value == '1'
  /// @note The grid is taken by value so the caller can `std::move` it to
  /// avoid large memory allocations and deep copies, safely transferring
  /// ownership.
  /// @note The initial direction of the robot shall be `Direction::Up`.
  DefaultRobot(Grid grid, Point start_cell);

  /// Returns true if the cell in front is open and robot moves into the cell.
  /// Returns false if the cell in front is blocked and robot stays in the
  /// current cell.
  bool move() override;

  /// Robot will stay in the same cell after calling turnLeft/turnRight.
  /// Each turn will be 90 degrees.
  void turnLeft() override;
  void turnRight() override;

  /// Clean the current cell.
  void clean() override;

  [[nodiscard]] auto grid() const -> const Grid& { return grid_; }

 private:
  Grid grid_;
  Point current_cell_;
  int width_;   // m
  int height_;  // n
  Direction direction_{Direction::Up};

  [[nodiscard]] auto current_cell_value() -> CellValue& {
    return cell_value(current_cell_);
  }

  [[nodiscard]] auto current_cell_value() const -> CellValue {
    return cell_value(current_cell_);
  }

  [[nodiscard]] auto cell_value(Point cell) -> CellValue& {
    auto const [row, col] = cell;
    return grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
  }

  [[nodiscard]] auto cell_value(Point cell) const -> CellValue {
    auto const [row, col] = cell;
    if (row < 0 || col < 0 || row >= height_ || col >= width_) {
      return CellValue::Wall;
    }
    return grid_[static_cast<size_t>(row)][static_cast<size_t>(col)];
  }
};

}  // namespace leet_code
