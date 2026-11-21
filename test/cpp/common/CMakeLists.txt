# test/cpp/common/CMakeLists.txt Unit test targets for shared header-only C++23 utilities under
# src/include/common/.

add_project_test(math_extra_test SOURCES math_extra_test.cpp LIBS common::common LABELS "common")

if(CMAKE_CXX_CLANG_TIDY)
    set_target_properties(
        math_extra_test
        PROPERTIES CXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY};--header-filter=.*/src/include/common/.*"
    )
endif()
