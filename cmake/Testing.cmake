# cmake/Testing.cmake Configures CTest and provides the standardized add_project_test helper
# function.

enable_testing()
include(GoogleTest)

if(POLICY CMP0224)
    cmake_policy(SET CMP0224 NEW)
endif()

# Helper function to register and configure a Google Test executable.
function(add_project_test test_target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS;LABELS;EXTRA_ARGS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_project_test called without SOURCES for target ${test_target}")
    endif()

    add_executable(${test_target})
    target_sources(${test_target} PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES})

    target_link_libraries(
        ${test_target}
        PRIVATE
            ${ARG_LIBS}
            GTest::gtest_main
            project_options
            project_warnings
            project_sanitizers
            project_architecture
    )

    if(TARGET GTest::gmock)
        target_link_libraries(${test_target} PRIVATE GTest::gmock)
    endif()

    if(NOT CMAKE_CROSSCOMPILING)
        set(_discovery_mode POST_BUILD)
        set(_test_timeout 60)
    elseif(CMAKE_CROSSCOMPILING_EMULATOR)
        set(_discovery_mode PRE_TEST)
        set(_test_timeout 120)
    else()
        message(STATUS "Skipping host test discovery for cross target ${test_target}")
        return()
    endif()

    if(ARG_LABELS)
        gtest_discover_tests(
            ${test_target}
            DISCOVERY_MODE ${_discovery_mode}
            EXTRA_ARGS ${ARG_EXTRA_ARGS}
            PROPERTIES TIMEOUT ${_test_timeout} LABELS "${ARG_LABELS}"
        )
    else()
        gtest_discover_tests(
            ${test_target}
            DISCOVERY_MODE ${_discovery_mode}
            EXTRA_ARGS ${ARG_EXTRA_ARGS}
            PROPERTIES TIMEOUT ${_test_timeout}
        )
    endif()
endfunction()
