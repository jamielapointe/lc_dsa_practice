set(_lc_spdx_license "Apache-2.0")
if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
    set(_lc_spdx_license "${PROJECT_SPDX_LICENSE}")
endif()

add_library(list_node INTERFACE)
add_library(leet_code::list_node ALIAS list_node)
target_sources(
    list_node
    INTERFACE
        FILE_SET HEADERS
            BASE_DIRS "${PROJECT_SOURCE_DIR}/src/include"
            FILES "${PROJECT_SOURCE_DIR}/src/include/leet_code/list_node.hpp"
)
target_compile_features(list_node INTERFACE cxx_std_23)
target_link_libraries(
    list_node
    INTERFACE
        common::common
        project_options
        project_warnings
        project_sanitizers
        project_architecture
)
set_target_properties(
    leet_code::list_node
    PROPERTIES EXPORT_NAME leet_code_list_node SPDX_LICENSE "${_lc_spdx_license}"
)
if(INSTALL_LC_DSA)
    install(TARGETS leet_code::list_node EXPORT lc_dsa_practice_targets FILE_SET HEADERS)
endif()

function(leet_code_add_library name)
    cmake_parse_arguments(ARG "" "" "HEADERS;SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "leet_code_add_library called without SOURCES for target ${name}")
    endif()

    if(NOT ARG_HEADERS)
        set(ARG_HEADERS "${PROJECT_SOURCE_DIR}/src/include/leet_code/${name}.hpp")
    endif()

    set(spdx_license "Apache-2.0")
    if(DEFINED PROJECT_SPDX_LICENSE AND NOT "${PROJECT_SPDX_LICENSE}" STREQUAL "")
        set(spdx_license "${PROJECT_SPDX_LICENSE}")
    endif()

    add_library(${name} STATIC)
    add_library(leet_code::${name} ALIAS ${name})

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

    if(CMAKE_CXX_CLANG_TIDY)
        set_target_properties(
            ${name}
            PROPERTIES
                CXX_CLANG_TIDY
                    "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/src/include/(leet_code|common)/.*"
        )
    endif()

    set_target_properties(
        leet_code::${name}
        PROPERTIES EXPORT_NAME "leet_code_${name}" SPDX_LICENSE "${spdx_license}"
    )

    if(INSTALL_LC_DSA)
        install(
            TARGETS leet_code::${name}
            EXPORT lc_dsa_practice_targets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            FILE_SET HEADERS
        )
    endif()
endfunction()

leet_code_add_library(two_sum SOURCES two_sum.cpp)

leet_code_add_library(add_two_numbers SOURCES add_two_numbers.cpp LIBS leet_code::list_node)

leet_code_add_library(shortest_path_in_binary_matrix SOURCES shortest_path_in_binary_matrix.cpp)

leet_code_add_library(number_of_islands SOURCES number_of_islands.cpp)

leet_code_add_library(robot SOURCES robot.cpp)

leet_code_add_library(robot_room_cleaner SOURCES robot_room_cleaner.cpp LIBS leet_code::robot)

leet_code_add_library(lru_cache SOURCES lru_cache.cpp)

leet_code_add_library(moving_average_from_data_stream SOURCES moving_average_from_data_stream.cpp)

leet_code_add_library(design_hit_counter SOURCES design_hit_counter.cpp)

leet_code_add_library(sliding_window_maximum SOURCES sliding_window_maximum.cpp)

leet_code_add_library(design_circular_queue SOURCES design_circular_queue.cpp)

leet_code_add_library(course_schedule_ii SOURCES course_schedule_ii.cpp)

leet_code_add_library(summary_ranges SOURCES summary_ranges.cpp)

leet_code_add_library(valid_parentheses SOURCES valid_parentheses.cpp)

leet_code_add_library(merge_sorted_array SOURCES merge_sorted_array.cpp)

leet_code_add_library(kth_largest_element_in_a_stream SOURCES kth_largest_element_in_a_stream.cpp)
