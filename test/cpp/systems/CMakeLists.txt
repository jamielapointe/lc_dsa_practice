# test/cpp/systems/CMakeLists.txt Test target registration for the systems tree using the modular
# Testing.cmake helper.

find_package(Threads REQUIRED)

# Reusable GoogleTest support for systems components: fork-based child processes with timeouts,
# shared rendezvous memory, SIGKILL fault injection, unique shared-memory names, and a global
# allocation counter. Headers are included as "systems_test/<name>.hpp".
add_library(
    systems_test_support
    STATIC
    support/allocation_counter.cpp
    support/global_new_delete.cpp
    support/process.cpp
    support/shared_region.cpp
    support/shm_name.cpp
)
add_library(systems::test_support ALIAS systems_test_support)
# libc++ declares the global allocation functions with reserved parameter names, which clang-tidy
# reports against the library header; see the comment in global_new_delete.cpp.
set_source_files_properties(support/global_new_delete.cpp PROPERTIES SKIP_LINTING ON)
target_include_directories(
    systems_test_support
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support ${PROJECT_SOURCE_DIR}/src/include
)
target_compile_features(systems_test_support PUBLIC cxx_std_23)
target_link_libraries(
    systems_test_support
    PRIVATE project_options project_warnings project_sanitizers project_architecture
    PUBLIC systems::platform Threads::Threads
)

add_project_test(
    test_support_test
    SOURCES test_support_test.cpp
    LIBS systems::test_support
    LABELS "systems;multiprocess"
)

add_project_test(bounded_fifo_test SOURCES bounded_fifo_test.cpp LIBS systems::bounded_fifo)

add_project_test(
    shm_pubsub_test
    SOURCES shm_pubsub_test.cpp
    LIBS systems::shm_pubsub systems::test_support
    LABELS "systems;multiprocess;fault"
)
