# src/c/leet_code/CMakeLists.txt Scaffolded for future ISO C23 LeetCode practice problems. Public
# headers live in src/include/leet_code/ and are included as <leet_code/...>.
#
# To register a new problem <slug>: leet_code_c_add_library(<slug> SOURCES <slug>.c [LIBS ...])
# Tests live in test/c/leet_code/ and link leet_code_c::<slug>.

# Helper function to register a C LeetCode static library target
function(leet_code_c_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "leet_code_c_add_library called without SOURCES for target ${name}")
    endif()

    set(target_name "leet_code_c_${name}")
    add_library(${target_name} STATIC ${ARG_SOURCES})
    add_library(leet_code_c::${name} ALIAS ${target_name})

    target_include_directories(
        ${target_name}
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${target_name} PUBLIC c_std_23)
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
endfunction()
