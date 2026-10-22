# examples/cpp/systems/CMakeLists.txt Standalone multi-process example executables for the systems
# track.

function(systems_add_example target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "systems_add_example called without SOURCES for target ${target_name}")
    endif()

    add_executable(${target_name} ${ARG_SOURCES})
    target_include_directories(
        ${target_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/include
    )
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_link_libraries(
        ${target_name}
        PRIVATE ${ARG_LIBS} project_options project_warnings project_sanitizers project_architecture
    )
endfunction()

systems_add_example(
    shm_pubsub_producer
    SOURCES shm_pubsub_producer.cpp
    LIBS systems::shm_pubsub systems::process_rendezvous
)

systems_add_example(
    shm_pubsub_consumer
    SOURCES shm_pubsub_consumer.cpp
    LIBS systems::shm_pubsub systems::process_rendezvous
)
