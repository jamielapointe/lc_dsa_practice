#include "lc_dsa/course_schedule_ii.hpp"

#include <vector>

namespace lc_dsa {

auto course_schedule_ii(int num_courses,
                        const std::vector<std::vector<int>>& prerequisites)
    -> std::vector<int> {
  static_cast<void>(num_courses);
  static_cast<void>(prerequisites);
  return {};
}

auto Solution::findOrder(int numCourses,
                         std::vector<std::vector<int>>& prerequisites)
    -> std::vector<int> {
  static_cast<void>(this);
  return course_schedule_ii(numCourses, prerequisites);
}

}  // namespace lc_dsa
