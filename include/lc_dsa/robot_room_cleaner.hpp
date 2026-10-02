#pragma once

#include "lc_dsa/robot.hpp"

namespace lc_dsa {

/// @file robot_room_cleaner.hpp
/// @brief 489. Robot Room Cleaner (Hard)
///
/// You are controlling a robot that is located somewhere in a room. The room is
/// modeled as an m x n grid where 0 represents a wall and 1 represents an empty
/// slot. The robot starts at an unknown location in the room that is guaranteed
/// to be empty, and you do not have access to the grid, but you can move the
/// robot using the given API `Robot`.
///
/// Different requirement from the original problem: the solution must be
/// iterative (i.e., no recursion)
///
/// Constraints:
/// - m == room.length
/// - n == room[i].length
/// - 1 <= m <= 100
/// - 1 <= n <= 200
/// - room[i][j] is either 0 or 1.
/// - 0 <= row < m
/// - 0 <= col < n
/// - room[row][col] == 1
/// - All the empty cells can be visited from the starting position.

/// @brief Modern C++23 Interface
auto robot_room_cleaner(Robot& robot) -> void;

/// @brief LeetCode Compatibility Wrapper
class Solution {
 public:
  void cleanRoom(Robot& robot);
};

}  // namespace lc_dsa
