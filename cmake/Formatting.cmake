# cmake/Formatting.cmake Configures custom targets 'format' and 'format-check' using clang-format.

find_program(CLANG_FORMAT_PROGRAM NAMES clang-format clang-format-23 clang-format-21)

if(CLANG_FORMAT_PROGRAM)
  file(
    GLOB_RECURSE
    ALL_CXX_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_SOURCE_DIR}/include/*.h"
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_SOURCE_DIR}/tests/*.hpp"
    "${CMAKE_SOURCE_DIR}/tests/*.h")

  if(ALL_CXX_SOURCES)
    add_custom_target(
      format
      COMMAND ${CLANG_FORMAT_PROGRAM} -i ${ALL_CXX_SOURCES}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Formatting C++ source files with clang-format"
      VERBATIM)

    add_custom_target(
      format-check
      COMMAND ${CLANG_FORMAT_PROGRAM} --dry-run --Werror ${ALL_CXX_SOURCES}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Checking C++ source formatting with clang-format"
      VERBATIM)
  else()
    add_custom_target(
      format
      COMMAND ${CMAKE_COMMAND} -E echo "No C++ source files found to format."
      COMMENT "No C++ files to format"
      VERBATIM)

    add_custom_target(
      format-check
      COMMAND ${CMAKE_COMMAND} -E echo "No C++ source files found to check."
      COMMENT "No C++ files to check"
      VERBATIM)
  endif()
else()
  message(
    WARNING "clang-format not found. Targets 'format' and 'format-check' will not be available.")
endif()
