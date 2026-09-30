cmake_minimum_required(VERSION 4.4.2)

# Dynamic macOS SDK sysroot detection (must precede project() for proper compiler probe)
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
  execute_process(
    COMMAND xcrun --show-sdk-path
    OUTPUT_VARIABLE _MACOS_SDK_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(_MACOS_SDK_PATH)
    set(CMAKE_OSX_SYSROOT
        "${_MACOS_SDK_PATH}"
        CACHE PATH "macOS SDK sysroot" FORCE)
  endif()
endif()

project(
  lc_dsa_practice
  VERSION 0.1.0
  DESCRIPTION "C++23 LeetCode DSA Practice with Modern CMake and Strict Tooling"
  LANGUAGES CXX C)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are strictly prohibited. Use CMakePresets or build/<preset>.")
endif()

# Enforce C++23 standard globally
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compilation database for clangd, clang-tidy, and pre-commit
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Register modular cmake helper path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include modular configuration components
include(ProjectOptions)
include(CompilerWarnings)
include(Sanitizers)
include(Architecture)
include(Dependencies)
include(Formatting)

# Conditionally add library sources (M2 creates src/CMakeLists.txt)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/CMakeLists.txt")
  add_subdirectory(src)
endif()

# Conditionally configure testing (CTest and M2 tests/CMakeLists.txt)
include(CTest)
if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
  include(Testing)
  add_subdirectory(tests)
endif()
