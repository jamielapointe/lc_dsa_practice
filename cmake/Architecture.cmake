# cmake/Architecture.cmake Encapsulates target CPU architecture detection and dynamic SIMD feature
# inspection.

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

add_library(project_architecture INTERFACE)

# Detect Target OS Family
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(TARGET_OS_FAMILY "LINUX")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(TARGET_OS_FAMILY "MACOS")
else()
    set(TARGET_OS_FAMILY "OTHER")
endif()

# Detect Target Architecture Family
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    set(TARGET_ARCH_FAMILY "X86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(TARGET_ARCH_FAMILY "ARM64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
    set(TARGET_ARCH_FAMILY "RISCV64")
else()
    set(TARGET_ARCH_FAMILY "GENERIC")
endif()

# Enforce supported OS + architecture combinations:
#   - Linux x86_64
#   - Linux aarch64
#   - macOS arm64
set(SYSTEMS_POSIX_SUPPORTED FALSE)
if(
    TARGET_OS_FAMILY STREQUAL "LINUX"
    AND (TARGET_ARCH_FAMILY STREQUAL "X86_64" OR TARGET_ARCH_FAMILY STREQUAL "ARM64")
)
    set(SYSTEMS_POSIX_SUPPORTED TRUE)
elseif(TARGET_OS_FAMILY STREQUAL "MACOS" AND TARGET_ARCH_FAMILY STREQUAL "ARM64")
    set(SYSTEMS_POSIX_SUPPORTED TRUE)
else()
    message(
        FATAL_ERROR
        "Unsupported OS/architecture combination: ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR}). "
        "Supported targets are Linux x86_64, Linux aarch64, and macOS arm64."
    )
endif()

target_compile_definitions(
    project_architecture
    INTERFACE
        "OS_FAMILY_${TARGET_OS_FAMILY}=1"
        "ARCH_FAMILY_${TARGET_ARCH_FAMILY}=1"
        $<$<BOOL:${SYSTEMS_POSIX_SUPPORTED}>:SYSTEMS_POSIX_SUPPORTED=1>
)

# Optional native host optimizations
option(ENABLE_NATIVE_OPTIMIZATION "Build with native host optimizations (-march=native)" OFF)
if(ENABLE_NATIVE_OPTIMIZATION AND NOT CMAKE_CROSSCOMPILING)
    target_compile_options(project_architecture INTERFACE "-march=native")
endif()

# Dynamic SIMD Feature Checks (ARM64)
if(TARGET_ARCH_FAMILY STREQUAL "ARM64" AND (CMAKE_CXX_COMPILER_ID MATCHES ".*Clang|AppleClang|GNU"))
    cmake_push_check_state()
    set(CMAKE_REQUIRED_FLAGS "")
    check_cxx_source_compiles(
        "
    #include <arm_neon.h>
    int main() {
      float32x4_t a = vdupq_n_f32(1.0f);
      float32x4_t b = vaddq_f32(a, a);
      return static_cast<int>(vgetq_lane_f32(b, 0));
    }
  "
        COMPILER_SUPPORTS_NEON
    )
    cmake_pop_check_state()

    if(COMPILER_SUPPORTS_NEON)
        target_compile_definitions(project_architecture INTERFACE HAVE_NEON=1)
        message(STATUS "Hardware/Compiler SIMD capability: ARM NEON (ASIMD) detected")
    endif()

    # Dynamic SIMD Feature Checks (x86_64)
elseif(
    TARGET_ARCH_FAMILY STREQUAL "X86_64"
    AND (CMAKE_CXX_COMPILER_ID MATCHES ".*Clang|AppleClang|GNU")
)
    cmake_push_check_state()
    set(CMAKE_REQUIRED_FLAGS "-mavx2 -mfma")
    check_cxx_source_compiles(
        "
    #include <immintrin.h>
    int main() {
      __m256 a = _mm256_set1_ps(1.0f);
      __m256 b = _mm256_fmadd_ps(a, a, a);
      return static_cast<int>(_mm256_cvtss_f32(b));
    }
  "
        COMPILER_SUPPORTS_AVX2
    )
    cmake_pop_check_state()

    if(COMPILER_SUPPORTS_AVX2)
        target_compile_definitions(project_architecture INTERFACE HAVE_AVX2=1 HAVE_FMA=1)
        message(STATUS "Hardware/Compiler SIMD capability: AVX2 + FMA detected")
    endif()

    cmake_push_check_state()
    set(CMAKE_REQUIRED_FLAGS "-mavx512f -mavx512vl -mavx512dq -mavx512bw")
    check_cxx_source_compiles(
        "
    #include <immintrin.h>
    int main() {
      __m512 a = _mm512_set1_ps(1.0f);
      __m512 b = _mm512_fmadd_ps(a, a, a);
      return static_cast<int>(_mm512_cvtss_f32(b));
    }
  "
        COMPILER_SUPPORTS_AVX512
    )
    cmake_pop_check_state()

    if(COMPILER_SUPPORTS_AVX512)
        target_compile_definitions(project_architecture INTERFACE HAVE_AVX512=1)
        message(STATUS "Hardware/Compiler SIMD capability: AVX-512 detected")
    endif()
endif()
