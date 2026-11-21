# examples/cpp/systems/CMakeLists.txt Standalone multi-process example executables for the systems
# track.

function(systems_add_example target_name)
    cmake_parse_arguments(ARG "" "" "PRIVATE_HEADERS;SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "systems_add_example called without SOURCES for target ${target_name}")
    endif()

    add_executable(${target_name})
    target_sources(${target_name} PRIVATE FILE_SET sources TYPE SOURCES FILES ${ARG_SOURCES})
    if(ARG_PRIVATE_HEADERS)
        target_sources(
            ${target_name}
            PRIVATE
                FILE_SET private_headers
                    TYPE HEADERS
                    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
                    FILES ${ARG_PRIVATE_HEADERS}
        )
    endif()
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_link_libraries(
        ${target_name}
        PRIVATE
            ${ARG_LIBS}
            common::common
            project_options
            project_warnings
            project_sanitizers
            project_architecture
    )

    if(CMAKE_CXX_CLANG_TIDY)
        set_target_properties(
            ${target_name}
            PROPERTIES
                CXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/examples/cpp/systems/.*"
        )
    endif()
endfunction()

systems_add_example(
    shm_pubsub_producer
    PRIVATE_HEADERS camera_frame.hpp
    SOURCES shm_pubsub_producer.cpp
    LIBS systems::shm_pubsub systems::process_rendezvous
)

systems_add_example(
    shm_pubsub_consumer
    PRIVATE_HEADERS camera_frame.hpp
    SOURCES shm_pubsub_consumer.cpp
    LIBS systems::shm_pubsub systems::process_rendezvous
)
