# cmake/StaticAnalysis.cmake Enforces optional clang-tidy static analysis on all C and C++
# translation units.

option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" ON)

if(ENABLE_CLANG_TIDY AND NOT CMAKE_CROSSCOMPILING)
    find_program(CLANG_TIDY_PROGRAM NAMES clang-tidy clang-tidy-23)
    if(CLANG_TIDY_PROGRAM)
        message(STATUS "clang-tidy static analysis enabled: ${CLANG_TIDY_PROGRAM}")
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_PROGRAM};--extra-arg=-Wno-unknown-warning-option")
        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_PROGRAM};--extra-arg=-Wno-unknown-warning-option")
    else()
        message(
            FATAL_ERROR
            "clang-tidy executable was not found. clang-tidy is required when ENABLE_CLANG_TIDY=ON."
        )
    endif()
elseif(CMAKE_CROSSCOMPILING)
    message(STATUS "clang-tidy static analysis skipped during cross-compilation")
else()
    message(STATUS "clang-tidy static analysis disabled (ENABLE_CLANG_TIDY=OFF)")
endif()
