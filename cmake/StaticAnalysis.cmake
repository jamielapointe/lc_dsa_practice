# cmake/StaticAnalysis.cmake Enforces mandatory clang-tidy static analysis on all C and C++
# translation units.

find_program(CLANG_TIDY_PROGRAM NAMES clang-tidy clang-tidy-23)
if(CLANG_TIDY_PROGRAM)
    message(STATUS "clang-tidy static analysis enabled: ${CLANG_TIDY_PROGRAM}")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_PROGRAM};--extra-arg=-Wno-unknown-warning-option")
    set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_PROGRAM};--extra-arg=-Wno-unknown-warning-option")
else()
    message(
        FATAL_ERROR
        "clang-tidy executable was not found. clang-tidy is required for all builds."
    )
endif()
