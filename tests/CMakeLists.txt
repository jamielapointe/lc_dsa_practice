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
