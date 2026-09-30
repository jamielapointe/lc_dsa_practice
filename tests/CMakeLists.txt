# tests/CMakeLists.txt
# Test target registration using modular Testing.cmake helper

add_project_test(
  two_sum_test
  SOURCES two_sum_test.cpp
  LIBS lc_dsa::two_sum)
