# examples/c/systems/CMakeLists.txt Standalone multi-process example executables for the systems C
# track.

function(systems_c_add_example target_name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})

    if(NOT ARG_SOURCES)
        message(
            FATAL_ERROR
            "systems_c_add_example called without SOURCES for target ${target_name}"
        )
    endif()

    add_executable(${target_name} ${ARG_SOURCES})
    target_include_directories(
        ${target_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src/include
    )
    target_compile_features(${target_name} PRIVATE c_std_23)
    target_link_libraries(
        ${target_name}
        PRIVATE ${ARG_LIBS} project_options project_warnings project_sanitizers project_architecture
    )

    if(CMAKE_C_CLANG_TIDY)
        set_target_properties(
            ${target_name}
            PROPERTIES
                C_CLANG_TIDY "${CMAKE_C_CLANG_TIDY};--header-filter=.*/src/include/systems/.*"
        )
    endif()
endfunction()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub_producer.c")
    systems_c_add_example(
        shm_pubsub_producer_c
        SOURCES shm_pubsub_producer.c
        LIBS systems_c::shm_pubsub systems_c::process_rendezvous
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shm_pubsub_consumer.c")
    systems_c_add_example(
        shm_pubsub_consumer_c
        SOURCES shm_pubsub_consumer.c
        LIBS systems_c::shm_pubsub systems_c::process_rendezvous
    )
endif()
