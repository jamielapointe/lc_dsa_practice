# cmake/Benchmarking.cmake Configures Google Benchmark and provides the standardized
# add_project_benchmark helper function.

function(add_project_benchmark benchmark_target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(
            FATAL_ERROR
            "add_project_benchmark called without SOURCES for target ${benchmark_target}"
        )
    endif()

    add_executable(${benchmark_target})
    target_sources(${benchmark_target} PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES})

    target_link_libraries(
        ${benchmark_target}
        PRIVATE
            ${ARG_LIBS}
            common::common
            benchmark::benchmark
            project_options
            project_warnings
            project_sanitizers
            project_architecture
    )
endfunction()
