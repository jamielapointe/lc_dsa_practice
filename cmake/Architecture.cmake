# cmake/Architecture.cmake Encapsulates target OS/CPU architecture detection, cache-line sizing,
# C23 <stdbit.h> probing, POSIX capability checks, and dynamic SIMD feature inspection.

add_library(project_architecture INTERFACE)

option(ENABLE_NATIVE_OPTIMIZATION "Build with native host optimizations (-march=native)" OFF)

block(SCOPE_FOR VARIABLES POLICIES DIAGNOSTICS PROPAGATE SYSTEMS_POSIX_SUPPORTED HAVE_STDBIT_H)
    include(CheckCXXCompilerFlag)
    include(CheckCXXSourceCompiles)
    include(CheckIncludeFile)
    include(CMakePushCheckState)

    # Detect Target OS Family
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_os_family "LINUX")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_os_family "MACOS")
    else()
        set(_os_family "OTHER")
    endif()

    # Detect Target Architecture Family and cache-line granule
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
        set(_arch_family "X86_64")
        set(_cache_line_bytes 64)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(_arch_family "ARM64")
        set(_cache_line_bytes 128)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^riscv64")
        set(_arch_family "RISCV64")
        set(_cache_line_bytes 64)
    else()
        set(_arch_family "GENERIC")
        set(_cache_line_bytes 64)
    endif()

    # Enforce supported OS + architecture combinations:
    #   - Linux x86_64, Linux aarch64, Linux riscv64
    #   - macOS arm64
    set(SYSTEMS_POSIX_SUPPORTED FALSE)
    if(
        _os_family STREQUAL "LINUX"
        AND (
            _arch_family STREQUAL "X86_64"
            OR _arch_family STREQUAL "ARM64"
            OR _arch_family STREQUAL "RISCV64"
        )
    )
        set(SYSTEMS_POSIX_SUPPORTED TRUE)
    elseif(_os_family STREQUAL "MACOS" AND _arch_family STREQUAL "ARM64")
        set(SYSTEMS_POSIX_SUPPORTED TRUE)
    elseif(NOT (CMAKE_CROSSCOMPILING AND ALLOW_NON_PIXI_TOOLCHAIN))
        message(
            FATAL_ERROR
            "Unsupported OS/architecture combination: ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR}). "
            "Supported targets are Linux x86_64, Linux aarch64, Linux riscv64, and macOS arm64."
        )
    endif()

    # Probe ISO C23 <stdbit.h> availability
    cmake_push_check_state(RESET)
    check_include_file("stdbit.h" HAVE_STDBIT_H)
    cmake_pop_check_state()

    target_compile_definitions(
        project_architecture
        INTERFACE
            "$<BUILD_INTERFACE:OS_FAMILY_${_os_family}=1>"
            "$<BUILD_INTERFACE:ARCH_FAMILY_${_arch_family}=1>"
            "$<BUILD_INTERFACE:TARGET_CACHE_LINE_BYTES=${_cache_line_bytes}>"
            "$<BUILD_INTERFACE:$<$<BOOL:${SYSTEMS_POSIX_SUPPORTED}>:SYSTEMS_POSIX_SUPPORTED=1>>"
            "$<BUILD_INTERFACE:$<$<BOOL:${HAVE_STDBIT_H}>:HAVE_STDBIT_H=1>>"
            "$<BUILD_INTERFACE:$<$<AND:$<BOOL:${SYSTEMS_POSIX_SUPPORTED}>,$<COMPILE_LANGUAGE:C>>:_POSIX_C_SOURCE=200809L>>"
            "$<BUILD_INTERFACE:$<$<AND:$<STREQUAL:${_os_family},LINUX>,$<COMPILE_LANGUAGE:C>>:_GNU_SOURCE=1>>"
            "$<BUILD_INTERFACE:$<$<AND:$<STREQUAL:${_os_family},MACOS>,$<COMPILE_LANGUAGE:C>>:_DARWIN_C_SOURCE=1>>"
    )

    # Dynamic POSIX Synchronization Primitive Checks (macOS vs Linux)
    if(SYSTEMS_POSIX_SUPPORTED)
        cmake_push_check_state(RESET)
        if(_os_family STREQUAL "LINUX")
            set(CMAKE_REQUIRED_FLAGS "-pthread")
            set(CMAKE_REQUIRED_LIBRARIES "pthread;rt")
        endif()
        check_cxx_source_compiles(
            "
            #include <fcntl.h>
            #include <semaphore.h>
            int main() {
                sem_t* sem = sem_open(\"/cmake_probe\", O_CREAT, 0600, 0);
                if (sem != SEM_FAILED) {
                    (void)sem_post(sem);
                    (void)sem_trywait(sem);
                    (void)sem_close(sem);
                    (void)sem_unlink(\"/cmake_probe\");
                }
                return 0;
            }
            "
            _has_posix_sem_open
        )
        cmake_pop_check_state()

        if(_has_posix_sem_open)
            target_compile_definitions(
                project_architecture
                INTERFACE "$<BUILD_INTERFACE:HAVE_POSIX_SEM_OPEN=1>"
            )
            message(STATUS "POSIX capability: named semaphores (sem_open) detected")
        endif()

        cmake_push_check_state(RESET)
        if(_os_family STREQUAL "LINUX")
            set(CMAKE_REQUIRED_FLAGS "-pthread")
            set(CMAKE_REQUIRED_LIBRARIES "pthread;rt")
        endif()
        check_cxx_source_compiles(
            "
            #include <ctime>
            #include <semaphore.h>
            int main() {
                struct timespec ts{0, 0};
                auto fn = &sem_timedwait;
                (void)ts;
                return fn != nullptr ? 0 : 1;
            }
            "
            _has_posix_sem_timedwait
        )
        cmake_pop_check_state()

        if(_has_posix_sem_timedwait)
            target_compile_definitions(
                project_architecture
                INTERFACE "$<BUILD_INTERFACE:HAVE_POSIX_SEM_TIMEDWAIT=1>"
            )
            message(STATUS "POSIX capability: timed semaphore wait (sem_timedwait) detected")
        else()
            message(
                STATUS
                "POSIX capability: sem_timedwait absent (using Darwin/POSIX backoff adapter)"
            )
        endif()
    endif()

    # Optional native host optimizations
    if(ENABLE_NATIVE_OPTIMIZATION AND NOT CMAKE_CROSSCOMPILING)
        check_cxx_compiler_flag("-march=native" _has_march_native)
        if(_has_march_native)
            target_compile_options(
                project_architecture
                INTERFACE "$<BUILD_INTERFACE:-march=native>"
            )
        else()
            message(WARNING "ENABLE_NATIVE_OPTIMIZATION requested but -march=native failed")
        endif()
    endif()

    # Dynamic SIMD Feature Checks
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
        if(_arch_family STREQUAL "X86_64")
            cmake_push_check_state(RESET)
            set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma")
            check_cxx_source_compiles(
                "
                #include <immintrin.h>
                int main() {
                    __m256 a = _mm256_set1_ps(1.0F);
                    __m256 b = _mm256_fmadd_ps(a, a, a);
                    return static_cast<int>(_mm256_cvtss_f32(b));
                }
                "
                _has_avx2_fma
            )
            cmake_pop_check_state()
            if(_has_avx2_fma)
                target_compile_definitions(
                    project_architecture
                    INTERFACE "$<BUILD_INTERFACE:HAVE_AVX2=1;HAVE_FMA=1>"
                )
                message(STATUS "Hardware/Compiler SIMD capability: AVX2 + FMA detected")
            endif()

            cmake_push_check_state(RESET)
            set(CMAKE_REQUIRED_FLAGS "-mavx512f -mavx512vl -mavx512dq -mavx512bw")
            check_cxx_source_compiles(
                "
                #include <immintrin.h>
                int main() {
                    __m512 a = _mm512_set1_ps(1.0F);
                    __m512 b = _mm512_fmadd_ps(a, a, a);
                    return static_cast<int>(_mm512_cvtss_f32(b));
                }
                "
                _has_avx512
            )
            cmake_pop_check_state()
            if(_has_avx512)
                target_compile_definitions(
                    project_architecture
                    INTERFACE "$<BUILD_INTERFACE:HAVE_AVX512=1>"
                )
                message(STATUS "Hardware/Compiler SIMD capability: AVX-512 detected")
            endif()
        elseif(_arch_family STREQUAL "ARM64")
            cmake_push_check_state(RESET)
            check_cxx_source_compiles(
                "
                #include <arm_neon.h>
                int main() {
                    float32x4_t a = vdupq_n_f32(1.0F);
                    float32x4_t b = vaddq_f32(a, a);
                    return static_cast<int>(vgetq_lane_f32(b, 0));
                }
                "
                _has_neon
            )
            cmake_pop_check_state()
            if(_has_neon)
                target_compile_definitions(
                    project_architecture
                    INTERFACE "$<BUILD_INTERFACE:HAVE_NEON=1>"
                )
                message(STATUS "Hardware/Compiler SIMD capability: ARM NEON (ASIMD) detected")
            endif()

            cmake_push_check_state(RESET)
            set(CMAKE_REQUIRED_FLAGS "-march=armv8-a+sve")
            check_cxx_source_compiles(
                "
                #include <arm_sve.h>
                int main() {
                    svfloat32_t a = svdup_f32(1.0F);
                    svfloat32_t b = svadd_f32_z(svptrue_b32(), a, a);
                    return static_cast<int>(svcntb()) + static_cast<int>(svlen_f32(b) > 0);
                }
                "
                _has_sve
            )
            cmake_pop_check_state()
            if(_has_sve)
                target_compile_definitions(
                    project_architecture
                    INTERFACE "$<BUILD_INTERFACE:HAVE_SVE=1>"
                )
                message(STATUS "Hardware/Compiler SIMD capability: ARM SVE detected")
            endif()
        elseif(_arch_family STREQUAL "RISCV64")
            set(_rvv_code
                "
                #include <cstddef>
                #include <riscv_vector.h>
                #if !defined(__riscv_v_intrinsic) || (__riscv_v_intrinsic < 1000000)
                #error \"RVV 1.0 intrinsics required\"
                #endif
                int main() {
                    std::size_t vl = __riscv_vsetvl_e32m1(4);
                    vfloat32m1_t a = __riscv_vfmv_v_f_f32m1(1.0F, vl);
                    vfloat32m1_t b = __riscv_vfadd_vv_f32m1(a, a, vl);
                    return static_cast<int>(__riscv_vfmv_f_s_f32m1_f32(b));
                }
                "
            )
            cmake_push_check_state(RESET)
            set(CMAKE_REQUIRED_FLAGS "-march=rva23u64")
            check_cxx_source_compiles("${_rvv_code}" _has_rvv_rva23)
            cmake_pop_check_state()

            set(_has_rvv_gcv FALSE)
            if(NOT _has_rvv_rva23)
                cmake_push_check_state(RESET)
                set(CMAKE_REQUIRED_FLAGS "-march=rv64gcv")
                check_cxx_source_compiles("${_rvv_code}" _has_rvv_gcv)
                cmake_pop_check_state()
            endif()

            if(_has_rvv_rva23 OR _has_rvv_gcv)
                target_compile_definitions(
                    project_architecture
                    INTERFACE "$<BUILD_INTERFACE:HAVE_RVV_1_0=1>"
                )
                message(STATUS "Hardware/Compiler SIMD capability: RISC-V Vector 1.0 detected")
            endif()
        endif()
    endif()
endblock()
