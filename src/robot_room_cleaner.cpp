#include "lc_dsa/robot_room_cleaner.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <stack>
#include <unordered_set>
#include <utility>
#include <vector>

#include "lc_dsa/robot.hpp"

namespace lc_dsa {

namespace {

/// @brief Keeps track of visited cells.
///
/// @param point The coordinates of the cell.
///
/// @note This is used instead of a 2D vector to avoid pre-allocating memory
/// for a potentially very large grid.
using VisitedSet = std::unordered_set<Point, PointHash>;

/// @brief Represents the state of the robot during DFS traversal.
///
/// @param point Position where the robot is currently located.
/// @param entry_direction Direction from which the robot entered the current
/// cell. This is the direction to reverse when backtracking.
/// @param facing_direction Direction the robot is currently facing.
struct State {
  Point point;
  Direction entry_direction;
  Direction facing_direction;
};

/// @brief The stack used for DFS traversal.
///
/// @param State The state of the robot during DFS traversal.
/// @param std::vector<State> The underlying container for the stack.
using Stack = std::stack<State, std::vector<State>>;

/// @brief The directions the robot can move in, ordered in a specific way to
/// facilitate the DFS traversal.
constexpr std::array<Direction, 4> kDirections{
    Direction::Down,
    Direction::Right,
    Direction::Up,
    Direction::Left,
};

/// @brief Reverses the given direction.
///
/// @param dir The direction to reverse.
///
/// @return The reverse direction.
[[nodiscard]] constexpr auto reverse_direction(Direction dir) noexcept
    -> Direction {
  switch (dir) {
    case Direction::Up:
      return Direction::Down;
    case Direction::Down:
      return Direction::Up;
    case Direction::Left:
      return Direction::Right;
    case Direction::Right:
      return Direction::Left;
  }
  std::unreachable();
}

// use the minimum number of turns to get the robot to match the desired
// direction
void rotate_robot_to_match(Robot& robot, Direction curr_dir,
                           Direction desired_dir) {
  if (curr_dir == desired_dir) {
    return;
  }

  auto const curr = static_cast<int>(curr_dir);
  auto const desired = static_cast<int>(desired_dir);

  switch ((desired - curr + 4) % 4) {
    case 1:
      robot.turnRight();
      break;
    case 2:
      robot.turnRight();
      robot.turnRight();
      break;
    case 3:
      robot.turnLeft();
      break;
    default:
      std::unreachable();
  }
}

}  // namespace

// iterative (i.e. non-recursive) backtracking DFS solution
auto robot_room_cleaner(Robot& robot) -> void {
  Point start_point;
  Stack stack;
  VisitedSet visited;
  Direction start_direction = Direction::Up;
  robot.clean();
  stack.push({
      .point = start_point,
      .entry_direction = start_direction,
      .facing_direction = start_direction,
  });
  visited.insert(start_point);

  while (!stack.empty()) {
    auto const [current_point, entry_direction, facing_direction] = stack.top();
    std::optional<Point> optional_next_point = std::nullopt;
    Direction next_direction = Direction::Up;

    for (const auto& direction : kDirections) {
      Point candidate_pt = current_point + direction;
      if (!visited.contains(candidate_pt)) {
        optional_next_point = candidate_pt;
        next_direction = direction;
        break;
      }
    }

    if (optional_next_point.has_value()) {
      Point next_point = optional_next_point.value();
      rotate_robot_to_match(robot, facing_direction, next_direction);
      visited.insert(next_point);
      if (robot.move()) {
        robot.clean();
        stack.emplace(next_point, next_direction, next_direction);
      } else {
        // hit a wall, but we rotated the robot, so we need to update the stack
        stack.top().facing_direction = next_direction;
      }
    } else {
      stack.pop();
      if (!stack.empty()) {
        rotate_robot_to_match(robot, facing_direction,
                              reverse_direction(entry_direction));
        robot.move();
        stack.top().facing_direction = reverse_direction(entry_direction);
      }
    }
  }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Solution::cleanRoom(Robot& robot) { robot_room_cleaner(robot); }

}  // namespace lc_dsa
