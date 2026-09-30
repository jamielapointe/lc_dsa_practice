# cmake/Testing.cmake Configures CTest and provides the standardized add_project_test helper
# function.

enable_testing()
include(GoogleTest)

# Helper function to register and configure a Google Test executable.
function(add_project_test test_target)
  cmake_parse_arguments(
    ARG
    ""
    ""
    "SOURCES;LIBS"
    ${ARGN})

  if(NOT ARG_SOURCES)
    message(FATAL_ERROR "add_project_test called without SOURCES for target ${test_target}")
  endif()

  add_executable(${test_target} ${ARG_SOURCES})

  target_link_libraries(
    ${test_target}
    PRIVATE ${ARG_LIBS}
            GTest::gtest_main
            project_options
            project_warnings
            project_sanitizers
            project_architecture)

  if(TARGET GTest::gmock)
    target_link_libraries(${test_target} PRIVATE GTest::gmock)
  endif()

  if(NOT CMAKE_CROSSCOMPILING)
    gtest_discover_tests(
      ${test_target} DISCOVERY_MODE POST_BUILD
      PROPERTIES
      TIMEOUT 60)
  endif()
endfunction()
