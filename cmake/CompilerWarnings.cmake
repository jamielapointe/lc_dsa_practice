# cmake/CompilerWarnings.cmake Encapsulates strict compiler warning configurations for project
# targets.

add_library(project_warnings INTERFACE)

set(CLANG_GCC_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
)

set(MSVC_WARNINGS
    /W4
    /w14242
    /w14254
    /w14263
    /w14265
    /w14287
    /we4289
    /w14296
    /w14311
    /w14545
    /w14546
    /w14547
    /w14549
    /w14555
    /w14619
    /w14640
    /w14826
    /w14905
    /w14906
    /w14928
    /permissive-
)

option(WARNINGS_AS_ERRORS "Treat compiler warnings as fatal errors" ON)
if(WARNINGS_AS_ERRORS)
    list(APPEND CLANG_GCC_WARNINGS -Werror)
    list(APPEND MSVC_WARNINGS /WX)
endif()

if(
    CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
    OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
    OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
)
    target_compile_options(project_warnings INTERFACE ${CLANG_GCC_WARNINGS})
elseif(MSVC)
    target_compile_options(project_warnings INTERFACE ${MSVC_WARNINGS})
endif()
