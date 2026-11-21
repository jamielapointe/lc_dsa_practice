# src/cpp/systems/CMakeLists.txt Low-level / systems / embedded-flavored C++ libraries. Public headers
# live in src/include/systems/ and are included as <systems/...>.
#
# To add a component <name>: create src/cpp/systems/<name>.cpp and call systems_add_library(<name>
# SOURCES <name>.cpp [HEADERS ...] [LIBS ...]). Tests live in test/cpp/systems/ and link
# systems::<name>.

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(_systems_spdx_license "Apache-2.0")
if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
    set(_systems_spdx_license "${PROJECT_SPDX_LICENSE}")
endif()

# Registers a static library `<name>` (alias `systems::<name>`) with the project-wide options.
function(systems_add_library name)
    cmake_parse_arguments(ARG "" "" "HEADERS;SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "systems_add_library called without SOURCES for target ${name}")
    endif()

    if(NOT ARG_HEADERS)
        set(ARG_HEADERS "${PROJECT_SOURCE_DIR}/src/include/systems/${name}.hpp")
    endif()

    set(spdx_license "Apache-2.0")
    if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
        set(spdx_license "${PROJECT_SPDX_LICENSE}")
    endif()

    add_library(${name} STATIC)
    add_library(systems::${name} ALIAS ${name})

    target_sources(
        ${name}
        PUBLIC FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include" FILES ${ARG_HEADERS}
        PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES}
    )
    target_compile_features(${name} PUBLIC cxx_std_23)
    target_link_libraries(
        ${name}
        PRIVATE project_options project_warnings project_sanitizers project_architecture
        PUBLIC common::common ${ARG_LIBS}
    )

    # Systems headers are header-heavy templates; make clang-tidy analyze them as well as the .cpp
    # files (the repo-wide .clang-tidy deliberately sets no header filter).
    if(CMAKE_CXX_CLANG_TIDY)
        set_target_properties(
            ${name}
            PROPERTIES
                CXX_CLANG_TIDY
                    "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/src/include/(systems|common)/.*"
        )
    endif()

    set_target_properties(
        systems::${name}
        PROPERTIES EXPORT_NAME "systems_${name}" SPDX_LICENSE "${spdx_license}"
    )

    if(INSTALL_LC_DSA)
        install(
            TARGETS systems::${name}
            EXPORT lc_dsa_practice_targets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            FILE_SET HEADERS
        )
    endif()
endfunction()

# Compile-time OS capability flags (header-only). Link systems::platform from components and
# tests that branch on capabilities such as robust or process-shared mutexes.
add_library(systems_platform INTERFACE)
add_library(systems::platform ALIAS systems_platform)
target_sources(
    systems_platform
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include"
            FILES "${PROJECT_SOURCE_DIR}/src/include/systems/platform.hpp"
)
target_compile_features(systems_platform INTERFACE cxx_std_23)
target_link_libraries(systems_platform INTERFACE common::common)
if(SYSTEMS_POSIX_SUPPORTED)
    target_compile_definitions(systems_platform INTERFACE SYSTEMS_POSIX_SUPPORTED=1)
endif()

# POSIX shared memory (shm_open/shm_unlink) lives in librt on older glibc, including the conda-forge
# Linux sysroot; macOS and QNX provide it in libc. Link rt only where it exists and exports the
# symbol so consumers of systems::platform never need a per-OS branch.
include(CheckLibraryExists)
check_library_exists(rt shm_open "" SYSTEMS_HAVE_LIBRT_SHM_OPEN)
if(SYSTEMS_HAVE_LIBRT_SHM_OPEN)
    target_link_libraries(systems_platform INTERFACE $<LINK_ONLY:rt>)
endif()

set_target_properties(
    systems::platform
    PROPERTIES EXPORT_NAME systems_platform SPDX_LICENSE "${_systems_spdx_license}"
)
if(INSTALL_LC_DSA)
    install(TARGETS systems::platform EXPORT lc_dsa_practice_targets FILE_SET HEADERS)
endif()

# Exportable threading interface target: uses CMake's FindThreads target in-tree and emits
# portable CPS compile/link usage requirements on install (CPS resolves requires in CONFIG mode
# only, where FindThreads.cmake cannot be found).
add_library(systems_threads INTERFACE)
add_library(systems::threads ALIAS systems_threads)
target_link_libraries(systems_threads INTERFACE $<BUILD_INTERFACE:Threads::Threads>)
if(THREADS_HAVE_PTHREAD_ARG)
    target_compile_options(systems_threads INTERFACE $<INSTALL_INTERFACE:-pthread>)
    target_link_options(systems_threads INTERFACE $<INSTALL_INTERFACE:-pthread>)
elseif(CMAKE_THREAD_LIBS_INIT)
    target_link_libraries(
        systems_threads
        INTERFACE "$<INSTALL_INTERFACE:$<LINK_ONLY:${CMAKE_THREAD_LIBS_INIT}>>"
    )
endif()
set_target_properties(
    systems::threads
    PROPERTIES EXPORT_NAME systems_threads SPDX_LICENSE "${_systems_spdx_license}"
)
if(INSTALL_LC_DSA)
    install(TARGETS systems::threads EXPORT lc_dsa_practice_targets)
endif()

# Fixed-capacity, thread-safe MPMC FIFO. The class is a header-only template; the .cpp provides an
# explicit instantiation so the template is always compiled and linted by the build.
systems_add_library(bounded_fifo SOURCES bounded_fifo.cpp LIBS systems::threads)

# Single-publisher / multi-subscriber channel over a shared-memory ring buffer, safe across threads
# and processes (spec: docs/systems/shm_pubsub.md). OS calls live only in shm_pubsub_posix.cpp.
set(SHM_PUBSUB_SOURCES shm_pubsub.cpp)
if(SYSTEMS_POSIX_SUPPORTED)
    list(APPEND SHM_PUBSUB_SOURCES shm_pubsub_posix.cpp)
endif()
systems_add_library(
    shm_pubsub
    SOURCES ${SHM_PUBSUB_SOURCES}
    LIBS systems::platform systems::threads
)

# Cross-process initialization handshake (spec: docs/systems/process_rendezvous.md). OS calls live
# only in process_rendezvous_posix.cpp.
set(PROCESS_RENDEZVOUS_SOURCES process_rendezvous.cpp)
if(SYSTEMS_POSIX_SUPPORTED)
    list(APPEND PROCESS_RENDEZVOUS_SOURCES process_rendezvous_posix.cpp)
endif()
systems_add_library(
    process_rendezvous
    SOURCES ${PROCESS_RENDEZVOUS_SOURCES}
    LIBS systems::platform systems::threads
)

# In-place horizontal flipping for 1-bpp monochrome bitmaps (spec: docs/systems/bitmap_flip.md).
systems_add_library(
    bitmap_flip
    HEADERS
        "${PROJECT_SOURCE_DIR}/src/include/systems/bitmap_flip.hpp"
        "${PROJECT_SOURCE_DIR}/src/include/systems/bitmap_utils.hpp"
    SOURCES bitmap_flip.cpp
)
