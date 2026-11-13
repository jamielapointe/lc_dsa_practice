# test/cpp/interop/crubit/CMakeLists.txt C++23 -> Rust 2024 Crubit interop tests (libc++ only,
# at most one Rust staticlib per binary).

function(interop_add_cpp_test target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;LABELS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "interop_add_cpp_test called without SOURCES for ${target_name}")
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

    add_test(NAME "${target_name}" COMMAND ${target_name})
    set(test_props TIMEOUT 60)
    if(ARG_LABELS)
        list(APPEND test_props LABELS "${ARG_LABELS}")
    endif()
    set_tests_properties("${target_name}" PROPERTIES ${test_props})
endfunction()

if(TARGET rs_primitives_enums_cc_api)
    interop_add_cpp_test(
        interop_cpp_primitives_enums_test
        SOURCES primitives_enums_test.cpp
        LIBS rs_primitives_enums_cc_api interop_cc_primitives_enums
        LABELS interop
    )
endif()

if(TARGET rs_references_views_cc_api)
    interop_add_cpp_test(
        interop_cpp_references_views_test
        SOURCES references_views_test.cpp
        LIBS rs_references_views_cc_api interop_cc_references_views
        LABELS interop
    )
endif()

if(TARGET rs_ownership_raii_cc_api)
    interop_add_cpp_test(
        interop_cpp_ownership_raii_test
        SOURCES ownership_raii_test.cpp
        LIBS rs_ownership_raii_cc_api interop_cc_ownership_raii
        LABELS interop
    )
endif()

if(TARGET rs_traits_errors_cc_api)
    interop_add_cpp_test(
        interop_cpp_traits_errors_test
        SOURCES traits_errors_test.cpp
        LIBS rs_traits_errors_cc_api interop_cc_traits_errors
        LABELS interop
    )
endif()

if(TARGET rs_telemetry_validator_cc_api)
    interop_add_cpp_test(
        interop_cpp_telemetry_safety_capstone_test
        SOURCES telemetry_safety_capstone_test.cpp
        LIBS rs_telemetry_validator_cc_api interop_cc_telemetry_bridge
        LABELS interop
    )
endif()
