# cmake/toolchains/aarch64-linux-gnu.cmake Cross-compilation toolchain for AArch64 Linux
# targets (e.g., NVIDIA IGX Thor / Orin).

if(POLICY CMP0210)
    cmake_policy(SET CMP0210 NEW)
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

if(DEFINED ENV{SYSROOT_AARCH64})
    set(CMAKE_SYSROOT "$ENV{SYSROOT_AARCH64}")
    set(CMAKE_FIND_ROOT_PATH "$ENV{SYSROOT_AARCH64}")
endif()

if(DEFINED ENV{STAGING_PREFIX_AARCH64})
    set(CMAKE_STAGING_PREFIX "$ENV{STAGING_PREFIX_AARCH64}")
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/aarch64-extra.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/aarch64-extra.cmake")
endif()

set(CMAKE_C_LINK_FLAGS "-fuse-ld=lld")
set(CMAKE_CXX_LINK_FLAGS "-fuse-ld=lld")

find_program(_qemu_aarch64 NAMES qemu-aarch64 qemu-execve-aarch64)
if(_qemu_aarch64)
    if(DEFINED ENV{SYSROOT_AARCH64})
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_qemu_aarch64};-L;$ENV{SYSROOT_AARCH64}")
    else()
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_qemu_aarch64}")
    endif()
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
