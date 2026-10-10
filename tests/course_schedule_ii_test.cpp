#include "lc_dsa/course_schedule_ii.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <vector>

namespace {

// Tier 1: Canonical Feature Coverage
TEST(CourseScheduleIiTest, Example1) {
  lc_dsa::Solution solution;
  std::vector<std::vector<int>> prerequisites = {{1, 0}};
  std::vector<int> expected = {0, 1};

  EXPECT_EQ(lc_dsa::course_schedule_ii(2, prerequisites), expected);
  EXPECT_EQ(solution.findOrder(2, prerequisites), expected);
}

TEST(CourseScheduleIiTest, Example2) {
  lc_dsa::Solution solution;
  std::vector<std::vector<int>> prerequisites = {
      {1, 0}, {2, 0}, {3, 1}, {3, 2}};
  std::vector<int> expected1 = {0, 1, 2, 3};
  std::vector<int> expected2 = {0, 2, 1, 3};

  auto result = lc_dsa::course_schedule_ii(4, prerequisites);
  EXPECT_TRUE(result == expected1 || result == expected2);

  auto result_sol = solution.findOrder(4, prerequisites);
  EXPECT_TRUE(result_sol == expected1 || result_sol == expected2);
}

TEST(CourseScheduleIiTest, Example3) {
  lc_dsa::Solution solution;
  std::vector<std::vector<int>> prerequisites = {};
  std::vector<int> expected = {0};

  EXPECT_EQ(lc_dsa::course_schedule_ii(1, prerequisites), expected);
  EXPECT_EQ(solution.findOrder(1, prerequisites), expected);
}

// Tier 2: Boundary, Corner & Edge Cases
TEST(CourseScheduleIiTest, ImpossibleSchedule) {
  std::vector<std::vector<int>> prerequisites = {{1, 0}, {0, 1}};
  std::vector<int> expected = {};
  EXPECT_EQ(lc_dsa::course_schedule_ii(2, prerequisites), expected);
}

TEST(CourseScheduleIiTest, DisconnectedGraph) {
  std::vector<std::vector<int>> prerequisites = {{1, 0}, {3, 2}};
  auto result = lc_dsa::course_schedule_ii(4, prerequisites);
  EXPECT_EQ(result.size(), 4);  // Dummy implementation will fail here
}

// Tier 4: Stress Workloads & OJ Simulation
TEST(CourseScheduleIiTest, MaxConstraints) {
  int num_courses = 2000;
  std::vector<std::vector<int>> prerequisites;
  for (int i = 1; i < num_courses; ++i) {
    prerequisites.push_back({i, i - 1});
  }

  auto start = std::chrono::high_resolution_clock::now();
  auto result = lc_dsa::course_schedule_ii(static_cast<size_t>(num_courses),
                                           prerequisites);
  auto end = std::chrono::high_resolution_clock::now();

  EXPECT_EQ(result.size(), num_courses);  // Dummy returns {} so will fail
  EXPECT_LT(end - start, std::chrono::milliseconds(200));
}

}  // namespace
