# test/rust/interop/crubit/CMakeLists.txt Rust 2024 -> C++23 Crubit integration tests.

if(
    EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/crubit_cpp_from_rust_tests/Cargo.toml"
    AND TARGET cc_primitives_enums
    AND TARGET cc_references_views
    AND TARGET cc_ownership_raii
    AND TARGET cc_traits_errors
    AND TARGET cc_telemetry_bridge
)
    crubit_add_rust_binary_or_test(
        crubit_cpp_from_rust_tests
        MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/crubit_cpp_from_rust_tests/Cargo.toml"
        TEST
        API_TARGETS
            cc_primitives_enums
            cc_references_views
            cc_ownership_raii
            cc_traits_errors
            cc_telemetry_bridge
        LABELS interop
    )
endif()
