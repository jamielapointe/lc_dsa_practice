# benchmarks/c/systems/CMakeLists.txt Benchmark target registration for the systems C track using the
# modular Benchmarking.cmake helper.

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bitmap_flip_benchmark.cpp")
    add_project_benchmark(
        bitmap_flip_c_benchmark
        SOURCES bitmap_flip_benchmark.cpp
        LIBS systems_c::bitmap_flip
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/huffman_tree_benchmark.cpp")
    add_project_benchmark(
        huffman_tree_c_benchmark
        SOURCES huffman_tree_benchmark.cpp
        LIBS systems_c::huffman_tree
    )
endif()
