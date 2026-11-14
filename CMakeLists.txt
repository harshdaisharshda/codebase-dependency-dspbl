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

add_executable(bench
    src/bench/bench_main.cpp
)

add_executable(unit-tests
    tests/test_main.cpp
    tests/ds_tests.cpp
    tests/graph_tests.cpp
    tests/io_tests.cpp
    tests/analysis_tests.cpp
    tests/bfs_tests.cpp
    tests/bench_tests.cpp
)

target_include_directories(dep-analyzer PRIVATE src)
target_include_directories(bench PRIVATE src)
target_include_directories(unit-tests PRIVATE src)

# Phase 6: the bench binary keeps asserts ACTIVE in Release (-UNDEBUG comes
# after the config flags, so it wins over -DNDEBUG). The runner's correctness
# checks (acyclicity of synthetic shapes, B3-reachability agreement, counter
# drift across trials) must execute where the benchmark results are produced;
# all of them sit outside the timed region, so timings stay honest.
# Unit-tests/dep-analyzer keep the normal per-config behaviour.
target_compile_options(bench PRIVATE -UNDEBUG)
# The env record must report the REAL config (NDEBUG is undefine'd above, so
# it can no longer serve as a Release/Debug proxy inside bench sources).
# Passed as raw compile options because the values contain spaces.
target_compile_options(bench PRIVATE
    "-DBENCH_BUILD_TYPE=\"${CMAKE_BUILD_TYPE}\"")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(bench PRIVATE
        "-DBENCH_OPT_FLAGS=\"-O3 (CMake Release default) + -UNDEBUG\"")
else()
    target_compile_options(bench PRIVATE
        "-DBENCH_OPT_FLAGS=\"no -O (CMake Debug default) + ASan/UBSan\"")
endif()

set_project_warnings(dep-analyzer)
set_project_warnings(bench)
set_project_warnings(unit-tests)
enable_debug_sanitizers(dep-analyzer)
enable_debug_sanitizers(bench)
enable_debug_sanitizers(unit-tests)
