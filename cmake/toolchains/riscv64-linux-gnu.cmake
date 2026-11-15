# cmake/toolchains/riscv64-linux-gnu.cmake Cross-compilation toolchain for RISC-V 64 RVA23 Linux
# targets with QEMU user-mode emulation detection.

if(POLICY CMP0210)
    cmake_policy(SET CMP0210 NEW)
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET riscv64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET riscv64-linux-gnu)

set(CMAKE_C_FLAGS_INIT "-march=rva23u64")
set(CMAKE_CXX_FLAGS_INIT "-march=rva23u64")

if(DEFINED ENV{SYSROOT_RISCV64})
    set(CMAKE_SYSROOT "$ENV{SYSROOT_RISCV64}")
    set(CMAKE_FIND_ROOT_PATH "$ENV{SYSROOT_RISCV64}")
endif()

if(DEFINED ENV{STAGING_PREFIX_RISCV64})
    set(CMAKE_STAGING_PREFIX "$ENV{STAGING_PREFIX_RISCV64}")
endif()

set(CMAKE_C_LINK_FLAGS "-fuse-ld=lld")
set(CMAKE_CXX_LINK_FLAGS "-fuse-ld=lld")

find_program(_qemu_riscv64 NAMES qemu-riscv64 qemu-execve-riscv64)
if(_qemu_riscv64)
    if(DEFINED ENV{SYSROOT_RISCV64})
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_qemu_riscv64};-L;$ENV{SYSROOT_RISCV64}")
    else()
        set(CMAKE_CROSSCOMPILING_EMULATOR "${_qemu_riscv64}")
    endif()
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
