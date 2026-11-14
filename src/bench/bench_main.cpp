// src/bench/bench_main.cpp
// Phase 5.4 — standalone benchmark binary (NOT part of the menu app and NOT
// part of unit-tests). Runs the approved matrix (5 shapes × 1k/5k/10k × 7
// variants) and writes results/benchmarks_{raw,summary}.csv.
//
// Benchmarks are meaningful only with optimizations ON: run the Release
// build (`./build/Release/bench`). Timing values are never asserted; the
// counters embedded in each row are verified across trials inside the runner.

#include "bench/benchmark_runner.h"

#include <iostream>

int main()
{
    std::cout
        << "BFS variant benchmark suite (instructor addendum)\n"
        << "shapes: chain, tree, sparse, dense (8 edges/node), hub-heavy\n"
        << "sizes : 1k, 5k, 10k nodes;  7 variants;  warm-up + 10 trials\n"
        << "output: results/benchmarks_raw.csv, results/benchmarks_summary.csv\n\n";

    const std::size_t timedCalls = bench::runBenchmarkSuite("results");
    std::cout << "\ndone: " << timedCalls << " timed variant executions, "
              << "15 graphs × 7 variants × 10 trials.\n";
    return 0;
}
