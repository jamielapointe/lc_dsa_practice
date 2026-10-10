#include "leet_code/robot.hpp"

#include <cstddef>
#include <utility>

namespace leet_code {

namespace {

[[nodiscard]] constexpr auto rotate_right(Direction dir) noexcept -> Direction {
  switch (dir) {
    case Direction::Up:
      return Direction::Right;
    case Direction::Right:
      return Direction::Down;
    case Direction::Down:
      return Direction::Left;
    case Direction::Left:
      return Direction::Up;
  }
  std::unreachable();
}

[[nodiscard]] constexpr auto rotate_left(Direction dir) noexcept -> Direction {
  switch (dir) {
    case Direction::Up:
      return Direction::Left;
    case Direction::Left:
      return Direction::Down;
    case Direction::Down:
      return Direction::Right;
    case Direction::Right:
      return Direction::Up;
  }
  std::unreachable();
}

}  // namespace

DefaultRobot::DefaultRobot(Grid grid, Point start_cell)
    : grid_(std::move(grid)),
      current_cell_(start_cell),
      width_{static_cast<int>(grid_[0].size())},
      height_{static_cast<int>(grid_.size())} {}

bool DefaultRobot::move() {
  Point tmp_cell = current_cell_ + direction_;
  if (std::as_const(*this).cell_value(tmp_cell) == CellValue::Wall) {
    return false;
  }
  current_cell_ = tmp_cell;
  return true;
}

void DefaultRobot::turnLeft() { direction_ = rotate_left(direction_); }

void DefaultRobot::turnRight() { direction_ = rotate_right(direction_); }

void DefaultRobot::clean() { current_cell_value() = CellValue::OpenClean; }

}  // namespace leet_code
