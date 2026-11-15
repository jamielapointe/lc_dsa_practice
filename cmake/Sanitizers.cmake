# cmake/Sanitizers.cmake Encapsulates dynamic sanitizer verification and configuration.

add_library(project_sanitizers INTERFACE)

option(ENABLE_SANITIZER_ADDRESS "Enable AddressSanitizer (ASan)" OFF)
option(ENABLE_SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)
option(ENABLE_SANITIZER_THREAD "Enable ThreadSanitizer (TSan)" OFF)
option(ENABLE_SANITIZER_MEMORY "Enable MemorySanitizer (MSan, Clang + Linux only)" OFF)
option(ENABLE_SANITIZER_LEAK "Enable LeakSanitizer (LSan)" OFF)

block(SCOPE_FOR VARIABLES POLICIES DIAGNOSTICS)
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)

    if(
        ENABLE_SANITIZER_THREAD
        AND (ENABLE_SANITIZER_ADDRESS OR ENABLE_SANITIZER_MEMORY OR ENABLE_SANITIZER_LEAK)
    )
        message(FATAL_ERROR "TSan cannot be combined with ASan, MSan, or LSan")
    endif()

    if(
        ENABLE_SANITIZER_MEMORY
        AND (ENABLE_SANITIZER_ADDRESS OR ENABLE_SANITIZER_THREAD OR ENABLE_SANITIZER_LEAK)
    )
        message(FATAL_ERROR "MSan cannot be combined with ASan, TSan, or LSan")
    endif()

    if(ENABLE_SANITIZER_LEAK AND ENABLE_SANITIZER_ADDRESS)
        message(
            FATAL_ERROR
            "ASan already includes LSan on Linux/macOS; disable ENABLE_SANITIZER_LEAK"
        )
    endif()

    if(
        ENABLE_SANITIZER_MEMORY
        AND (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    )
        message(FATAL_ERROR "MemorySanitizer requires LLVM Clang on Linux")
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang|GNU)$")
        set(_sanitizer_flags "")

        if(ENABLE_SANITIZER_ADDRESS)
            list(APPEND _sanitizer_flags "-fsanitize=address" "-fno-omit-frame-pointer")
        endif()

        if(ENABLE_SANITIZER_UNDEFINED)
            list(APPEND _sanitizer_flags "-fsanitize=undefined" "-fno-sanitize-recover=all")
        endif()

        if(ENABLE_SANITIZER_THREAD)
            list(APPEND _sanitizer_flags "-fsanitize=thread")
        endif()

        if(ENABLE_SANITIZER_MEMORY)
            list(
                APPEND _sanitizer_flags
                "-fsanitize=memory"
                "-fsanitize-memory-track-origins=2"
                "-fno-omit-frame-pointer"
            )
        endif()

        if(ENABLE_SANITIZER_LEAK)
            list(APPEND _sanitizer_flags "-fsanitize=leak")
        endif()

        if(_sanitizer_flags)
            string(JOIN " " _sanitizer_flags_str ${_sanitizer_flags})
            cmake_push_check_state(RESET)
            set(CMAKE_REQUIRED_FLAGS "${_sanitizer_flags_str}")
            set(CMAKE_REQUIRED_LINK_OPTIONS "${_sanitizer_flags}")
            check_cxx_source_compiles("int main() { return 0; }" _compiler_supports_sanitizers)
            cmake_pop_check_state()

            if(NOT _compiler_supports_sanitizers)
                message(
                    FATAL_ERROR
                    "Requested sanitizers are not supported by the current "
                    "compiler/linker: ${_sanitizer_flags_str}"
                )
            endif()

            target_compile_options(
                project_sanitizers
                INTERFACE "$<BUILD_INTERFACE:${_sanitizer_flags}>"
            )
            target_link_options(
                project_sanitizers
                INTERFACE "$<BUILD_INTERFACE:${_sanitizer_flags}>"
            )
            message(STATUS "Sanitizers enabled: ${_sanitizer_flags_str}")
        endif()
    endif()
endblock()
