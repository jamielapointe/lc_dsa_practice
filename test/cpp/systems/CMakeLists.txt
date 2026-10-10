# test/cpp/systems/CMakeLists.txt Test target registration for the systems tree using the modular
# Testing.cmake helper.

add_project_test(
  bounded_fifo_test
  SOURCES
  bounded_fifo_test.cpp
  LIBS
  systems::bounded_fifo
)
