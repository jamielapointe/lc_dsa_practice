# cmake/Dependencies.cmake Resolves external C++ dependencies. All dependencies are provided by the
# Pixi (conda-forge) environment; FetchContent / ExternalProject are intentionally not used.

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
endif()
