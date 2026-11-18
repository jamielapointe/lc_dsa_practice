cmake_minimum_required(VERSION 4.5)

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
    COMPAT_VERSION 0.1.0
    SPDX_LICENSE "Apache-2.0"
    DESCRIPTION
        "ISO C23 and C++23 LeetCode and low-level systems practice with modern CMake and strict tooling"
    LANGUAGES CXX C
)

# Prevent in-source builds
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds are strictly prohibited. Use CMakePresets (build/<preset>)."
    )
endif()

# Enforce strict CMake 4.5+ diagnostics in root CMakeLists.txt (rejects non-target directives,
# absolute install destinations, and experimental gates while keeping CMakePresets.json on IDE-safe
# Schema v11).
cmake_diagnostic(SET CMD_STRICT WARN RECURSE)
cmake_diagnostic(SET CMD_NON_TARGET_DIRECTIVE SEND_ERROR)
cmake_diagnostic(SET CMD_INSTALL_ABSOLUTE_DESTINATION SEND_ERROR)
cmake_diagnostic(SET CMD_EXPERIMENTAL SEND_ERROR)

# Default C++20/23 module scanning OFF so ccache, clang-tidy, and misc-include-cleaner stay
# deterministic; opt in explicitly for standalone CMake 4.5 GA `import std`.
option(ENABLE_CXX_MODULES "Enable C++20/23 module scanning and GA import std" OFF)
if(ENABLE_CXX_MODULES)
    set(CMAKE_CXX_SCAN_FOR_MODULES ON)
else()
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
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

# Optional CMake 4.3+ build telemetry and Chrome trace export
option(ENABLE_BUILD_INSTRUMENTATION "Enable CMake build telemetry and Chrome trace export" OFF)
if(ENABLE_BUILD_INSTRUMENTATION)
    cmake_instrumentation(
        API_VERSION 1
        DATA_VERSION 1.2
        HOOKS postGenerate preCMakeBuild postCMakeBuild
        OPTIONS staticSystemInformation compileTrace trace
    )
endif()

# Project-level switches for the C and C++ trees
option(BUILD_LEET_CODE "Build the C and C++ LeetCode practice libraries and tests" ON)
option(BUILD_SYSTEMS "Build the low-level / systems / embedded C and C++ libraries and tests" ON)
option(BUILD_INTEROP "Build Google Crubit C++23 <-> Rust 2024 interop targets and tests" OFF)
option(BUILD_EXAMPLES "Build the example executables" ON)
option(BUILD_BENCHMARKS "Build the benchmark targets" OFF)

# Register modular cmake helper path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Include modular configuration components
block(SCOPE_FOR POLICIES DIAGNOSTICS)
    include(ProjectOptions)
    include(CompilerWarnings)
    include(StaticAnalysis)
    include(Sanitizers)
    include(Coverage)
    include(Architecture)
    include(Dependencies)
    include(Formatting)
endblock()

include(GNUInstallDirs)
option(INSTALL_LC_DSA "Enable installation, CPS PACKAGE_INFO, and SPDX 3.0.1 SBOM generation" ON)

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

if(BUILD_INTEROP)
    include(CrubitInterop)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp/interop/crubit/CMakeLists.txt")
        add_subdirectory(src/cpp/interop/crubit)
    endif()
    if(BUILD_EXAMPLES)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/interop/crubit/CMakeLists.txt")
            add_subdirectory(examples/interop/crubit)
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
    if(BUILD_INTEROP)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/cpp/interop/crubit/CMakeLists.txt")
            add_subdirectory(test/cpp/interop/crubit)
        endif()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/rust/interop/crubit/CMakeLists.txt")
            add_subdirectory(test/rust/interop/crubit)
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

# Install internal interface targets, CPS PACKAGE_INFO descriptor, and SPDX 3.0.1 SBOM
if(INSTALL_LC_DSA AND (BUILD_LEET_CODE OR BUILD_SYSTEMS))
    install(
        TARGETS project_options project_warnings project_sanitizers project_architecture
        EXPORT lc_dsa_practice_targets
    )
    install(PACKAGE_INFO lc_dsa_practice EXPORT lc_dsa_practice_targets LOWER_CASE_FILE)
    # gersemi: off
    install(
        SBOM lc_dsa_practice
        EXPORTS lc_dsa_practice_targets
        DATA_LICENSE "CC0-1.0"
        DEFAULT_LICENSE "${PROJECT_SPDX_LICENSE}"
        FORMAT spdx-3.0.1+json
    )
    # gersemi: on
endif()
