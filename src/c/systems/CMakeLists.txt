# src/c/systems/CMakeLists.txt Low-level / systems / embedded-flavored ISO C23 libraries. Public
# headers live in src/include/systems/ and are included as <systems/...>.
#
# To add a component <name>: create src/c/systems/<name>.c and call systems_c_add_library(<name>
# SOURCES <name>.c [LIBS ...]). Tests live in test/c/systems/ and link systems_c::<name>.

find_package(Threads REQUIRED)

# Registers a static library `<name>` (alias `systems_c::<name>`) with the project-wide options.
function(systems_c_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "systems_c_add_library called without SOURCES for target ${name}")
    endif()

    set(target_name "systems_c_${name}")
    add_library(${target_name} STATIC ${ARG_SOURCES})
    add_library(systems_c::${name} ALIAS ${target_name})

    target_include_directories(
        ${target_name}
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>
    )
    target_compile_features(${target_name} PUBLIC c_std_23)
    target_link_libraries(
        ${target_name}
        PRIVATE project_options project_warnings project_sanitizers project_architecture
        PUBLIC ${ARG_LIBS}
    )

    if(CMAKE_C_CLANG_TIDY)
        set_target_properties(
            ${target_name}
            PROPERTIES
                C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--header-filter=.*/src/include/systems/.*"
        )
    endif()
endfunction()

# Compile-time OS capability flags (header-only). Link systems_c::platform from components and tests
# that branch on capabilities such as robust or process-shared mutexes.
add_library(systems_c_platform INTERFACE)
add_library(systems_c::platform ALIAS systems_c_platform)
target_include_directories(
    systems_c_platform
    INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include> $<INSTALL_INTERFACE:include>
)
target_compile_features(systems_c_platform INTERFACE c_std_23)
if(SYSTEMS_POSIX_SUPPORTED)
    target_compile_definitions(systems_c_platform INTERFACE SYSTEMS_POSIX_SUPPORTED=1)
endif()

# POSIX shared memory (shm_open/shm_unlink) lives in librt on older glibc, including the
# conda-forge Linux sysroot; macOS and QNX provide it in libc. Link rt only where it exists and
# exports the symbol so consumers of systems_c::platform never need a per-OS branch.
include(CheckLibraryExists)
check_library_exists(rt shm_open "" SYSTEMS_HAVE_LIBRT_SHM_OPEN)
if(SYSTEMS_HAVE_LIBRT_SHM_OPEN)
    target_link_libraries(systems_c_platform INTERFACE rt)
endif()

# Fixed-capacity, heap-free, thread-safe MPMC bounded FIFO in C23.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bounded_fifo.c")
    systems_c_add_library(bounded_fifo SOURCES bounded_fifo.c LIBS Threads::Threads)
endif()

# Wait-free single-publisher / lock-free multi-subscriber shared-memory ring buffer (C23).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub.c")
    set(SHM_PUBSUB_C_SOURCES shm_pubsub.c)
    if(SYSTEMS_POSIX_SUPPORTED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub_posix.c")
        list(APPEND SHM_PUBSUB_C_SOURCES shm_pubsub_posix.c)
    endif()
    systems_c_add_library(
        shm_pubsub
        SOURCES ${SHM_PUBSUB_C_SOURCES}
        LIBS systems_c::platform Threads::Threads
    )
endif()

# Crash-safe cross-process initialization handshake (C23).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/process_rendezvous.c")
    set(PROCESS_RENDEZVOUS_C_SOURCES process_rendezvous.c)
    if(SYSTEMS_POSIX_SUPPORTED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/process_rendezvous_posix.c")
        list(APPEND PROCESS_RENDEZVOUS_C_SOURCES process_rendezvous_posix.c)
    endif()
    systems_c_add_library(
        process_rendezvous
        SOURCES ${PROCESS_RENDEZVOUS_C_SOURCES}
        LIBS systems_c::platform Threads::Threads
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
