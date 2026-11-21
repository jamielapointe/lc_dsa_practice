# test/cpp/leet_code/CMakeLists.txt
# Test target registration using modular Testing.cmake helper

add_project_test(two_sum_test SOURCES two_sum_test.cpp LIBS leet_code::two_sum)

add_project_test(list_node_test SOURCES list_node_test.cpp LIBS leet_code::list_node)

add_project_test(
    add_two_numbers_test
    SOURCES add_two_numbers_test.cpp
    LIBS leet_code::add_two_numbers leet_code::list_node
)

add_project_test(
    shortest_path_in_binary_matrix_test
    SOURCES shortest_path_in_binary_matrix_test.cpp
    LIBS leet_code::shortest_path_in_binary_matrix
)

add_project_test(
    number_of_islands_test
    SOURCES number_of_islands_test.cpp
    LIBS leet_code::number_of_islands
    RUN_SERIAL
)

add_project_test(
    robot_room_cleaner_test
    SOURCES robot_room_cleaner_test.cpp
    LIBS leet_code::robot_room_cleaner
)

add_project_test(robot_test SOURCES robot_test.cpp LIBS leet_code::robot)

add_project_test(
    robot_room_cleaner_perf_test
    SOURCES robot_room_cleaner_perf_test.cpp
    LIBS leet_code::robot_room_cleaner
    LABELS "Performance"
    RUN_SERIAL
)

add_project_test(
    lru_cache_test
    SOURCES lru_cache_test.cpp
    LIBS leet_code::lru_cache
    RUN_SERIAL
)

add_project_test(
    moving_average_from_data_stream_test
    SOURCES moving_average_from_data_stream_test.cpp
    LIBS leet_code::moving_average_from_data_stream
)

add_project_test(
    design_hit_counter_test
    SOURCES design_hit_counter_test.cpp
    LIBS leet_code::design_hit_counter
)

add_project_test(
    sliding_window_maximum_test
    SOURCES sliding_window_maximum_test.cpp
    LIBS leet_code::sliding_window_maximum
)

add_project_test(
    design_circular_queue_test
    SOURCES design_circular_queue_test.cpp
    LIBS leet_code::design_circular_queue
)

add_project_test(
    course_schedule_ii_test
    SOURCES course_schedule_ii_test.cpp
    LIBS leet_code::course_schedule_ii
)

add_project_test(
    summary_ranges_test
    SOURCES summary_ranges_test.cpp
    LIBS leet_code::summary_ranges
    RUN_SERIAL
)

add_project_test(
    valid_parentheses_test
    SOURCES valid_parentheses_test.cpp
    LIBS leet_code::valid_parentheses
)

add_project_test(
    merge_sorted_array_test
    SOURCES merge_sorted_array_test.cpp
    LIBS leet_code::merge_sorted_array
)
