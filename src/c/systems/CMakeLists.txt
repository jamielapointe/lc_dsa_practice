# src/c/systems/CMakeLists.txt Low-level / systems / embedded-flavored ISO C23 libraries. Public
# headers live in src/include/systems/ and are included as <systems/...>.
#
# To add a component <name>: create src/c/systems/<name>.c and call systems_c_add_library(<name>
# SOURCES <name>.c [HEADERS ...] [PRIVATE_HEADERS ...] [LIBS ...]). Tests live in test/c/systems/
# and link systems_c::<name>.

find_package(Threads REQUIRED)

set(_systems_c_spdx_license "Apache-2.0")
if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
    set(_systems_c_spdx_license "${PROJECT_SPDX_LICENSE}")
endif()

# Compile-time OS capability flags (header-only, dual C23 / C++23). Link systems_c::platform from
# components and tests that branch on capabilities such as robust or process-shared mutexes.
add_library(systems_c_platform INTERFACE)
add_library(systems_c::platform ALIAS systems_c_platform)
target_sources(
    systems_c_platform
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include"
            FILES "${PROJECT_SOURCE_DIR}/src/include/systems/platform.h"
)
target_compile_features(systems_c_platform INTERFACE c_std_23 cxx_std_23)
target_compile_definitions(
    systems_c_platform
    INTERFACE $<BUILD_INTERFACE:$<$<BOOL:${HAVE_STDBIT_H}>:HAVE_STDBIT_H=1>>
)
if(SYSTEMS_POSIX_SUPPORTED)
    target_compile_definitions(systems_c_platform INTERFACE SYSTEMS_POSIX_SUPPORTED=1)
    if(APPLE)
        target_compile_definitions(
            systems_c_platform
            INTERFACE
                $<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:C>:_POSIX_C_SOURCE=200809L;_DARWIN_C_SOURCE=1>>
        )
    else()
        target_compile_definitions(
            systems_c_platform
            INTERFACE
                $<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:C>:_POSIX_C_SOURCE=200809L;_GNU_SOURCE=1>>
        )
    endif()
endif()

# POSIX shared memory (shm_open/shm_unlink) lives in librt on older glibc, including the
# conda-forge Linux sysroot; macOS and QNX provide it in libc. Link rt only where it exists and
# exports the symbol so consumers of systems_c::platform never need a per-OS branch.
include(CheckLibraryExists)
check_library_exists(rt shm_open "" SYSTEMS_HAVE_LIBRT_SHM_OPEN)
if(SYSTEMS_HAVE_LIBRT_SHM_OPEN)
    target_link_libraries(systems_c_platform INTERFACE $<LINK_ONLY:rt>)
endif()

set_target_properties(
    systems_c::platform
    PROPERTIES EXPORT_NAME systems_c_platform SPDX_LICENSE "${_systems_c_spdx_license}"
)
if(INSTALL_LC_DSA)
    install(TARGETS systems_c::platform EXPORT lc_dsa_practice_targets FILE_SET HEADERS)
endif()

# Registers a static library `<name>` (alias `systems_c::<name>`) with the project-wide options.
function(systems_c_add_library name)
    cmake_parse_arguments(ARG "" "" "HEADERS;PRIVATE_HEADERS;SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "systems_c_add_library called without SOURCES for target ${name}")
    endif()

    if(NOT ARG_HEADERS)
        set(ARG_HEADERS "${PROJECT_SOURCE_DIR}/src/include/systems/${name}.h")
    endif()

    set(spdx_license "Apache-2.0")
    if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
        set(spdx_license "${PROJECT_SPDX_LICENSE}")
    endif()

    set(target_name "systems_c_${name}")
    add_library(${target_name} STATIC)
    add_library(systems_c::${name} ALIAS ${target_name})

    target_sources(
        ${target_name}
        PUBLIC FILE_SET HEADERS BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include" FILES ${ARG_HEADERS}
        PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES}
    )
    if(ARG_PRIVATE_HEADERS)
        target_sources(
            ${target_name}
            PRIVATE
                FILE_SET private_headers
                    TYPE HEADERS
                    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                    FILES ${ARG_PRIVATE_HEADERS}
        )
    endif()
    target_compile_features(${target_name} PUBLIC c_std_23)
    target_compile_options(
        ${target_name}
        PRIVATE $<$<COMPILE_LANGUAGE:C>:-ftrivial-auto-var-init=zero>
    )
    target_link_libraries(
        ${target_name}
        PRIVATE project_options project_warnings project_sanitizers project_architecture
        PUBLIC systems_c::platform ${ARG_LIBS}
    )

    if(CMAKE_C_CLANG_TIDY)
        set_target_properties(
            ${target_name}
            PROPERTIES
                C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--header-filter=.*/src/include/systems/.*"
        )
    endif()

    set_target_properties(
        systems_c::${name}
        PROPERTIES EXPORT_NAME "${target_name}" SPDX_LICENSE "${spdx_license}"
    )

    if(INSTALL_LC_DSA)
        install(
            TARGETS systems_c::${name}
            EXPORT lc_dsa_practice_targets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            FILE_SET HEADERS
        )
    endif()
endfunction()

# Fixed-capacity, heap-free, thread-safe MPMC bounded FIFO in C23.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bounded_fifo.c")
    systems_c_add_library(bounded_fifo SOURCES bounded_fifo.c LIBS Threads::Threads)
endif()

# Wait-free single-publisher / lock-free multi-subscriber shared-memory ring buffer (C23).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub.c")
    set(SHM_PUBSUB_C_SOURCES shm_pubsub.c)
    set(SHM_PUBSUB_C_PRIVATE_HEADERS "")
    if(SYSTEMS_POSIX_SUPPORTED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub_posix.c")
        list(APPEND SHM_PUBSUB_C_SOURCES shm_pubsub_posix.c)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub_posix.h")
            list(APPEND SHM_PUBSUB_C_PRIVATE_HEADERS shm_pubsub_posix.h)
        endif()
    endif()
    systems_c_add_library(
        shm_pubsub
        PRIVATE_HEADERS ${SHM_PUBSUB_C_PRIVATE_HEADERS}
        SOURCES ${SHM_PUBSUB_C_SOURCES}
        LIBS Threads::Threads
    )
endif()

# Crash-safe cross-process initialization handshake (C23).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/process_rendezvous.c")
    set(PROCESS_RENDEZVOUS_C_SOURCES process_rendezvous.c)
    set(PROCESS_RENDEZVOUS_C_PRIVATE_HEADERS "")
    if(SYSTEMS_POSIX_SUPPORTED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/process_rendezvous_posix.c")
        list(APPEND PROCESS_RENDEZVOUS_C_SOURCES process_rendezvous_posix.c)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/process_rendezvous_posix.h")
            list(APPEND PROCESS_RENDEZVOUS_C_PRIVATE_HEADERS process_rendezvous_posix.h)
        endif()
    endif()
    systems_c_add_library(
        process_rendezvous
        PRIVATE_HEADERS ${PROCESS_RENDEZVOUS_C_PRIVATE_HEADERS}
        SOURCES ${PROCESS_RENDEZVOUS_C_SOURCES}
        LIBS Threads::Threads
    )
endif()

# In-place horizontal flipping for 1-bpp monochrome bitmaps (C23).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bitmap_flip.c")
    systems_c_add_library(bitmap_flip SOURCES bitmap_flip.c)
endif()

# Zero-allocation canonical binary prefix tree (Huffman tree) reconstruction and decoding (C23).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/huffman_tree.c")
    systems_c_add_library(huffman_tree SOURCES huffman_tree.c)
endif()

# Zero-allocation MSB-first bit-stream pattern matching (byte-aligned, naive, and KMP) in C23.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bit_pattern.c")
    systems_c_add_library(bit_pattern SOURCES bit_pattern.c)
endif()

# Deterministic priority-queue-based periodic task scheduler in C23 (spec: docs/systems/priority_scheduler.md).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/priority_scheduler.c")
    systems_c_add_library(
        priority_scheduler
        SOURCES priority_scheduler.c
        LIBS Threads::Threads
    )
endif()
