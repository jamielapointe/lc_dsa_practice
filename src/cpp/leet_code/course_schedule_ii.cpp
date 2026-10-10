#include "leet_code/course_schedule_ii.hpp"

#include <cstddef>
#include <vector>

namespace leet_code {

auto course_schedule_ii(size_t num_courses,
                        const std::vector<std::vector<int>>& prerequisites)
    -> std::vector<int> {
  std::vector<std::vector<int>> graph(num_courses);
  std::vector<int> in_degree(num_courses);
  std::vector<int> course_order;
  course_order.reserve(num_courses);
  for (const auto& prereq : prerequisites) {
    graph[static_cast<size_t>(prereq[1])].push_back(prereq[0]);
    in_degree[static_cast<size_t>(prereq[0])]++;
  }
  for (size_t i = 0; i < num_courses; i++) {
    if (in_degree[i] == 0) {
      course_order.push_back(static_cast<int>(i));
    }
  }

  for (size_t i = 0; i < course_order.size(); ++i) {
    auto course = course_order[i];
    for (const auto& neighbor : graph[static_cast<size_t>(course)]) {
      if (--in_degree[static_cast<size_t>(neighbor)] == 0) {
        course_order.push_back(neighbor);
      }
    }
  }

  if (course_order.size() == num_courses) {
    return course_order;
  }
  return {};
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
auto Solution::findOrder(int numCourses,
                         std::vector<std::vector<int>>& prerequisites)
    -> std::vector<int> {
  return course_schedule_ii(static_cast<size_t>(numCourses), prerequisites);
}

}  // namespace leet_code
