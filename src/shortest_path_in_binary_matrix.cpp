#include "lc_dsa/shortest_path_in_binary_matrix.hpp"

#include <array>
#include <cstddef>
#include <deque>
#include <span>
#include <unordered_set>
#include <vector>

namespace lc_dsa {

namespace {

struct Node {
  int row{0};
  int col{0};
  int distance_from_start{0};

  Node(int r, int c, int dist) : row(r), col(c), distance_from_start(dist) {}
  Node(int r, int c) : row(r), col(c) {}
  Node() = default;

  friend bool operator==(const Node& lhs, const Node& rhs) {
    return lhs.row == rhs.row && lhs.col == rhs.col;
  }

  // friend bool operator!=(const Node& lhs, const Node& rhs) {
  //   return !(lhs == rhs);
  // }
};

struct NodeHasher {
  std::size_t operator()(const Node& node) const {
    return std::hash<int>()(node.row) ^ (std::hash<int>()(node.col) << 1U);
  }
};

using Grid = std::span<const std::vector<int>>;
using Visited = std::unordered_set<Node, NodeHasher>;
using Queue = std::deque<Node>;

inline bool is_valid(const Grid& grid, const Visited& visited, int row,
                     int col) {
  int length = static_cast<int>(grid.size());
  if (row < 0 || col < 0 || row >= length || col >= length) {
    return false;
  }
  if (grid[static_cast<size_t>(row)][static_cast<size_t>(col)] != 0) {
    return false;
  }
  if (visited.contains(Node(row, col))) {
    return false;
  }
  return true;
}

constexpr std::array<std::pair<int, int>, 8> directions{
    {
        {-1, -1},
        {-1, 0},
        {-1, 1},
        {0, -1},
        {0, 1},
        {1, -1},
        {1, 0},
        {1, 1},
    },
};

inline auto next_node(const Node& node, const std::pair<int, int>& direction)
    -> Node {
  return {node.row + direction.first, node.col + direction.second,
          node.distance_from_start + 1};
}

}  // namespace

auto shortest_path_in_binary_matrix(Grid grid) -> int {
  // Problem statement says that the grid is always non-empty and square... so
  // don't waste time checking for this.
  if (grid[0][0] != 0) {
    return -1;
  }
  if (grid.back().back() != 0) {
    return -1;
  }

  Node start(0, 0, 1);
  Node end(static_cast<int>(grid.size()) - 1,
           static_cast<int>(grid[0].size()) - 1);
  if (start == end) {
    return 1;
  }

  Queue queue;
  Visited visited;
  queue.push_back(start);
  visited.insert(start);

  while (!queue.empty()) {
    auto node = queue.front();
    queue.pop_front();

    if (node == end) {
      return node.distance_from_start;
    }

    for (const auto& direction : directions) {
      auto next = next_node(node, direction);
      if (is_valid(grid, visited, next.row, next.col)) {
        queue.push_back(next);
        visited.insert(next);
      }
    }
  }

  return -1;  // Dummy return
}

auto Solution::shortestPathBinaryMatrix(Grid grid) -> int {
  return shortest_path_in_binary_matrix(grid);
}

}  // namespace lc_dsa
