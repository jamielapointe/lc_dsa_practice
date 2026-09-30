# cmake/Dependencies.cmake Manages external project dependencies via hybrid FetchContent and system
# packages.

include(FetchContent)

if(NOT DEFINED BUILD_TESTING OR BUILD_TESTING)
  # Modern dependency resolution: Tries find_package(GTest CONFIG) first to use pre-installed system
  # package (Homebrew 1.18.0) Falls back to FetchContent from GitHub v1.16.0 if not found.
  FetchContent_Declare(
    googletest
    SYSTEM
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.16.0
    FIND_PACKAGE_ARGS
    NAMES
    GTest
    CONFIG)

  # Prevent Google Test from overriding parent project install targets and options
  set(INSTALL_GTEST
      OFF
      CACHE BOOL "Disable installation of googletest" FORCE)
  set(BUILD_GMOCK
      ON
      CACHE BOOL "Build GMock" FORCE)
  # cmake-lint: disable=C0103
  set(gtest_force_shared_crt
      ON
      CACHE BOOL "Use shared CRT" FORCE)

  FetchContent_MakeAvailable(googletest)
endif()
