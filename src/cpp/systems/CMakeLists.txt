# src/cpp/systems/CMakeLists.txt Low-level / systems / embedded-flavored C++ libraries. Public headers
# live in src/include/systems/ and are included as <systems/...>.
#
# To add a component <name>: create src/cpp/systems/<name>.cpp and call systems_add_library(<name>
# SOURCES <name>.cpp [LIBS ...]). Tests live in test/cpp/systems/ and link systems::<name>.

find_package(Threads REQUIRED)

# Registers a static library `<name>` (alias `systems::<name>`) with the project-wide options.
function(systems_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "systems_add_library called without SOURCES for target ${name}")
    endif()

    add_library(${name} STATIC ${ARG_SOURCES})
    add_library(systems::${name} ALIAS ${name})

    target_include_directories(
        ${name}
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${name} PUBLIC cxx_std_23)
    target_link_libraries(
        ${name}
        PRIVATE project_options project_warnings project_sanitizers project_architecture
        PUBLIC ${ARG_LIBS}
    )

    # Systems headers are header-heavy templates; make clang-tidy analyze them as well as the .cpp
    # files (the repo-wide .clang-tidy deliberately sets no header filter).
    if(CMAKE_CXX_CLANG_TIDY)
        set_target_properties(
            ${name}
            PROPERTIES
                CXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/src/include/systems/.*"
        )
    endif()
endfunction()

# Fixed-capacity, thread-safe MPMC FIFO. The class is a header-only template; the .cpp provides an
# explicit instantiation so the template is always compiled and linted by the build.
systems_add_library(bounded_fifo SOURCES bounded_fifo.cpp LIBS Threads::Threads)
