# src/CMakeLists.txt
# Static library target for Two Sum DSA practice library

add_library(two_sum STATIC two_sum.cpp)
add_library(lc_dsa::two_sum ALIAS two_sum)

target_include_directories(two_sum PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

target_compile_features(two_sum PUBLIC cxx_std_23)

target_link_libraries(
  two_sum
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

# Header-only interface target for shared ListNode and ScopedLinkedList
add_library(list_node INTERFACE)
add_library(lc_dsa::list_node ALIAS list_node)

target_include_directories(list_node INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                               $<INSTALL_INTERFACE:include>)

target_compile_features(list_node INTERFACE cxx_std_23)

target_link_libraries(
  list_node
  INTERFACE project_options
            project_warnings
            project_sanitizers
            project_architecture)

# Static library target for Add Two Numbers DSA practice library
add_library(add_two_numbers STATIC add_two_numbers.cpp)
add_library(lc_dsa::add_two_numbers ALIAS add_two_numbers)

target_include_directories(add_two_numbers PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                  $<INSTALL_INTERFACE:include>)

target_compile_features(add_two_numbers PUBLIC cxx_std_23)

target_link_libraries(
  add_two_numbers
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture
  PUBLIC lc_dsa::list_node)

# Static library target for Shortest Path in Binary Matrix DSA practice library
add_library(shortest_path_in_binary_matrix STATIC shortest_path_in_binary_matrix.cpp)
add_library(lc_dsa::shortest_path_in_binary_matrix ALIAS shortest_path_in_binary_matrix)

target_include_directories(shortest_path_in_binary_matrix PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                                 $<INSTALL_INTERFACE:include>)

target_compile_features(shortest_path_in_binary_matrix PUBLIC cxx_std_23)

target_link_libraries(
  shortest_path_in_binary_matrix
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

# Static library target for Number of Islands DSA practice library
add_library(number_of_islands STATIC number_of_islands.cpp)
add_library(lc_dsa::number_of_islands ALIAS number_of_islands)

target_include_directories(number_of_islands PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                    $<INSTALL_INTERFACE:include>)

target_compile_features(number_of_islands PUBLIC cxx_std_23)

target_link_libraries(
  number_of_islands
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

# Static library target for Robot Helper
add_library(robot STATIC robot.cpp)
add_library(lc_dsa::robot ALIAS robot)

target_include_directories(robot PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)

target_compile_features(robot PUBLIC cxx_std_23)

target_link_libraries(
  robot
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

# Static library target for Robot Room Cleaner DSA practice library
add_library(robot_room_cleaner STATIC robot_room_cleaner.cpp)
add_library(lc_dsa::robot_room_cleaner ALIAS robot_room_cleaner)

target_include_directories(robot_room_cleaner PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                     $<INSTALL_INTERFACE:include>)

target_compile_features(robot_room_cleaner PUBLIC cxx_std_23)

target_link_libraries(
  robot_room_cleaner
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture
  PUBLIC lc_dsa::robot)

add_library(lru_cache STATIC lru_cache.cpp)
add_library(lc_dsa::lru_cache ALIAS lru_cache)

target_include_directories(lru_cache PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                            $<INSTALL_INTERFACE:include>)

target_compile_features(lru_cache PUBLIC cxx_std_23)

target_link_libraries(
  lru_cache
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

add_library(moving_average_from_data_stream STATIC moving_average_from_data_stream.cpp)
add_library(lc_dsa::moving_average_from_data_stream ALIAS moving_average_from_data_stream)

target_include_directories(moving_average_from_data_stream PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                                  $<INSTALL_INTERFACE:include>)

target_compile_features(moving_average_from_data_stream PUBLIC cxx_std_23)

target_link_libraries(
  moving_average_from_data_stream
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

add_library(design_hit_counter STATIC design_hit_counter.cpp)
add_library(lc_dsa::design_hit_counter ALIAS design_hit_counter)

target_include_directories(design_hit_counter PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                     $<INSTALL_INTERFACE:include>)

target_compile_features(design_hit_counter PUBLIC cxx_std_23)

target_link_libraries(
  design_hit_counter
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)

add_library(sliding_window_maximum STATIC sliding_window_maximum.cpp)
add_library(lc_dsa::sliding_window_maximum ALIAS sliding_window_maximum)

target_include_directories(sliding_window_maximum PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                                                         $<INSTALL_INTERFACE:include>)

target_compile_features(sliding_window_maximum PUBLIC cxx_std_23)

target_link_libraries(
  sliding_window_maximum
  PRIVATE project_options
          project_warnings
          project_sanitizers
          project_architecture)
