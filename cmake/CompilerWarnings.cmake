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
    -Wcast-align
    -Wunused
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
    $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
    $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
    $<$<COMPILE_LANGUAGE:C>:-Wmissing-declarations>
    $<$<COMPILE_LANGUAGE:C>:-Wold-style-definition>
    $<$<COMPILE_LANGUAGE:C>:-Wbad-function-cast>
    $<$<COMPILE_LANGUAGE:C>:-Wvla>
    $<$<COMPILE_LANGUAGE:C>:-Wcast-qual>
    $<$<COMPILE_LANGUAGE:C>:-Wwrite-strings>
    $<$<COMPILE_LANGUAGE:C>:-Wundef>
    $<$<COMPILE_LANGUAGE:C>:-Wswitch-enum>
)

set(MSVC_WARNINGS
    /W4
    /w14242
    /w14254
    $<$<COMPILE_LANGUAGE:CXX>:/w14263>
    $<$<COMPILE_LANGUAGE:CXX>:/w14265>
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
    $<$<COMPILE_LANGUAGE:CXX>:/permissive->
)

option(WARNINGS_AS_ERRORS "Treat compiler warnings as fatal errors" ON)
if(WARNINGS_AS_ERRORS)
    list(APPEND CLANG_GCC_WARNINGS -Werror)
    list(APPEND MSVC_WARNINGS /WX)
endif()

if(
    CMAKE_CXX_COMPILER_ID MATCHES ".*Clang"
    OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"
    OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang"
    OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang"
    OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
    OR CMAKE_C_COMPILER_ID STREQUAL "GNU"
)
    target_compile_options(project_warnings INTERFACE ${CLANG_GCC_WARNINGS})
elseif(MSVC)
    target_compile_options(project_warnings INTERFACE ${MSVC_WARNINGS})
endif()
