# cmake/ProjectOptions.cmake Encapsulates core build options, compiler launchers, output paths, and
# definitions.

add_library(project_options INTERFACE)

# Hermetic toolchain guard: all builds must use the Pixi-provided compiler (.pixi/envs/...). Set
# -DALLOW_NON_PIXI_TOOLCHAIN=ON to bypass (e.g. quick experiments with a system compiler).
option(ALLOW_NON_PIXI_TOOLCHAIN "Allow building with a compiler outside .pixi/envs/" OFF)
if(NOT ALLOW_NON_PIXI_TOOLCHAIN AND NOT CMAKE_CXX_COMPILER MATCHES "\\.pixi/envs/")
    message(
        FATAL_ERROR
        "Hermetic toolchain violation: CMAKE_CXX_COMPILER ('${CMAKE_CXX_COMPILER}') is not located in "
        ".pixi/envs/. Run builds through Pixi (e.g. 'pixi run cpp-debug') or pass -DALLOW_NON_PIXI_TOOLCHAIN=ON."
    )
endif()

# Compiler launcher acceleration (ccache)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache compiler launcher found: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Position Independent Code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Standardized output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Interprocedural Optimization (IPO / LTO)
option(ENABLE_IPO "Enable Interprocedural Optimization (LTO)" OFF)
if(ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "Interprocedural Optimization (IPO/LTO) enabled")
    else()
        message(WARNING "IPO requested but not supported: ${ipo_error}")
    endif()
endif()

# Static Analysis (clang-tidy during build)
option(ENABLE_CLANG_TIDY "Run clang-tidy on every C++ translation unit during the build" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_PROGRAM NAMES clang-tidy clang-tidy-23)
    if(CLANG_TIDY_PROGRAM)
        message(STATUS "clang-tidy static analysis enabled: ${CLANG_TIDY_PROGRAM}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_PROGRAM};--extra-arg=-Wno-unknown-warning-option")
    else()
        message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON but the clang-tidy executable was not found.")
    endif()
endif()

# Native Linker Selection (LLD on Linux/ELF)
if(NOT DEFINED CMAKE_LINKER_TYPE AND NOT APPLE AND NOT WIN32)
    find_program(LLD_PROGRAM ld.lld)
    if(LLD_PROGRAM)
        set(CMAKE_LINKER_TYPE "LLD" CACHE STRING "Linker type" FORCE)
        message(STATUS "Defaulting linker to LLD: ${LLD_PROGRAM}")
    endif()
endif()

# Target compile definitions
target_compile_definitions(
    project_options
    INTERFACE $<$<CONFIG:Debug>:DEBUG=1> $<$<CONFIG:Release>:NDEBUG=1>
)
