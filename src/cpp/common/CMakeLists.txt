# src/cpp/common/CMakeLists.txt Shared header-only C++23 utilities usable across all C++ tracks
# (leet_code, systems, interop, tests, benchmarks, examples). Public headers live in
# src/include/common/ and are included as "common/<name>.hpp".
#
# When adding a new header under src/include/common/, list it in COMMON_HEADERS below so CMake
# verifies standalone compilation (CMAKE_VERIFY_INTERFACE_HEADER_SETS), runs clang-tidy against it,
# and installs it into CPS PACKAGE_INFO / SPDX 3.0.1 SBOM exports.

set(_common_spdx_license "Apache-2.0")
if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
    set(_common_spdx_license "${PROJECT_SPDX_LICENSE}")
endif()

set(COMMON_HEADERS "${PROJECT_SOURCE_DIR}/src/include/common/math_extra.hpp")

if(CMAKE_CXX_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/src/include/common/.*")
endif()

add_library(common INTERFACE)
add_library(common::common ALIAS common)
target_sources(
    common
    INTERFACE FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include" FILES ${COMMON_HEADERS}
)
target_compile_features(common INTERFACE cxx_std_23)
target_link_libraries(common INTERFACE project_options project_architecture)
# Apply strict compiler warnings only to CMake's own header-set verification target so common's
# headers stay warning-clean without leaking -Werror into transitive consumers (e.g. Crubit thunks).
target_compile_options(
    common
    INTERFACE
        "$<BUILD_INTERFACE:$<$<STREQUAL:$<TARGET_PROPERTY:NAME>,common_verify_interface_header_sets>:$<TARGET_PROPERTY:project_warnings,INTERFACE_COMPILE_OPTIONS>>>"
)
set_target_properties(
    common::common
    PROPERTIES EXPORT_NAME common SPDX_LICENSE "${_common_spdx_license}"
)
if(INSTALL_LC_DSA AND (BUILD_LEET_CODE OR BUILD_SYSTEMS))
    install(TARGETS common::common EXPORT lc_dsa_practice_targets FILE_SET HEADERS)
endif()
