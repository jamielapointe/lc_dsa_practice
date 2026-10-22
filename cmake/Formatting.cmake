# cmake/Formatting.cmake Configures custom targets 'format' and 'format-check' using clang-format.

find_program(CLANG_FORMAT_PROGRAM NAMES clang-format clang-format-23)

if(CLANG_FORMAT_PROGRAM)
    file(
        GLOB_RECURSE ALL_C_CXX_SOURCES
        CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/include/*.hpp"
        "${CMAKE_SOURCE_DIR}/src/include/*.h"
        "${CMAKE_SOURCE_DIR}/src/cpp/*.cpp"
        "${CMAKE_SOURCE_DIR}/src/cpp/*.hpp"
        "${CMAKE_SOURCE_DIR}/src/cpp/*.h"
        "${CMAKE_SOURCE_DIR}/src/c/*.c"
        "${CMAKE_SOURCE_DIR}/src/c/*.h"
        "${CMAKE_SOURCE_DIR}/test/cpp/*.cpp"
        "${CMAKE_SOURCE_DIR}/test/cpp/*.hpp"
        "${CMAKE_SOURCE_DIR}/test/cpp/*.h"
        "${CMAKE_SOURCE_DIR}/test/c/*.c"
        "${CMAKE_SOURCE_DIR}/test/c/*.cpp"
        "${CMAKE_SOURCE_DIR}/test/c/*.hpp"
        "${CMAKE_SOURCE_DIR}/test/c/*.h"
        "${CMAKE_SOURCE_DIR}/examples/cpp/*.cpp"
        "${CMAKE_SOURCE_DIR}/examples/cpp/*.hpp"
        "${CMAKE_SOURCE_DIR}/examples/cpp/*.h"
        "${CMAKE_SOURCE_DIR}/examples/c/*.c"
        "${CMAKE_SOURCE_DIR}/examples/c/*.h"
        "${CMAKE_SOURCE_DIR}/benchmarks/cpp/*.cpp"
        "${CMAKE_SOURCE_DIR}/benchmarks/cpp/*.hpp"
        "${CMAKE_SOURCE_DIR}/benchmarks/cpp/*.h"
        "${CMAKE_SOURCE_DIR}/benchmarks/c/*.c"
        "${CMAKE_SOURCE_DIR}/benchmarks/c/*.cpp"
        "${CMAKE_SOURCE_DIR}/benchmarks/c/*.hpp"
        "${CMAKE_SOURCE_DIR}/benchmarks/c/*.h"
    )

    if(ALL_C_CXX_SOURCES)
        add_custom_target(
            format
            COMMAND ${CLANG_FORMAT_PROGRAM} -i ${ALL_C_CXX_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Formatting C and C++ source files with clang-format"
            VERBATIM
        )

        add_custom_target(
            format-check
            COMMAND ${CLANG_FORMAT_PROGRAM} --dry-run --Werror ${ALL_C_CXX_SOURCES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Checking C and C++ source formatting with clang-format"
            VERBATIM
        )
    else()
        add_custom_target(
            format
            COMMAND ${CMAKE_COMMAND} -E echo "No C/C++ source files found to format."
            COMMENT "No C/C++ files to format"
            VERBATIM
        )

        add_custom_target(
            format-check
            COMMAND ${CMAKE_COMMAND} -E echo "No C/C++ source files found to check."
            COMMENT "No C/C++ files to check"
            VERBATIM
        )
    endif()
else()
    message(
        WARNING
        "clang-format not found. Targets 'format' and 'format-check' will not be available."
    )
endif()
