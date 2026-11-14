# examples/interop/crubit/CMakeLists.txt C++23 and Rust 2024 Crubit showcase demos.

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpp/CMakeLists.txt")
    add_subdirectory(cpp)
endif()

if(
    EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/rust/crubit_showcase_rust_demo/Cargo.toml"
    AND TARGET cc_primitives_enums
    AND TARGET cc_references_views
    AND TARGET cc_ownership_raii
    AND TARGET cc_traits_errors
    AND TARGET cc_telemetry_bridge
)
    crubit_add_rust_binary_or_test(
        crubit_showcase_rust_demo
        MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/rust/crubit_showcase_rust_demo/Cargo.toml"
        API_TARGETS
            cc_primitives_enums
            cc_references_views
            cc_ownership_raii
            cc_traits_errors
            cc_telemetry_bridge
    )
endif()
