# tests/CMakeLists.txt
# Test target registration using modular Testing.cmake helper

add_project_test(
  two_sum_test
  SOURCES
  two_sum_test.cpp
  LIBS
  lc_dsa::two_sum)

add_project_test(
  list_node_test
  SOURCES
  list_node_test.cpp
  LIBS
  lc_dsa::list_node)

add_project_test(
  add_two_numbers_test
  SOURCES
  add_two_numbers_test.cpp
  LIBS
  lc_dsa::add_two_numbers
  lc_dsa::list_node)

add_project_test(
  shortest_path_in_binary_matrix_test
  SOURCES
  shortest_path_in_binary_matrix_test.cpp
  LIBS
  lc_dsa::shortest_path_in_binary_matrix)

add_project_test(
  number_of_islands_test
  SOURCES
  number_of_islands_test.cpp
  LIBS
  lc_dsa::number_of_islands)

add_project_test(
  robot_room_cleaner_test
  SOURCES
  robot_room_cleaner_test.cpp
  LIBS
  lc_dsa::robot_room_cleaner)

add_project_test(
  robot_test
  SOURCES
  robot_test.cpp
  LIBS
  lc_dsa::robot)

add_project_test(
  robot_room_cleaner_perf_test
  SOURCES
  robot_room_cleaner_perf_test.cpp
  LIBS
  lc_dsa::robot_room_cleaner
  LABELS
  Performance)
