# cmake/ProjectOptions.cmake Encapsulates core build options, compiler launchers, output paths,
# linker selection, IPO/LTO, C++ exceptions/RTTI controls, and build-only definitions.

add_library(project_options INTERFACE)

# Hermetic toolchain guard: all builds must use the Pixi-provided compiler (.pixi/envs/...). Set
# -DALLOW_NON_PIXI_TOOLCHAIN=ON to bypass (e.g. air-gapped sysroots or cross-compilation).
option(ALLOW_NON_PIXI_TOOLCHAIN "Allow building with a compiler outside .pixi/envs/" OFF)
if(NOT ALLOW_NON_PIXI_TOOLCHAIN)
    if(NOT CMAKE_CXX_COMPILER MATCHES "\\.pixi/envs/")
        message(
            FATAL_ERROR
            "Hermetic toolchain violation: CMAKE_CXX_COMPILER ('${CMAKE_CXX_COMPILER}') is not located in "
            ".pixi/envs/. Run builds through Pixi (e.g. 'pixi run test-cpp') or pass -DALLOW_NON_PIXI_TOOLCHAIN=ON."
        )
    endif()
    if(CMAKE_C_COMPILER AND NOT CMAKE_C_COMPILER MATCHES "\\.pixi/envs/")
        message(
            FATAL_ERROR
            "Hermetic toolchain violation: CMAKE_C_COMPILER ('${CMAKE_C_COMPILER}') is not located in "
            ".pixi/envs/. Run builds through Pixi (e.g. 'pixi run test-cpp') or pass -DALLOW_NON_PIXI_TOOLCHAIN=ON."
        )
    endif()
endif()

# Compiler launcher acceleration (ccache)
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "ccache compiler launcher found: ${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# Position Independent Code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Standardized output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Validated linker selection (CMAKE_LINKER_TYPE also governs try_compile())
if(NOT DEFINED CMAKE_LINKER_TYPE)
    block(SCOPE_FOR VARIABLES POLICIES DIAGNOSTICS)
        include(CheckLinkerFlag)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            check_linker_flag(CXX "-fuse-ld=lld" _has_lld_linker)
            if(_has_lld_linker)
                set(CMAKE_LINKER_TYPE "LLD" CACHE STRING "CMake linker type")
                message(STATUS "Defaulting linker to LLD")
            else()
                check_linker_flag(CXX "-fuse-ld=mold" _has_mold_linker)
                if(_has_mold_linker)
                    set(CMAKE_LINKER_TYPE "MOLD" CACHE STRING "CMake linker type")
                    message(STATUS "Defaulting linker to MOLD")
                else()
                    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                        set(_wild_flag "--ld-path=wild")
                    else()
                        set(_wild_flag "-fuse-ld=wild")
                    endif()
                    check_linker_flag(CXX "${_wild_flag}" _has_wild_linker)
                    if(_has_wild_linker)
                        set(CMAKE_LINKER_TYPE "WILD" CACHE STRING "CMake linker type")
                        message(STATUS "Defaulting linker to WILD")
                    endif()
                endif()
            endif()
        elseif(APPLE)
            set(CMAKE_LINKER_TYPE "SYSTEM" CACHE STRING "CMake linker type")
        endif()
    endblock()
endif()

# Interprocedural Optimization (IPO / LTO)
option(ENABLE_IPO "Enable Interprocedural Optimization (LTO)" OFF)
if(ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_error)
    if(_ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "Interprocedural Optimization (IPO/LTO) enabled")
    else()
        message(WARNING "IPO requested but not supported: ${_ipo_error}")
    endif()
endif()

# C++ exceptions and RTTI (ON by default because LeetCode and BoundedFifo tests use exceptions)
option(ENABLE_EXCEPTIONS "Enable C++ exceptions" ON)
option(ENABLE_RTTI "Enable C++ RTTI" ON)
if(NOT ENABLE_EXCEPTIONS)
    target_compile_options(
        project_options
        INTERFACE "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>>"
    )
endif()
if(NOT ENABLE_RTTI)
    target_compile_options(
        project_options
        INTERFACE "$<BUILD_INTERFACE:$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>>"
    )
endif()

# Target compile definitions (build-only so CPS consumers do not inherit internal macros)
target_compile_definitions(
    project_options
    INTERFACE
        "$<BUILD_INTERFACE:$<$<CONFIG:Debug>:DEBUG=1>>"
        "$<BUILD_INTERFACE:$<$<CONFIG:Release>:NDEBUG=1>>"
)
