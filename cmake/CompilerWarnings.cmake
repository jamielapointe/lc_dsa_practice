# cmake/CompilerWarnings.cmake Encapsulates strict compiler warning configurations for project
# targets on Linux and macOS (Clang, AppleClang, GNU).

add_library(project_warnings INTERFACE)

option(WARNINGS_AS_ERRORS "Treat compiler warnings as fatal errors" ON)

block(SCOPE_FOR VARIABLES POLICIES DIAGNOSTICS)
    foreach(_lang IN ITEMS C CXX)
        if(
            DEFINED CMAKE_${_lang}_COMPILER_ID
            AND NOT CMAKE_${_lang}_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$"
        )
            message(
                FATAL_ERROR
                "Unsupported ${_lang} compiler: ${CMAKE_${_lang}_COMPILER_ID}. "
                "Only Clang, AppleClang, and GNU are supported."
            )
        endif()
    endforeach()

    set(_common_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wcast-align
        -Wunused
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wformat-security
        -Wimplicit-fallthrough
    )

    if(WARNINGS_AS_ERRORS)
        list(APPEND _common_warnings -Werror)
    endif()

    set(_cxx_warnings -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)

    set(_c_warnings
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wmissing-declarations
        -Wold-style-definition
        -Wbad-function-cast
        -Wvla
        -Wcast-qual
        -Wwrite-strings
        -Wundef
        -Wswitch-enum
    )

    set(_gcc_warnings -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op)

    target_compile_options(
        project_warnings
        INTERFACE
            "$<BUILD_INTERFACE:${_common_warnings}>"
            "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:${_cxx_warnings}>>"
            "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:C>:${_c_warnings}>>"
            "$<BUILD_INTERFACE:$<$<AND:$<COMPILE_LANGUAGE:C>,$<C_COMPILER_ID:GNU>>:${_gcc_warnings}>>"
            "$<BUILD_INTERFACE:$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:GNU>>:${_gcc_warnings}>>"
    )
endblock()
