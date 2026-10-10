#include "leet_code/number_of_islands.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace leet_code {

namespace {

using NodeT = std::pair<int, int>;
struct PairHash {
  auto operator()(const NodeT& p) const -> std::size_t {
    return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1U);
  }
};

using EdgeList = std::vector<NodeT>;
using AdjacencyMap = std::unordered_map<NodeT, EdgeList, PairHash>;
using VisitedSet = std::unordered_set<NodeT, PairHash>;
using EdgeStack = std::stack<NodeT, std::vector<NodeT>>;

constexpr std::array<std::array<int, 2>, 4> kDirections{
    {
        {{-1, 0}},
        {{1, 0}},
        {{0, -1}},
        {{0, 1}},
    },
};

}  // namespace

auto number_of_islands(std::span<std::vector<char>> grid) -> int {
  // we assume grid is mxn and non-empty.
  int num_rows = static_cast<int>(grid.size());
  int num_cols = static_cast<int>(grid[0].size());
  VisitedSet visited;
  int num_islands = 0;

  auto is_valid = [&](int row, int col) -> bool {
    return row >= 0 && row < num_rows && col >= 0 && col < num_cols &&
           grid[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] ==
               '1' &&
           !visited.contains({row, col});
  };

  auto dfs = [&](int row, int col) -> void {
    EdgeStack stack;
    stack.emplace(row, col);
    while (!stack.empty()) {
      auto [r, c] = stack.top();
      stack.pop();
      visited.emplace(r, c);
      for (const auto& [dr, dc] : kDirections) {
        int new_r = r + dr;
        int new_c = c + dc;
        if (is_valid(new_r, new_c)) {
          stack.emplace(new_r, new_c);
        }
      }
    }
  };

  for (int row = 0; row < num_rows; ++row) {
    for (int col = 0; col < num_cols; ++col) {
      if (is_valid(row, col)) {
        ++num_islands;
        dfs(row, col);
      }
    }
  }

  return num_islands;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
int Solution::numIslands(std::vector<std::vector<char>>& grid) {
  return number_of_islands(grid);
}

}  // namespace leet_code
