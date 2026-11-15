# src/c/leet_code/CMakeLists.txt Scaffolded for future ISO C23 LeetCode practice problems. Public
# headers live in src/include/leet_code/ and are included as <leet_code/...>.
#
# To register a new problem <slug>: leet_code_c_add_library(<slug> SOURCES <slug>.c [LIBS ...])
# Tests live in test/c/leet_code/ and link leet_code_c::<slug>.

# Helper function to register a C LeetCode static library target
function(leet_code_c_add_library name)
    cmake_parse_arguments(ARG "" "" "HEADERS;SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "leet_code_c_add_library called without SOURCES for target ${name}")
    endif()

    if(NOT ARG_HEADERS)
        set(ARG_HEADERS "${PROJECT_SOURCE_DIR}/src/include/leet_code/${name}.h")
    endif()

    set(spdx_license "Apache-2.0")
    if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
        set(spdx_license "${PROJECT_SPDX_LICENSE}")
    endif()

    set(target_name "leet_code_c_${name}")
    add_library(${target_name} STATIC)
    add_library(leet_code_c::${name} ALIAS ${target_name})

    target_sources(
        ${target_name}
        PUBLIC FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include" FILES ${ARG_HEADERS}
        PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES}
    )
    target_compile_features(${target_name} PUBLIC c_std_23)
    target_compile_options(
        ${target_name}
        PRIVATE $<$<COMPILE_LANGUAGE:C>:-ftrivial-auto-var-init=zero>
    )
    target_compile_definitions(
        ${target_name}
        PUBLIC $<BUILD_INTERFACE:$<$<BOOL:${HAVE_STDBIT_H}>:HAVE_STDBIT_H=1>>
    )
    target_link_libraries(
        ${target_name}
        PRIVATE project_options project_warnings project_sanitizers project_architecture
        PUBLIC ${ARG_LIBS}
    )

    if(CMAKE_C_CLANG_TIDY)
        set_target_properties(
            ${target_name}
            PROPERTIES
                C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--header-filter=.*/src/include/leet_code/.*"
        )
    endif()

    set_target_properties(
        leet_code_c::${name}
        PROPERTIES EXPORT_NAME "${target_name}" SPDX_LICENSE "${spdx_license}"
    )

    if(INSTALL_LC_DSA)
        install(
            TARGETS leet_code_c::${name}
            EXPORT lc_dsa_practice_targets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            FILE_SET HEADERS
        )
    endif()
endfunction()
