# cmake/Dependencies.cmake Resolves external C++ dependencies. All dependencies are provided by the
# Pixi (conda-forge) environment; FetchContent / ExternalProject are intentionally not used.

# Scope CMD_UNINITIALIZED suppression strictly to third-party conda-forge CMake config scripts
# (GTestTargets.cmake, benchmarkTargets.cmake) so first-party targets retain strict -Wuninitialized.
block(SCOPE_FOR DIAGNOSTICS)
    cmake_diagnostic(SET CMD_UNINITIALIZED IGNORE)

    if(BUILD_TESTING AND (BUILD_LEET_CODE OR BUILD_SYSTEMS))
        find_package(GTest CONFIG REQUIRED)
    endif()

    if(BUILD_BENCHMARKS)
        find_package(benchmark CONFIG REQUIRED)
    endif()
endblock()
