# cmake/Coverage.cmake Optional LLVM source-based code coverage instrumentation.
#
# Enable with -DENABLE_COVERAGE=ON (see the `coverage` preset). Instrumented test binaries write
# raw profiles (see LLVM_PROFILE_FILE in the `coverage` test preset); `scripts/ci/cpp_coverage.py`
# merges them with llvm-profdata and enforces the line-coverage gate with llvm-cov.

option(ENABLE_COVERAGE "Instrument C++ code for LLVM source-based coverage" OFF)

if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        message(FATAL_ERROR "ENABLE_COVERAGE requires a Clang-based compiler.")
    endif()
    # Every project target links project_options, so the flags propagate everywhere.
    target_compile_options(project_options INTERFACE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(project_options INTERFACE -fprofile-instr-generate)
    message(STATUS "LLVM source-based coverage instrumentation enabled")
endif()
