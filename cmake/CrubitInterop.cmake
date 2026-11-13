# cmake/CrubitInterop.cmake Provides Crubit bidirectional C++23 <-> Rust 2024 build rules:
# - crubit_add_cpp_api_from_rust
# - crubit_add_rust_api_from_cpp
# - crubit_add_rust_binary_or_test

include_guard(GLOBAL)

get_filename_component(_CRUBIT_INTEROP_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(CRUBIT_DIR
    "${_CRUBIT_INTEROP_REPO_ROOT}/build/crubit"
    CACHE PATH
    "Directory containing bootstrapped Crubit toolchain and binaries"
)
set(_CRUBIT_INVOKE_RS_BINDINGS_SCRIPT
    "${_CRUBIT_INTEROP_REPO_ROOT}/scripts/crubit/invoke_rs_bindings_from_cc.py"
)

if(NOT EXISTS "${CRUBIT_DIR}/crubit_paths.cmake")
    message(
        FATAL_ERROR
        "Crubit paths file not found: '${CRUBIT_DIR}/crubit_paths.cmake'. "
        "Run 'pixi run crubit-bootstrap' before configuring with BUILD_INTEROP=ON."
    )
endif()

include("${CRUBIT_DIR}/crubit_paths.cmake")

foreach(
    _crubit_req_bin
    IN
    ITEMS
        CRUBIT_CARGO_EXECUTABLE
        CRUBIT_RUSTC_EXECUTABLE
        CRUBIT_RS_BINDINGS_FROM_CC_EXECUTABLE
        CRUBIT_CARGO_CPP_API_FROM_RUST_EXECUTABLE
)
    if(NOT EXISTS "${${_crubit_req_bin}}")
        message(
            FATAL_ERROR
            "Required Crubit tool missing (${_crubit_req_bin}='${${_crubit_req_bin}}'). "
            "Run 'pixi run crubit-bootstrap' to repair."
        )
    endif()
endforeach()

if(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/bin/python")
    set(CRUBIT_PYTHON_EXECUTABLE "$ENV{CONDA_PREFIX}/bin/python")
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(CRUBIT_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

find_program(CRUBIT_CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-23)

if(NOT TARGET crubit_support_cc)
    add_library(
        crubit_support_cc
        STATIC
        "${CRUBIT_SOURCE_DIR}/support/internal/check_no_mutable_aliasing.cc"
        "${CRUBIT_SOURCE_DIR}/support/rs_std/char.cc"
        "${CRUBIT_SOURCE_DIR}/support/rs_std/waker.cc"
    )
    add_library(crubit::support_cc ALIAS crubit_support_cc)
    set_target_properties(
        crubit_support_cc
        PROPERTIES
            CXX_CLANG_TIDY ""
            C_CLANG_TIDY ""
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
    )
    target_include_directories(crubit_support_cc SYSTEM PUBLIC "${CRUBIT_SOURCE_DIR}")
    target_compile_options(crubit_support_cc PUBLIC -stdlib=libc++ PRIVATE -Wno-everything)
    target_link_options(crubit_support_cc PUBLIC -stdlib=libc++)
    if(UNIX AND NOT APPLE AND DEFINED ENV{CONDA_PREFIX})
        target_link_directories(crubit_support_cc PUBLIC "$ENV{CONDA_PREFIX}/lib")
        target_link_options(crubit_support_cc PUBLIC "-Wl,-rpath,$ENV{CONDA_PREFIX}/lib")
    endif()
endif()

# Internal helper: resolve a path relative to CMAKE_CURRENT_SOURCE_DIR when not already absolute.
function(_crubit_resolve_source_path out_var raw_path)
    if(IS_ABSOLUTE "${raw_path}")
        set(${out_var} "${raw_path}" PARENT_SCOPE)
    else()
        set(${out_var} "${CMAKE_CURRENT_SOURCE_DIR}/${raw_path}" PARENT_SCOPE)
    endif()
endfunction()

# Internal helper: determine Cargo profile flags and target subdirectory from CMAKE_BUILD_TYPE.
function(_crubit_cargo_profile out_is_release out_profile_subdir)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(${out_is_release} TRUE PARENT_SCOPE)
        set(${out_profile_subdir} "release" PARENT_SCOPE)
    else()
        set(${out_is_release} FALSE PARENT_SCOPE)
        set(${out_profile_subdir} "debug" PARENT_SCOPE)
    endif()
endfunction()

# Internal helper: recursively collect non-interface C++ targets reachable via LINK_LIBRARIES.
function(_crubit_collect_link_targets out_var)
    set(visited "")
    set(ordered "")
    set(queue ${ARGN})
    while(queue)
        list(POP_FRONT queue tgt)
        if(NOT TARGET "${tgt}")
            continue()
        endif()
        list(FIND visited "${tgt}" idx)
        if(NOT idx EQUAL -1)
            continue()
        endif()
        list(APPEND visited "${tgt}")

        get_target_property(tgt_type "${tgt}" TYPE)
        if(NOT tgt_type STREQUAL "INTERFACE_LIBRARY")
            list(APPEND ordered "${tgt}")
        endif()

        get_target_property(iface_libs "${tgt}" INTERFACE_LINK_LIBRARIES)
        if(iface_libs)
            foreach(dep IN LISTS iface_libs)
                if(TARGET "${dep}")
                    list(APPEND queue "${dep}")
                endif()
            endforeach()
        endif()

        if(NOT tgt_type STREQUAL "INTERFACE_LIBRARY")
            get_target_property(priv_libs "${tgt}" LINK_LIBRARIES)
            if(priv_libs)
                foreach(dep IN LISTS priv_libs)
                    if(TARGET "${dep}")
                        list(APPEND queue "${dep}")
                    endif()
                endforeach()
            endif()
        endif()
    endwhile()
    set(${out_var} "${ordered}" PARENT_SCOPE)
endfunction()

# Internal helper: clean generator-wrapped include directories from a C++ target.
function(_crubit_extract_include_dirs out_var cpp_target)
    set(dirs "")
    get_target_property(raw_dirs "${cpp_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(raw_dirs)
        foreach(entry IN LISTS raw_dirs)
            if(entry MATCHES "^\\$<INSTALL_INTERFACE:.*>$")
                continue()
            elseif(entry MATCHES "^\\$<BUILD_INTERFACE:(.*)>$")
                list(APPEND dirs "${CMAKE_MATCH_1}")
            elseif(NOT entry MATCHES "^\\$<")
                list(APPEND dirs "${entry}")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES dirs)
    set(${out_var} "${dirs}" PARENT_SCOPE)
endfunction()

# Internal helper: clean non-generator compile definitions from a C++ target.
function(_crubit_extract_compile_defs out_var cpp_target)
    set(defs "")
    get_target_property(raw_defs "${cpp_target}" INTERFACE_COMPILE_DEFINITIONS)
    if(raw_defs)
        foreach(entry IN LISTS raw_defs)
            if(NOT entry MATCHES "^\\$<")
                list(APPEND defs "${entry}")
            endif()
        endforeach()
    endif()
    list(REMOVE_DUPLICATES defs)
    set(${out_var} "${defs}" PARENT_SCOPE)
endfunction()

# Generate C++ bindings and a Rust static library for a Cargo library crate via
# `cargo cpp_api_from_rust`.
function(crubit_add_cpp_api_from_rust target_name)
    cmake_parse_arguments(
        ARG
        "ALL_FEATURES;NO_DEFAULT_FEATURES"
        "MANIFEST_PATH;CRATE_NAME;OUT_DIR"
        "FEATURES"
        ${ARGN}
    )

    if(NOT ARG_MANIFEST_PATH)
        message(FATAL_ERROR "crubit_add_cpp_api_from_rust(${target_name}) requires MANIFEST_PATH")
    endif()

    _crubit_resolve_source_path(manifest_path "${ARG_MANIFEST_PATH}")
    get_filename_component(crate_dir "${manifest_path}" DIRECTORY)

    if(ARG_CRATE_NAME)
        set(crate_name "${ARG_CRATE_NAME}")
    else()
        string(REPLACE "-" "_" crate_name "${target_name}")
    endif()

    if(ARG_OUT_DIR)
        set(out_dir "${ARG_OUT_DIR}")
    else()
        set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/crubit_cpp_api/${target_name}")
    endif()

    set(cargo_target_dir "${CMAKE_BINARY_DIR}/crubit_cargo")
    _crubit_cargo_profile(is_release profile_subdir)

    set(generated_header "${out_dir}/include/crubit/${crate_name}.h")
    set(generated_lib "${cargo_target_dir}/${profile_subdir}/lib${crate_name}.a")

    file(GLOB_RECURSE rust_sources CONFIGURE_DEPENDS "${crate_dir}/src/*.rs")

    set(cmd_args
        --manifest-path
        "${manifest_path}"
        --target-dir
        "${cargo_target_dir}"
        --out-dir
        "${out_dir}"
    )
    if(ARG_ALL_FEATURES)
        list(APPEND cmd_args --all-features)
    endif()
    if(ARG_NO_DEFAULT_FEATURES)
        list(APPEND cmd_args --no-default-features)
    endif()
    if(ARG_FEATURES)
        string(JOIN "," features_csv ${ARG_FEATURES})
        list(APPEND cmd_args --features "${features_csv}")
    endif()
    if(is_release)
        list(APPEND cmd_args -- --release)
    endif()

    add_custom_command(
        OUTPUT "${generated_header}" "${generated_lib}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "PATH=${CRUBIT_INSTALL_DIR}/bin:${CRUBIT_SYSROOT_DIR}/bin:$ENV{PATH}"
            "CARGO=${CRUBIT_CARGO_EXECUTABLE}" "RUSTC=${CRUBIT_RUSTC_EXECUTABLE}"
            "RUSTC_BOOTSTRAP=1" "CRUBIT_SUPPORT_RUST_PATH=${CRUBIT_SUPPORT_CRATE_DIR}"
            "${CRUBIT_CARGO_EXECUTABLE}" cpp_api_from_rust ${cmd_args}
        DEPENDS "${manifest_path}" ${rust_sources}
        COMMENT "Generating C++ API from Rust crate ${crate_name} (${target_name})"
        VERBATIM
    )

    add_custom_target(${target_name}_crubit_gen DEPENDS "${generated_header}" "${generated_lib}")

    add_library(${target_name}_rust_static STATIC IMPORTED GLOBAL)
    set_target_properties(
        ${target_name}_rust_static
        PROPERTIES IMPORTED_LOCATION "${generated_lib}"
    )
    add_dependencies(${target_name}_rust_static ${target_name}_crubit_gen)

    add_library(${target_name} INTERFACE)
    add_dependencies(${target_name} ${target_name}_crubit_gen)
    target_include_directories(${target_name} SYSTEM INTERFACE "${out_dir}/include")
    target_compile_options(${target_name} INTERFACE -stdlib=libc++)
    target_link_options(${target_name} INTERFACE -stdlib=libc++)
    target_link_libraries(${target_name} INTERFACE ${target_name}_rust_static crubit::support_cc)
    if(APPLE)
        target_link_libraries(
            ${target_name}
            INTERFACE "-framework Security" "-framework CoreFoundation"
        )
    elseif(UNIX)
        target_link_libraries(${target_name} INTERFACE dl pthread m)
    endif()

    set_target_properties(
        ${target_name}
        PROPERTIES
            CRUBIT_CPP_API_HEADER "${generated_header}"
            CRUBIT_CPP_API_INCLUDE_DIR "${out_dir}/include"
            CRUBIT_CPP_API_STATIC_LIB "${generated_lib}"
            CRUBIT_CRATE_NAME "${crate_name}"
    )
endfunction()

# Generate Rust bindings and C++ thunk sources for a C++ library target via
# `rs_bindings_from_cc`.
function(crubit_add_rust_api_from_cpp target_name)
    cmake_parse_arguments(
        ARG
        ""
        "CPP_TARGET;CRATE_NAME;OUT_DIR"
        "PUBLIC_HEADERS;INCLUDE_DIRS;SYSTEM_INCLUDE_DIRS;DEFINES;EXTRA_RS_SRCS;EXTRA_CPP_SRCS;CLANG_ARGS;CRUBIT_FEATURES"
        ${ARGN}
    )

    if(NOT ARG_CPP_TARGET)
        message(FATAL_ERROR "crubit_add_rust_api_from_cpp(${target_name}) requires CPP_TARGET")
    endif()
    if(NOT TARGET "${ARG_CPP_TARGET}")
        message(
            FATAL_ERROR
            "crubit_add_rust_api_from_cpp(${target_name}): CPP_TARGET '${ARG_CPP_TARGET}' is not a target"
        )
    endif()
    if(NOT ARG_PUBLIC_HEADERS)
        message(FATAL_ERROR "crubit_add_rust_api_from_cpp(${target_name}) requires PUBLIC_HEADERS")
    endif()

    if(ARG_CRATE_NAME)
        set(crate_name "${ARG_CRATE_NAME}")
    else()
        string(REPLACE "-" "_" crate_name "${target_name}")
    endif()

    if(ARG_OUT_DIR)
        set(out_dir "${ARG_OUT_DIR}")
    else()
        set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/crubit_rust_api/${target_name}")
    endif()

    set(rs_out "${out_dir}/src/lib.rs")
    set(cc_out "${out_dir}/${target_name}_rust_api_impl.cc")
    set(cargo_toml_out "${out_dir}/Cargo.toml")

    _crubit_extract_include_dirs(target_inc_dirs "${ARG_CPP_TARGET}")
    _crubit_extract_compile_defs(target_defs "${ARG_CPP_TARGET}")

    set(all_inc_dirs ${ARG_INCLUDE_DIRS} ${target_inc_dirs})
    list(REMOVE_DUPLICATES all_inc_dirs)
    set(all_defs ${ARG_DEFINES} ${target_defs})
    list(REMOVE_DUPLICATES all_defs)

    set(invoke_args
        --rs-bindings-from-cc
        "${CRUBIT_RS_BINDINGS_FROM_CC_EXECUTABLE}"
        --crubit-src
        "${CRUBIT_SOURCE_DIR}"
        --target
        "//interop:${target_name}"
        --crate-name
        "${crate_name}"
        --rs-out
        "${rs_out}"
        --cc-out
        "${cc_out}"
        --cargo-toml-out
        "${cargo_toml_out}"
        --cxx
        "${CMAKE_CXX_COMPILER}"
        --cxx-standard
        23
    )
    if(CRUBIT_CLANG_FORMAT_EXECUTABLE)
        list(APPEND invoke_args --clang-format "${CRUBIT_CLANG_FORMAT_EXECUTABLE}")
    endif()
    if(EXISTS "${CRUBIT_RUSTFMT_EXECUTABLE}")
        list(APPEND invoke_args --rustfmt "${CRUBIT_RUSTFMT_EXECUTABLE}")
    endif()

    set(header_deps "")
    foreach(hdr IN LISTS ARG_PUBLIC_HEADERS)
        list(APPEND invoke_args --public-header "${hdr}")
        if(IS_ABSOLUTE "${hdr}" AND EXISTS "${hdr}")
            list(APPEND header_deps "${hdr}")
        else()
            foreach(inc IN LISTS all_inc_dirs)
                if(EXISTS "${inc}/${hdr}")
                    list(APPEND header_deps "${inc}/${hdr}")
                    break()
                endif()
            endforeach()
        endif()
    endforeach()

    foreach(inc IN LISTS all_inc_dirs)
        list(APPEND invoke_args --include-dir "${inc}")
    endforeach()
    foreach(sys_inc IN LISTS ARG_SYSTEM_INCLUDE_DIRS)
        list(APPEND invoke_args --system-include-dir "${sys_inc}")
    endforeach()
    foreach(def IN LISTS all_defs)
        list(APPEND invoke_args --define "${def}")
    endforeach()
    foreach(extra_rs IN LISTS ARG_EXTRA_RS_SRCS)
        list(APPEND invoke_args --extra-rs-src "${extra_rs}")
    endforeach()
    foreach(extra_cc IN LISTS ARG_EXTRA_CPP_SRCS)
        list(APPEND invoke_args --extra-cpp-src "${extra_cc}")
    endforeach()
    foreach(clang_arg IN LISTS ARG_CLANG_ARGS)
        list(APPEND invoke_args --extra-clang-arg "${clang_arg}")
    endforeach()
    if(ARG_CRUBIT_FEATURES)
        set(crubit_features ${ARG_CRUBIT_FEATURES})
    else()
        set(crubit_features supported assume_lifetimes)
    endif()
    foreach(crubit_feat IN LISTS crubit_features)
        list(APPEND invoke_args --crubit-feature "${crubit_feat}")
    endforeach()

    add_custom_command(
        OUTPUT "${rs_out}" "${cc_out}" "${cargo_toml_out}"
        COMMAND "${CRUBIT_PYTHON_EXECUTABLE}" "${_CRUBIT_INVOKE_RS_BINDINGS_SCRIPT}" ${invoke_args}
        DEPENDS "${_CRUBIT_INVOKE_RS_BINDINGS_SCRIPT}" ${header_deps} "${ARG_CPP_TARGET}"
        COMMENT "Generating Rust API from C++ target ${ARG_CPP_TARGET} (${target_name})"
        VERBATIM
    )

    add_custom_target(${target_name}_crubit_gen DEPENDS "${rs_out}" "${cc_out}" "${cargo_toml_out}")

    add_library(${target_name}_impl_cc STATIC "${cc_out}")
    set_target_properties(
        ${target_name}_impl_cc
        PROPERTIES
            CXX_CLANG_TIDY ""
            C_CLANG_TIDY ""
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
    )
    target_compile_options(${target_name}_impl_cc PUBLIC -stdlib=libc++ PRIVATE -Wno-everything)
    target_link_options(${target_name}_impl_cc PUBLIC -stdlib=libc++)
    target_include_directories(${target_name}_impl_cc SYSTEM PRIVATE "${CRUBIT_SOURCE_DIR}")
    if(all_inc_dirs)
        target_include_directories(${target_name}_impl_cc PRIVATE ${all_inc_dirs})
    endif()
    target_link_libraries(${target_name}_impl_cc PUBLIC "${ARG_CPP_TARGET}" crubit::support_cc)
    add_dependencies(${target_name}_impl_cc ${target_name}_crubit_gen)

    add_library(${target_name} INTERFACE)
    add_dependencies(${target_name} ${target_name}_crubit_gen)
    set_target_properties(
        ${target_name}
        PROPERTIES
            CRUBIT_API_CRATE_NAME "${crate_name}"
            CRUBIT_API_CRATE_DIR "${out_dir}"
            CRUBIT_API_IMPL_CC_TARGET "${target_name}_impl_cc"
            CRUBIT_API_CPP_TARGET "${ARG_CPP_TARGET}"
    )
endfunction()

# Build a Rust binary or integration test crate patched against one or more
# `crubit_add_rust_api_from_cpp` targets and linked with their C++ static libraries.
function(crubit_add_rust_binary_or_test target_name)
    cmake_parse_arguments(
        ARG
        "TEST;ALL_FEATURES;NO_DEFAULT_FEATURES"
        "MANIFEST_PATH;CRATE_NAME;BINARY_NAME"
        "API_TARGETS;CPP_LIBS;FEATURES;LABELS;ARGS"
        ${ARGN}
    )

    if(NOT ARG_MANIFEST_PATH)
        message(FATAL_ERROR "crubit_add_rust_binary_or_test(${target_name}) requires MANIFEST_PATH")
    endif()

    _crubit_resolve_source_path(manifest_path "${ARG_MANIFEST_PATH}")
    get_filename_component(crate_dir "${manifest_path}" DIRECTORY)

    set(cargo_target_dir "${CMAKE_BINARY_DIR}/crubit_cargo")
    _crubit_cargo_profile(is_release profile_subdir)

    set(patch_args "")
    set(seed_cpp_targets ${ARG_CPP_LIBS})
    set(gen_deps "")

    foreach(api_tgt IN LISTS ARG_API_TARGETS)
        if(NOT TARGET "${api_tgt}")
            message(
                FATAL_ERROR
                "crubit_add_rust_binary_or_test(${target_name}): API_TARGETS entry '${api_tgt}' is not a target"
            )
        endif()
        get_target_property(api_crate "${api_tgt}" CRUBIT_API_CRATE_NAME)
        get_target_property(api_dir "${api_tgt}" CRUBIT_API_CRATE_DIR)
        get_target_property(api_impl_cc "${api_tgt}" CRUBIT_API_IMPL_CC_TARGET)
        get_target_property(api_cpp "${api_tgt}" CRUBIT_API_CPP_TARGET)

        list(APPEND patch_args --config "patch.crates-io.${api_crate}.path=\"${api_dir}\"")
        if(api_impl_cc)
            list(APPEND seed_cpp_targets "${api_impl_cc}")
        endif()
        if(api_cpp)
            list(APPEND seed_cpp_targets "${api_cpp}")
        endif()
        list(APPEND gen_deps "${api_tgt}")
    endforeach()

    _crubit_collect_link_targets(cpp_link_targets ${seed_cpp_targets} crubit::support_cc)

    string(ASCII 31 _unit_sep)
    set(rustflags_list "-C" "linker=${CMAKE_CXX_COMPILER}" "-C" "link-arg=-stdlib=libc++")
    if(UNIX AND NOT APPLE)
        if(CMAKE_LINKER_TYPE STREQUAL "LLD")
            list(APPEND rustflags_list "-C" "link-arg=-fuse-ld=lld")
        endif()
        if(CMAKE_INTERPROCEDURAL_OPTIMIZATION AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            list(APPEND rustflags_list "-C" "link-arg=-flto=thin")
        endif()
    endif()
    if(UNIX AND NOT APPLE AND cpp_link_targets)
        list(APPEND rustflags_list "-C" "link-arg=-Wl,--start-group")
    endif()
    foreach(cpp_tgt IN LISTS cpp_link_targets)
        list(APPEND rustflags_list "-C" "link-arg=$<TARGET_FILE:${cpp_tgt}>")
    endforeach()
    if(UNIX AND NOT APPLE AND cpp_link_targets)
        list(APPEND rustflags_list "-C" "link-arg=-Wl,--end-group")
    endif()
    if(UNIX AND NOT APPLE AND DEFINED ENV{CONDA_PREFIX})
        list(
            APPEND rustflags_list
            "-C"
            "link-arg=-L$ENV{CONDA_PREFIX}/lib"
            "-C"
            "link-arg=-Wl,-rpath,$ENV{CONDA_PREFIX}/lib"
        )
    endif()
    list(APPEND rustflags_list "-C" "link-arg=-lc++")
    if(UNIX AND NOT APPLE)
        if(DEFINED ENV{CONDA_PREFIX})
            list(APPEND rustflags_list "-C" "link-arg=-lc++abi")
        endif()
        list(
            APPEND rustflags_list
            "-C"
            "link-arg=-lm"
            "-C"
            "link-arg=-lpthread"
            "-C"
            "link-arg=-ldl"
        )
    endif()
    list(JOIN rustflags_list "${_unit_sep}" encoded_rustflags)

    set(cargo_common_args --manifest-path "${manifest_path}" --target-dir "${cargo_target_dir}")
    if(is_release)
        list(APPEND cargo_common_args --release)
    endif()
    if(ARG_ALL_FEATURES)
        list(APPEND cargo_common_args --all-features)
    endif()
    if(ARG_NO_DEFAULT_FEATURES)
        list(APPEND cargo_common_args --no-default-features)
    endif()
    if(ARG_FEATURES)
        string(JOIN "," features_csv ${ARG_FEATURES})
        list(APPEND cargo_common_args --features "${features_csv}")
    endif()
    if(ARG_BINARY_NAME)
        list(APPEND cargo_common_args --bin "${ARG_BINARY_NAME}")
    endif()
    list(APPEND cargo_common_args ${patch_args})

    set(cargo_env_args
        "PATH=${CRUBIT_INSTALL_DIR}/bin:${CRUBIT_SYSROOT_DIR}/bin:$ENV{PATH}"
        "CARGO=${CRUBIT_CARGO_EXECUTABLE}"
        "RUSTC=${CRUBIT_RUSTC_EXECUTABLE}"
        "RUSTC_BOOTSTRAP=1"
        "CARGO_ENCODED_RUSTFLAGS=${encoded_rustflags}"
    )

    if(ARG_TEST)
        add_custom_target(
            ${target_name}
            ALL
            COMMAND
                "${CMAKE_COMMAND}" -E env ${cargo_env_args} "${CRUBIT_CARGO_EXECUTABLE}" test
                --no-run ${cargo_common_args}
            DEPENDS ${gen_deps} ${cpp_link_targets}
            COMMENT "Building Rust interop test target ${target_name}"
            VERBATIM
        )

        if(BUILD_TESTING OR CMAKE_TESTING_ENABLED)
            add_test(
                NAME "${target_name}"
                COMMAND
                    "${CMAKE_COMMAND}" -E env ${cargo_env_args} "${CRUBIT_CARGO_EXECUTABLE}" test
                    --offline ${cargo_common_args} -- ${ARG_ARGS}
            )
            set(test_props TIMEOUT 60)
            if(ARG_LABELS)
                list(APPEND test_props LABELS "${ARG_LABELS}")
            endif()
            set_tests_properties("${target_name}" PROPERTIES ${test_props})
        endif()
    else()
        add_custom_target(
            ${target_name}
            ALL
            COMMAND
                "${CMAKE_COMMAND}" -E env ${cargo_env_args} "${CRUBIT_CARGO_EXECUTABLE}" build
                ${cargo_common_args}
            DEPENDS ${gen_deps} ${cpp_link_targets}
            COMMENT "Building Rust interop binary target ${target_name}"
            VERBATIM
        )
    endif()
endfunction()
