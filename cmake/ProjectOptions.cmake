# cmake/ProjectOptions.cmake Encapsulates core build options, compiler launchers, output paths, and
# definitions.

add_library(project_options INTERFACE)

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
find_program(CLANG_TIDY_PROGRAM NAMES clang-tidy clang-tidy-23 clang-tidy-21)
if(CLANG_TIDY_PROGRAM)
  message(STATUS "clang-tidy static analysis enabled: ${CLANG_TIDY_PROGRAM}")
  set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_PROGRAM};--extra-arg=-Wno-unknown-warning-option")
else()
  message(FATAL_ERROR "clang-tidy executable was not found. It is required for this project.")
endif()

# Native Linker Selection (LLD on Linux/ELF)
if(NOT DEFINED CMAKE_LINKER_TYPE
   AND NOT APPLE
   AND NOT WIN32)
  find_program(LLD_PROGRAM ld.lld)
  if(LLD_PROGRAM)
    set(CMAKE_LINKER_TYPE
        "LLD"
        CACHE STRING "Linker type" FORCE)
    message(STATUS "Defaulting linker to LLD: ${LLD_PROGRAM}")
  endif()
endif()

# Target compile definitions
target_compile_definitions(project_options INTERFACE $<$<CONFIG:Debug>:DEBUG=1> $<$<CONFIG:Release>:NDEBUG=1>)
