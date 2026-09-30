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
