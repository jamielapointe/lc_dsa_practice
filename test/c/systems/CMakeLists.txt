# test/c/systems/CMakeLists.txt Test target registration for the C systems track. Contains
# GoogleTest suites (compiled as C++ linking extern "C" C23 headers) and a pure-C23 test target
# (compiled strictly as C with -std=c23).

find_package(Threads REQUIRED)

# Helper function to register pure-C23 CTest executables without GoogleTest/C++ dependencies
function(add_project_c_test test_target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;LABELS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_project_c_test called without SOURCES for target ${test_target}")
    endif()

    add_executable(${test_target} ${ARG_SOURCES})
    target_compile_features(${test_target} PRIVATE c_std_23)
    target_include_directories(
        ${test_target}
        PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
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

    add_test(NAME ${test_target} COMMAND ${test_target})
    if(ARG_LABELS)
        set_tests_properties(${test_target} PROPERTIES TIMEOUT 60 LABELS "${ARG_LABELS}")
    else()
        set_tests_properties(${test_target} PROPERTIES TIMEOUT 60)
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

    add_project_c_test(
        systems_c23_test
        SOURCES systems_c23_test.c
        LIBS ${SYSTEMS_C23_TEST_LIBS}
        LABELS "systems;c23;pure_c"
    )
endif()
