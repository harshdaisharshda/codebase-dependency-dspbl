// src/bench/bench_main.cpp
// Phase 5.4/6 — standalone benchmark binary (NOT part of the menu app and
// NOT part of unit-tests). Runs the approved matrix (5 shapes × 1k/5k/10k ×
// 7 variants) PLUS the real scanned fixture project, and writes
// benchmarks_{raw,summary}.csv + benchmark_env.txt into the output
// directory.
//
// Usage: bench [outputDir]   (default: results)
//   Phase 6 final frozen run:  ./build/Release/bench results/frozen
//   — a dedicated directory, so earlier runs' files are never overwritten.
//
// Benchmarks are meaningful only with optimizations ON: run the Release
// build. The bench target keeps asserts ACTIVE (-UNDEBUG) so the runner's
// correctness checks execute here; timing values themselves are never
// asserted anywhere.

#include "bench/benchmark_runner.h"

#include <iostream>

int main(int argc, char** argv)
{
    const std::string outputDir = argc > 1 ? argv[1] : "results";

    std::cout
        << "BFS variant benchmark suite (instructor addendum)\n"
        << "shapes: chain, tree, sparse, dense (8 edges/node), hub-heavy\n"
        << "sizes : 1k, 5k, 10k nodes;  7 variants;  warm-up + 10 trials\n"
        << "extra : real scanned fixture project as an integration case\n"
        << "output: " << outputDir
        << "/benchmarks_raw.csv, benchmarks_summary.csv, benchmark_env.txt\n\n";

    const std::size_t timedCalls = bench::runBenchmarkSuite(outputDir);
    std::cout << "\ndone: " << timedCalls << " timed variant executions ("
              << "15 synthetic graphs + fixture) × 7 variants × 10 trials.\n";
    return 0;
}
