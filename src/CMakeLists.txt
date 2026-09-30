# src/CMakeLists.txt
# Static library target for Two Sum DSA practice library

add_library(two_sum STATIC two_sum.cpp)
add_library(lc_dsa::two_sum ALIAS two_sum)

target_include_directories(
  two_sum
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)

target_compile_features(two_sum PUBLIC cxx_std_23)

target_link_libraries(
  two_sum
  PRIVATE project_options project_warnings project_sanitizers project_architecture)
