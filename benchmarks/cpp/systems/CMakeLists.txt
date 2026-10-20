# benchmarks/cpp/systems/CMakeLists.txt
# Benchmark target registration for the systems tree using the modular Benchmarking.cmake helper.

add_project_benchmark(
    bitmap_flip_benchmark
    SOURCES bitmap_flip_benchmark.cpp
    LIBS systems::bitmap_flip
)
