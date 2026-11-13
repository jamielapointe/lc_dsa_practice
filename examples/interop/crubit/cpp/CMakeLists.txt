# examples/interop/crubit/cpp/CMakeLists.txt C++23 demo binaries exercising Rust 2024 crates via
# Crubit (one Rust staticlib per binary).

function(interop_add_cpp_demo target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "interop_add_cpp_demo called without SOURCES for ${target_name}")
    endif()

    add_executable(${target_name} ${ARG_SOURCES})
    target_include_directories(
        ${target_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/include
    )
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_compile_options(${target_name} PRIVATE -stdlib=libc++)
    target_link_options(${target_name} PRIVATE -stdlib=libc++)
    target_link_libraries(
        ${target_name}
        PRIVATE ${ARG_LIBS} project_options project_warnings project_architecture
    )
endfunction()

if(TARGET rs_primitives_enums_cc_api)
    interop_add_cpp_demo(
        crubit_demo_primitives_enums
        SOURCES demo_primitives_enums.cpp
        LIBS rs_primitives_enums_cc_api interop_cc_primitives_enums
    )
endif()

if(TARGET rs_references_views_cc_api)
    interop_add_cpp_demo(
        crubit_demo_references_views
        SOURCES demo_references_views.cpp
        LIBS rs_references_views_cc_api interop_cc_references_views
    )
endif()

if(TARGET rs_ownership_raii_cc_api)
    interop_add_cpp_demo(
        crubit_demo_ownership_raii
        SOURCES demo_ownership_raii.cpp
        LIBS rs_ownership_raii_cc_api interop_cc_ownership_raii
    )
endif()

if(TARGET rs_traits_errors_cc_api)
    interop_add_cpp_demo(
        crubit_demo_traits_errors
        SOURCES demo_traits_errors.cpp
        LIBS rs_traits_errors_cc_api interop_cc_traits_errors
    )
endif()

if(TARGET rs_telemetry_validator_cc_api)
    interop_add_cpp_demo(
        crubit_demo_telemetry_capstone
        SOURCES demo_telemetry_capstone.cpp
        LIBS rs_telemetry_validator_cc_api interop_cc_telemetry_bridge
    )
endif()
