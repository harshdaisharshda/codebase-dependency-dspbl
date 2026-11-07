# CMakeLists.txt — Codebase Dependency Analyzer (Phase 0)
# Targets:
#   dep-analyzer : the menu-driven console application
#   unit-tests   : self-registering unit-test binary (no external framework)
# Conventions (AGENTS.md): C++17, no warnings at -Wall -Wextra in either config,
# Debug builds carry -fsanitize=address,undefined.

cmake_minimum_required(VERSION 3.16)
project(CodebaseDependencyAnalyzer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

function(set_project_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
endfunction()

function(enable_debug_sanitizers target)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE
            -fsanitize=address,undefined)
    endif()
endfunction()

add_executable(dep-analyzer
    src/main.cpp
)

add_executable(unit-tests
    tests/test_main.cpp
    tests/ds_tests.cpp
    tests/graph_tests.cpp
)

target_include_directories(dep-analyzer PRIVATE src)
target_include_directories(unit-tests PRIVATE src)

set_project_warnings(dep-analyzer)
set_project_warnings(unit-tests)
enable_debug_sanitizers(dep-analyzer)
enable_debug_sanitizers(unit-tests)
