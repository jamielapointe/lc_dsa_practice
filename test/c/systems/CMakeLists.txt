# test/c/systems/CMakeLists.txt Test target registration for the C systems track. Contains
# GoogleTest suites (compiled as C++ linking extern "C" C23 headers) and a pure-C23 test target
# (compiled strictly as C with -std=c23).

find_package(Threads REQUIRED)

# Helper function to register pure-C23 CTest executables without GoogleTest/C++ dependencies
function(add_project_c_test test_target)
    cmake_parse_arguments(ARG "NO_ADD_TEST" "" "SOURCES;LIBS;LABELS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_project_c_test called without SOURCES for target ${test_target}")
    endif()

    add_executable(${test_target})
    target_sources(${test_target} PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES})
    target_compile_features(${test_target} PRIVATE c_std_23)
    target_compile_options(
        ${test_target}
        PRIVATE $<$<COMPILE_LANGUAGE:C>:-ftrivial-auto-var-init=zero>
    )
    target_link_libraries(
        ${test_target}
        PRIVATE ${ARG_LIBS} project_options project_warnings project_sanitizers project_architecture
    )

    if(CMAKE_C_CLANG_TIDY)
        set_target_properties(
            ${test_target}
            PROPERTIES
                C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--header-filter=.*/src/include/systems/.*"
        )
    endif()

    if(NOT ARG_NO_ADD_TEST AND (BUILD_TESTING OR CMAKE_TESTING_ENABLED))
        add_test(NAME ${test_target} COMMAND ${test_target})
        if(ARG_LABELS)
            set_tests_properties(${test_target} PROPERTIES TIMEOUT 60 LABELS "${ARG_LABELS}")
        else()
            set_tests_properties(${test_target} PROPERTIES TIMEOUT 60)
        endif()
    endif()
endfunction()

# GoogleTest suites (C++ test runner linking extern "C" C systems headers & systems::test_support)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bounded_fifo_c_test.cpp")
    add_project_test(
        bounded_fifo_c_test
        SOURCES bounded_fifo_c_test.cpp
        LIBS systems_c::bounded_fifo systems::test_support
        LABELS "systems"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub_c_test.cpp")
    add_project_test(
        shm_pubsub_c_test
        SOURCES shm_pubsub_c_test.cpp
        LIBS systems_c::shm_pubsub systems::test_support
        LABELS "systems;multiprocess;fault"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/process_rendezvous_c_test.cpp")
    add_project_test(
        process_rendezvous_c_test
        SOURCES process_rendezvous_c_test.cpp
        LIBS systems_c::process_rendezvous systems::test_support
        LABELS "systems;multiprocess;fault"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bitmap_flip_c_test.cpp")
    add_project_test(
        bitmap_flip_c_test
        SOURCES bitmap_flip_c_test.cpp
        LIBS systems_c::bitmap_flip systems::test_support
        LABELS "systems"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/huffman_tree_c_test.cpp")
    add_project_test(
        huffman_tree_c_test
        SOURCES huffman_tree_c_test.cpp
        LIBS systems_c::huffman_tree systems::test_support
        LABELS "systems"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bit_pattern_c_test.cpp")
    add_project_test(
        bit_pattern_c_test
        SOURCES bit_pattern_c_test.cpp
        LIBS systems_c::bit_pattern systems::test_support
        LABELS "systems"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/priority_scheduler_c_test.cpp")
    add_project_test(
        priority_scheduler_c_test
        SOURCES priority_scheduler_c_test.cpp
        LIBS systems_c::priority_scheduler systems::test_support
        LABELS "systems"
    )
endif()

# Pure-C23 test target: compiled strictly with C compiler under -std=c23, zero C++ linkage
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/systems_c23_test.c")
    set(SYSTEMS_C23_TEST_LIBS systems_c::platform)
    if(TARGET systems_c::bounded_fifo)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::bounded_fifo)
    endif()
    if(TARGET systems_c::shm_pubsub)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::shm_pubsub)
    endif()
    if(TARGET systems_c::process_rendezvous)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::process_rendezvous)
    endif()
    if(TARGET systems_c::bitmap_flip)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::bitmap_flip)
    endif()
    if(TARGET systems_c::huffman_tree)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::huffman_tree)
    endif()
    if(TARGET systems_c::bit_pattern)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::bit_pattern)
    endif()
    if(TARGET systems_c::priority_scheduler)
        list(APPEND SYSTEMS_C23_TEST_LIBS systems_c::priority_scheduler)
    endif()

    add_project_c_test(
        systems_c23_test
        NO_ADD_TEST
        SOURCES systems_c23_test.c
        LIBS ${SYSTEMS_C23_TEST_LIBS}
        LABELS "systems;c23;pure_c"
    )

    if(BUILD_TESTING OR CMAKE_TESTING_ENABLED)
        if(
            (NOT CMAKE_CROSSCOMPILING OR CMAKE_CROSSCOMPILING_EMULATOR)
            AND CMAKE_VERSION VERSION_GREATER_EQUAL "4.5"
        )
            # gersemi: off
            discover_tests(
                COMMAND systems_c23_test
                DISCOVERY_ARGS --list-tests
                DISCOVERY_MATCH "^([A-Za-z0-9_]+)$"
                TEST_NAME "systems_c23.\\1"
                TEST_ARGS --run-test "\\1"
                TEST_PROPERTIES TIMEOUT 60 LABELS "systems;c23;pure_c"
                BUILD_DEPENDS systems_c23_test
            )
            # gersemi: on
        else()
            add_test(NAME systems_c23_test COMMAND systems_c23_test)
            set_tests_properties(systems_c23_test PROPERTIES TIMEOUT 60 LABELS "systems;c23;pure_c")
        endif()
    endif()
endif()
