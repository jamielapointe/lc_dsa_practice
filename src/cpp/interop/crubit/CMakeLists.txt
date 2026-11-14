# src/cpp/interop/crubit/CMakeLists.txt Google Crubit C++23 <-> Rust 2024 interop libraries and
# generated bindings.

function(interop_add_cpp_library target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "interop_add_cpp_library called without SOURCES for ${target_name}")
    endif()

    add_library(${target_name} STATIC ${ARG_SOURCES})
    target_include_directories(
        ${target_name}
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${target_name} PUBLIC cxx_std_23)
    target_compile_options(${target_name} PUBLIC -stdlib=libc++)
    target_link_options(${target_name} PUBLIC -stdlib=libc++)
    if(UNIX AND NOT APPLE AND DEFINED ENV{CONDA_PREFIX})
        target_link_directories(${target_name} PUBLIC "$ENV{CONDA_PREFIX}/lib")
        target_link_options(${target_name} PUBLIC "-Wl,-rpath,$ENV{CONDA_PREFIX}/lib")
    endif()
    target_link_libraries(
        ${target_name}
        PRIVATE project_options project_warnings project_architecture
    )

    if(CMAKE_CXX_CLANG_TIDY)
        set_target_properties(
            ${target_name}
            PROPERTIES
                CXX_CLANG_TIDY
                    "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/src/include/interop/crubit/.*"
        )
    endif()
endfunction()

interop_add_cpp_library(interop_cc_primitives_enums SOURCES primitives_and_enums.cpp)
interop_add_cpp_library(interop_cc_references_views SOURCES references_and_views.cpp)
interop_add_cpp_library(interop_cc_ownership_raii SOURCES ownership_and_raii.cpp)
interop_add_cpp_library(interop_cc_traits_errors SOURCES traits_and_errors.cpp)
interop_add_cpp_library(interop_cc_telemetry_bridge SOURCES telemetry_safety_bridge.cpp)

crubit_add_rust_api_from_cpp(
    cc_primitives_enums
    CPP_TARGET interop_cc_primitives_enums
    CRATE_NAME cc_primitives_enums
    PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/src/include/interop/crubit/primitives_and_enums.hpp"
)

crubit_add_rust_api_from_cpp(
    cc_references_views
    CPP_TARGET interop_cc_references_views
    CRATE_NAME cc_references_views
    PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/src/include/interop/crubit/references_and_views.hpp"
)

crubit_add_rust_api_from_cpp(
    cc_ownership_raii
    CPP_TARGET interop_cc_ownership_raii
    CRATE_NAME cc_ownership_raii
    PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/src/include/interop/crubit/ownership_and_raii.hpp"
)

crubit_add_rust_api_from_cpp(
    cc_traits_errors
    CPP_TARGET interop_cc_traits_errors
    CRATE_NAME cc_traits_errors
    PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/src/include/interop/crubit/traits_and_errors.hpp"
)

crubit_add_rust_api_from_cpp(
    cc_telemetry_bridge
    CPP_TARGET interop_cc_telemetry_bridge
    CRATE_NAME cc_telemetry_bridge
    PUBLIC_HEADERS "${PROJECT_SOURCE_DIR}/src/include/interop/crubit/telemetry_safety_bridge.hpp"
)

if(EXISTS "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_primitives_enums/Cargo.toml")
    crubit_add_cpp_api_from_rust(
        rs_primitives_enums_cc_api
        MANIFEST_PATH "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_primitives_enums/Cargo.toml"
        CRATE_NAME rs_primitives_enums
    )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_references_views/Cargo.toml")
    crubit_add_cpp_api_from_rust(
        rs_references_views_cc_api
        MANIFEST_PATH "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_references_views/Cargo.toml"
        CRATE_NAME rs_references_views
    )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_ownership_raii/Cargo.toml")
    crubit_add_cpp_api_from_rust(
        rs_ownership_raii_cc_api
        MANIFEST_PATH "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_ownership_raii/Cargo.toml"
        CRATE_NAME rs_ownership_raii
    )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_traits_errors/Cargo.toml")
    crubit_add_cpp_api_from_rust(
        rs_traits_errors_cc_api
        MANIFEST_PATH "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_traits_errors/Cargo.toml"
        CRATE_NAME rs_traits_errors
    )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_telemetry_validator/Cargo.toml")
    crubit_add_cpp_api_from_rust(
        rs_telemetry_validator_cc_api
        MANIFEST_PATH "${PROJECT_SOURCE_DIR}/src/rust/interop/crubit/rs_telemetry_validator/Cargo.toml"
        CRATE_NAME rs_telemetry_validator
    )
endif()
