# cmake/Architecture.cmake Encapsulates target CPU architecture detection and dynamic SIMD feature
# inspection.

include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

add_library(project_architecture INTERFACE)

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

target_compile_definitions(project_architecture INTERFACE "ARCH_FAMILY_${TARGET_ARCH_FAMILY}=1")

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
    COMPILER_SUPPORTS_NEON)
  cmake_pop_check_state()

  if(COMPILER_SUPPORTS_NEON)
    target_compile_definitions(project_architecture INTERFACE HAVE_NEON=1)
    message(STATUS "Hardware/Compiler SIMD capability: ARM NEON (ASIMD) detected")
  endif()

  # Dynamic SIMD Feature Checks (x86_64)
elseif(TARGET_ARCH_FAMILY STREQUAL "X86_64" AND (CMAKE_CXX_COMPILER_ID MATCHES
                                                 ".*Clang|AppleClang|GNU"))
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
    COMPILER_SUPPORTS_AVX2)
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
    COMPILER_SUPPORTS_AVX512)
  cmake_pop_check_state()

  if(COMPILER_SUPPORTS_AVX512)
    target_compile_definitions(project_architecture INTERFACE HAVE_AVX512=1)
    message(STATUS "Hardware/Compiler SIMD capability: AVX-512 detected")
  endif()
endif()
