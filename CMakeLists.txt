cmake_minimum_required(VERSION 4.4.3 FATAL_ERROR)

# Deletion safety guard: ensure CCACHE_DIR resolves to an absolute path before project() compiler
# checks.
if(
    NOT DEFINED ENV{CCACHE_DIR}
    OR "$ENV{CCACHE_DIR}" MATCHES "^~"
    OR NOT IS_ABSOLUTE "$ENV{CCACHE_DIR}"
)
    set(ENV{CCACHE_DIR} "${CMAKE_CURRENT_BINARY_DIR}/.ccache")
endif()

project(
    lc_dsa_practice
    VERSION 0.1.0
    DESCRIPTION
        "C++23 LeetCode and low-level systems C++ practice with modern CMake and strict tooling"
    LANGUAGES CXX C
)

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds are strictly prohibited. Use CMakePresets (build/<preset>)."
    )
endif()

# Enforce C++23 standard globally
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compilation database for clangd, clang-tidy, and pre-commit
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project-level switches for the two C++ trees
option(BUILD_LEET_CODE "Build the C++ LeetCode practice libraries and tests" ON)
option(BUILD_SYSTEMS "Build the low-level / systems / embedded C++ libraries and tests" ON)

# Register modular cmake helper path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include modular configuration components
include(ProjectOptions)
include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(Architecture)
include(Dependencies)
include(Formatting)

# C++ libraries: src/cpp/<tree>/ with headers in src/include/<tree>/
if(BUILD_LEET_CODE)
    add_subdirectory(src/cpp/leet_code)
endif()
if(BUILD_SYSTEMS)
    add_subdirectory(src/cpp/systems)
endif()

# C++ tests: test/cpp/<tree>/
include(CTest)
if(BUILD_TESTING)
    include(Testing)
    if(BUILD_LEET_CODE)
        add_subdirectory(test/cpp/leet_code)
    endif()
    if(BUILD_SYSTEMS)
        add_subdirectory(test/cpp/systems)
    endif()
endif()
