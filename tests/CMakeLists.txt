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

add_project_test(
  lru_cache_test
  SOURCES
  lru_cache_test.cpp
  LIBS
  lc_dsa::lru_cache)

add_project_test(
  moving_average_from_data_stream_test
  SOURCES
  moving_average_from_data_stream_test.cpp
  LIBS
  lc_dsa::moving_average_from_data_stream)

add_project_test(
  design_hit_counter_test
  SOURCES
  design_hit_counter_test.cpp
  LIBS
  lc_dsa::design_hit_counter)
