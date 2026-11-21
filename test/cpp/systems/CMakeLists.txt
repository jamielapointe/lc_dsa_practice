# test/cpp/systems/CMakeLists.txt Test target registration for the systems tree using the modular
# Testing.cmake helper.

find_package(Threads REQUIRED)

# Reusable GoogleTest support for systems components: fork-based child processes with timeouts,
# shared rendezvous memory, SIGKILL fault injection, unique shared-memory names, and a global
# allocation counter. Headers are included as "systems_test/<name>.hpp".
set(SYSTEMS_TEST_SUPPORT_HEADERS support/systems_test/allocation_counter.hpp)
set(SYSTEMS_TEST_SUPPORT_SOURCES support/allocation_counter.cpp)
if(SYSTEMS_POSIX_SUPPORTED)
    list(
        APPEND SYSTEMS_TEST_SUPPORT_HEADERS
        support/systems_test/process.hpp
        support/systems_test/reap.hpp
        support/systems_test/shared_region.hpp
        support/systems_test/shm_name.hpp
    )
    list(
        APPEND SYSTEMS_TEST_SUPPORT_SOURCES
        support/process.cpp
        support/reap.cpp
        support/shared_region.cpp
        support/shm_name.cpp
    )
endif()
add_library(systems_test_support STATIC)
add_library(systems::test_support ALIAS systems_test_support)
target_sources(
    systems_test_support
    PUBLIC
        FILE_SET HEADERS
            BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/support"
            FILES ${SYSTEMS_TEST_SUPPORT_HEADERS}
    PRIVATE FILE_SET sources TYPE SOURCES FILES ${SYSTEMS_TEST_SUPPORT_SOURCES}
    PRIVATE FILE_SET allocator_override TYPE SOURCES FILES support/global_new_delete.cpp
)
# libc++ declares the global allocation functions with reserved parameter names, which clang-tidy
# reports against the library header; see the comment in global_new_delete.cpp.
set_property(FILE_SET allocator_override TARGET systems_test_support PROPERTY SKIP_LINTING ON)
target_compile_features(systems_test_support PUBLIC cxx_std_23)
target_link_libraries(
    systems_test_support
    PRIVATE project_options project_warnings project_sanitizers project_architecture
    PUBLIC common::common systems::platform Threads::Threads
)

add_project_test(
    test_support_test
    SOURCES test_support_test.cpp
    LIBS systems::test_support
    LABELS "systems;multiprocess"
)

add_project_test(
    bounded_fifo_test
    SOURCES bounded_fifo_test.cpp
    LIBS systems::bounded_fifo
    LABELS "systems"
)

add_project_test(
    shm_pubsub_test
    SOURCES shm_pubsub_test.cpp
    LIBS systems::shm_pubsub systems::test_support
    LABELS "systems;multiprocess;fault"
)

add_project_test(
    process_rendezvous_test
    SOURCES process_rendezvous_test.cpp
    LIBS systems::process_rendezvous systems::test_support
    LABELS "systems;multiprocess;fault"
)

add_project_test(
    bitmap_flip_test
    SOURCES bitmap_flip_test.cpp
    LIBS systems::bitmap_flip systems::test_support
    LABELS "systems"
)
