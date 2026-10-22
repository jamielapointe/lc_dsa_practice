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
        "ISO C23 and C++23 LeetCode and low-level systems practice with modern CMake and strict tooling"
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

# Enforce C23 standard globally
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Export compilation database for clangd, clang-tidy, and pre-commit
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Project-level switches for the C and C++ trees
option(BUILD_LEET_CODE "Build the C and C++ LeetCode practice libraries and tests" ON)
option(BUILD_SYSTEMS "Build the low-level / systems / embedded C and C++ libraries and tests" ON)
option(BUILD_EXAMPLES "Build the example executables" ON)
option(BUILD_BENCHMARKS "Build the benchmark targets" OFF)

# Register modular cmake helper path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include modular configuration components
include(ProjectOptions)
include(CompilerWarnings)
include(StaticAnalysis)
include(Sanitizers)
include(Coverage)
include(Architecture)
include(Dependencies)
include(Formatting)

# C and C++ libraries: src/{cpp,c}/<tree>/ with headers in src/include/<tree>/
if(BUILD_LEET_CODE)
    add_subdirectory(src/cpp/leet_code)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/c/leet_code/CMakeLists.txt")
        add_subdirectory(src/c/leet_code)
    endif()
endif()
if(BUILD_SYSTEMS)
    add_subdirectory(src/cpp/systems)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/c/systems/CMakeLists.txt")
        add_subdirectory(src/c/systems)
    endif()
    if(BUILD_EXAMPLES)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/cpp/systems/CMakeLists.txt")
            add_subdirectory(examples/cpp/systems)
        endif()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/c/systems/CMakeLists.txt")
            add_subdirectory(examples/c/systems)
        endif()
    endif()
endif()

# Tests: test/{cpp,c}/<tree>/
include(CTest)
if(BUILD_TESTING)
    include(Testing)
    if(BUILD_LEET_CODE)
        add_subdirectory(test/cpp/leet_code)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/c/leet_code/CMakeLists.txt")
            add_subdirectory(test/c/leet_code)
        endif()
    endif()
    if(BUILD_SYSTEMS)
        add_subdirectory(test/cpp/systems)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/c/systems/CMakeLists.txt")
            add_subdirectory(test/c/systems)
        endif()
    endif()
endif()

# Benchmarks: benchmarks/{cpp,c}/<tree>/
if(BUILD_BENCHMARKS)
    include(Benchmarking)
    if(BUILD_SYSTEMS)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/cpp/systems/CMakeLists.txt")
            add_subdirectory(benchmarks/cpp/systems)
        endif()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/c/systems/CMakeLists.txt")
            add_subdirectory(benchmarks/c/systems)
        endif()
    endif()
endif()
