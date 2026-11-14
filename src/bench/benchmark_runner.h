#pragma once
// src/bench/benchmark_runner.h
//
// Phase 5.4 — the benchmark runner (instructor addendum; ARCHITECTURE §6):
//
//   Matrix      : 5 approved shapes × {1000, 5000, 10000} nodes (ruling
//                 §9.20), one fixed seed per (shape, size) → reproducible.
//   Variants    : B1 explore / B2 multi-source / B4 reverse / B5 bounded
//                 (depth 3, a meaningful include horizon — unlimited B5 ≡ B1
//                 is an equivalence, not a timing pair) / B6
//                 direction-optimizing / B3 bidirectional / B3 classic
//                 baseline (analysis::dependencyPath — Phase 4 as the
//                 shortest-path oracle; it predates counters, so its counter
//                 columns are zeros by definition).
//   Pairings    : B1 vs B6 (full traversal); B3 vs classic BFS pathfind.
//                 Single-seed B2 ≡ B1 and B4 ≡ impactOf are equivalences,
//                 asserted in unit tests — not benchmark pairs.
//   Methodology : 1 warm-up (discarded) + 10 measured trials per
//                 (variant × graph); drop min/max; mean/median/stdev over
//                 the 8 kept; variant order ROTATED per trial to spread
//                 cache effects; steady_clock; single graph object per case;
//                 graph construction and correctness validation are OUTSIDE
//                 the timed region. Timings are never asserted anywhere.
//   Determinism : all six variants are deterministic, so counters must be
//                 IDENTICAL across trials — the runner asserts this (a
//                 mismatch would be a bug, not noise).
//   Output      : results/benchmarks_raw.csv (one row per trial) +
//                 results/benchmarks_summary.csv (one row per variant×graph)
//                 + results/benchmark_env.txt (environment record). Schema
//                 version 1 (columns documented in DEVELOPMENT_PLAN.md).
//
// Honesty rule (AGENTS.md): every published number comes from these real
// runs; the runner writes only what it measured.

#include "bench/graph_generators.h"
#include "graph/analysis.h"
#include "graph/bfs.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace bench
{

enum class Variant
{
    B1Explore,
    B2MultiSource,
    B4Reverse,
    B5Bounded,
    B6DirectionOptimizing,
    B3Bidirectional,
    B3ClassicBaseline
};

inline const char* variantName(const Variant v)
{
    switch (v)
    {
    case Variant::B1Explore:
        return "b1_explore";
    case Variant::B2MultiSource:
        return "b2_multi_source";
    case Variant::B4Reverse:
        return "b4_reverse";
    case Variant::B5Bounded:
        return "b5_bounded_3";
    case Variant::B6DirectionOptimizing:
        return "b6_direction_optimizing";
    case Variant::B3Bidirectional:
        return "b3_bidirectional";
    case Variant::B3ClassicBaseline:
        return "b3_classic_baseline";
    }
    return "unknown";
}

// Depth-3 horizon for B5: "direct deps + two more layers" — the documented
// meaningful bounded depth (unlimited B5 ≡ B1 is an equivalence, not a pair).
inline constexpr std::size_t kBenchBound = 3;

// Per-trial counter snapshot (mode switches only meaningful for B6).
struct Sample
{
    std::size_t verticesVisited = 0;
    std::size_t edgesExamined = 0;
    std::size_t maxFrontier = 0;
    int modeSwitches = 0;
};

// Everything the runner needs about one generated case.
struct GraphCase
{
    std::string kind;
    int nodes = 0;
    std::uint64_t seed = 0;
    graph::DependencyGraph graph;
    int center = 0;    // traversal seed (B1/B2/B4/B5/B6)
    int hub = 0;       // second B2 seed (max in-degree node)
    int pathFrom = 0;  // B3 start (max out-degree node, largest id on ties)
    int pathTo = 0;    // B3 target (deepest node reachable from pathFrom)
};

struct CaseSetup
{
    GraphCase data;
    bool pathExists = false; // validated once, untimed
};

// Builds one case: graph + untimed seed selection + untimed validation.
inline CaseSetup buildCase(const Shape shape, const int nodes,
                           const std::uint64_t seed)
{
    CaseSetup setup;
    setup.data.kind = shapeName(shape);
    setup.data.nodes = nodes;
    setup.data.seed = seed;
    generateGraph(shape, nodes, seed, setup.data.graph);
    graph::DependencyGraph& g = setup.data.graph;

    // Traversal seed: middle node. Second B2 seed: the biggest hub.
    setup.data.center = nodes / 2;
    int hub = 0;
    for (int id = 1; id < nodes; ++id)
        if (g.inDegree(id) > g.inDegree(hub))
            hub = id;
    setup.data.hub = hub;

    // B3 endpoints: start at the largest-id max-out-degree node (ties →
    // largest id, so a chain benchmark walks the WHOLE chain, not one hop);
    // target = the deepest node an untimed B1 reaches from there.
    int from = 0;
    for (int id = 1; id < nodes; ++id)
        if (g.outDegree(id) >= g.outDegree(from))
            from = id;
    setup.data.pathFrom = from;
    const bfs::TraversalReport probe = bfs::explore(g, from);
    int to = from;
    int deepest = 0;
    for (int id = 0; id < nodes; ++id)
        if (probe.level[static_cast<std::size_t>(id)] > deepest)
        {
            deepest = probe.level[static_cast<std::size_t>(id)];
            to = id;
        }
    setup.data.pathTo = to;

    // Untimed correctness validation: all shapes acyclic, and the two
    // pathfinders must agree on reachability for the chosen pair.
    const analysis::CycleReport cycles = analysis::findCycles(g);
    assert(cycles.hasCycles == false && "benchmark graph must be acyclic");
    setup.pathExists =
        analysis::dependencyPath(g, from, to).found ==
        bfs::bidirectionalPath(g, from, to).found;
    assert(setup.pathExists && "B3 baselines disagree on reachability");
    return setup;
}

// One execution of one variant (the timed region is ONLY the bfs call —
// timed by the caller; this function builds the counter snapshot).
inline Sample runVariant(const Variant variant, const GraphCase& data)
{
    Sample sample;
    switch (variant)
    {
    case Variant::B1Explore:
    {
        const bfs::TraversalReport r = bfs::explore(data.graph, data.center);
        sample = {r.counters.verticesVisited, r.counters.edgesExamined,
                  r.counters.maxFrontier, 0};
        break;
    }
    case Variant::B2MultiSource:
    {
        const bfs::TraversalReport r =
            bfs::exploreMultiSource(data.graph, {data.center, data.hub});
        sample = {r.counters.verticesVisited, r.counters.edgesExamined,
                  r.counters.maxFrontier, 0};
        break;
    }
    case Variant::B4Reverse:
    {
        const bfs::TraversalReport r =
            bfs::exploreReverse(data.graph, data.center);
        sample = {r.counters.verticesVisited, r.counters.edgesExamined,
                  r.counters.maxFrontier, 0};
        break;
    }
    case Variant::B5Bounded:
    {
        const bfs::TraversalReport r =
            bfs::exploreBounded(data.graph, data.center, kBenchBound);
        sample = {r.counters.verticesVisited, r.counters.edgesExamined,
                  r.counters.maxFrontier, 0};
        break;
    }
    case Variant::B6DirectionOptimizing:
    {
        const bfs::TraversalReport r =
            bfs::exploreDirectionOptimizing(data.graph, data.center);
        sample = {r.counters.verticesVisited, r.counters.edgesExamined,
                  r.counters.maxFrontier, r.bottomUpLevels};
        break;
    }
    case Variant::B3Bidirectional:
    {
        const bfs::PathReport r =
            bfs::bidirectionalPath(data.graph, data.pathFrom, data.pathTo);
        sample = {r.counters.verticesVisited, r.counters.edgesExamined,
                  r.counters.maxFrontier, 0};
        break;
    }
    case Variant::B3ClassicBaseline:
    {
        // Phase 4 oracle: no counters exist (pre-counters API) — zeros are
        // the documented convention for this row.
        const analysis::PathResult r =
            analysis::dependencyPath(data.graph, data.pathFrom, data.pathTo);
        sample = {r.found ? 1u : 0u, 0, 0, 0};
        break;
    }
    }
    return sample;
}

struct Stats
{
    double mean = 0, median = 0, stdev = 0, min = 0, max = 0;
};

inline Stats summarize(std::vector<double> times) // by value: we sort
{
    // 10 measured trials → drop min and max → statistics over the 8 kept.
    std::sort(times.begin(), times.end());
    const std::vector<double> kept(times.begin() + 1, times.end() - 1);
    Stats s;
    s.min = times.front();
    s.max = times.back();
    double sum = 0;
    for (const double t : kept)
        sum += t;
    s.mean = sum / static_cast<double>(kept.size());
    s.median = (kept[kept.size() / 2 - 1] + kept[kept.size() / 2]) / 2.0;
    double sq = 0;
    for (const double t : kept)
        sq += (t - s.mean) * (t - s.mean);
    s.stdev = std::sqrt(sq / static_cast<double>(kept.size()));
    return s;
}

inline std::string timestampIso()
{
    const std::time_t now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buffer[32];
    const std::tm* tm = std::localtime(&now);
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm);
    return buffer;
}

inline const char* buildType()
{
#ifdef NDEBUG
    return "Release";
#else
    return "Debug";
#endif
}

// Runs the full approved matrix and writes raw + summary CSVs.
// Returns the number of timed trials executed (for the stdout footer).
inline std::size_t runBenchmarkSuite(const std::string& outputDir)
{
    constexpr std::uint64_t kBaseSeed = 20261115;
    const int sizes[] = {1000, 5000, 10000};
    const Shape shapes[] = {
        Shape::Chain, Shape::Tree, Shape::Sparse, Shape::Dense,
        Shape::HubHeavy,
    };
    const Variant baseOrder[] = {
        Variant::B1Explore,     Variant::B2MultiSource,
        Variant::B4Reverse,     Variant::B5Bounded,
        Variant::B6DirectionOptimizing, Variant::B3Bidirectional,
        Variant::B3ClassicBaseline,
    };
    constexpr int kVariantCount = 7;
    constexpr int kTrials = 10;

    std::filesystem::create_directories(outputDir);
    const std::string stamp = timestampIso();
    const std::string schema = "1";

    std::ofstream raw(outputDir + "/benchmarks_raw.csv");
    raw << "schema_version,timestamp_iso,graph_kind,nodes,edges,seed,variant,"
           "bound,trial,time_us,vertices_visited,edges_examined,max_frontier,"
           "mode_switches\n";
    std::ofstream summary(outputDir + "/benchmarks_summary.csv");
    summary << "schema_version,timestamp_iso,graph_kind,nodes,edges,seed,"
               "variant,bound,trials_kept,mean_us,median_us,stdev_us,min_us,"
               "max_us,vertices_visited,edges_examined,max_frontier,"
               "mode_switches\n";

    {
        std::ofstream env(outputDir + "/benchmark_env.txt");
        env << "timestamp: " << stamp << "\nbuild: " << buildType()
            << "\ncompiler: " << __VERSION__ << "\nstandard: C++17\n"
            << "methodology: warmup 1 + 10 trials, drop min/max, stats over 8; "
               "rotated variant order; steady_clock; single-threaded\n";
    }

    std::size_t timedCalls = 0;
    for (const int nodes : sizes)
    {
        for (const Shape shape : shapes)
        {
            const std::uint64_t seed =
                kBaseSeed + static_cast<std::uint64_t>(nodes) * 1000 +
                static_cast<std::uint64_t>(static_cast<int>(shape));
            const CaseSetup setup = buildCase(shape, nodes, seed);
            const GraphCase& data = setup.data;
            const std::string label =
                data.kind + "/" + std::to_string(nodes);
            std::cout << "[" << label << "] V=" << data.graph.nodeCount()
                      << " E=" << data.graph.edgeCount() << " path "
                      << data.pathFrom << "->" << data.pathTo << "\n";

            // Per-variant trial storage: samples[variantIndex][trial].
            std::vector<std::vector<double>> times(kVariantCount);
            std::vector<std::vector<Sample>> samples(kVariantCount);
            for (int v = 0; v < kVariantCount; ++v)
            {
                times[v].reserve(kTrials);
                samples[v].reserve(kTrials);
            }

            // Warm-up round: every variant once, discarded (also acts as a
            // determinism baseline for the counter assertion below).
            std::vector<Sample> baseline(kVariantCount);
            for (int v = 0; v < kVariantCount; ++v)
                baseline[v] = runVariant(baseOrder[v], data);

            for (int trial = 0; trial < kTrials; ++trial)
            {
                // Rotated order per trial: spreads cache/allocator effects.
                for (int slot = 0; slot < kVariantCount; ++slot)
                {
                    const int v = (trial + slot) % kVariantCount;
                    const Variant variant = baseOrder[v];
                    const auto start = std::chrono::steady_clock::now();
                    const Sample s = runVariant(variant, data);
                    const auto end = std::chrono::steady_clock::now();
                    // Deterministic variants: counters must not drift.
                    assert(s.verticesVisited == baseline[v].verticesVisited &&
                           s.edgesExamined == baseline[v].edgesExamined &&
                           s.maxFrontier == baseline[v].maxFrontier &&
                           "counters drifted across trials");
                    times[v].push_back(
                        std::chrono::duration<double, std::micro>(end - start)
                            .count());
                    samples[v].push_back(s);
                    ++timedCalls;
                }
            }

            // Emit rows.
            for (int v = 0; v < kVariantCount; ++v)
            {
                const int bound =
                    baseOrder[v] == Variant::B5Bounded
                        ? static_cast<int>(kBenchBound)
                        : 0;
                for (int trial = 0; trial < kTrials; ++trial)
                {
                    const Sample& s = samples[v][trial];
                    raw << schema << ',' << stamp << ',' << data.kind << ','
                        << nodes << ',' << data.graph.edgeCount() << ','
                        << seed << ',' << variantName(baseOrder[v]) << ','
                        << bound << ',' << trial << ','
                        << std::fixed << std::setprecision(3)
                        << times[v][trial] << ',' << s.verticesVisited << ','
                        << s.edgesExamined << ',' << s.maxFrontier << ','
                        << s.modeSwitches << '\n';
                }
                raw << std::defaultfloat; // not needed after setprecision reset
                raw.unsetf(std::ios::fixed);

                const Stats stats = summarize(times[v]);
                const Sample& s = samples[v].front();
                summary << schema << ',' << stamp << ',' << data.kind << ','
                        << nodes << ',' << data.graph.edgeCount() << ',' << seed
                        << ',' << variantName(baseOrder[v]) << ',' << bound
                        << ",8," << std::fixed << std::setprecision(3)
                        << stats.mean << ',' << stats.median << ','
                        << stats.stdev << ',' << stats.min << ',' << stats.max
                        << ',' << s.verticesVisited << ',' << s.edgesExamined
                        << ',' << s.maxFrontier << ',' << s.modeSwitches
                        << '\n';
                summary.unsetf(std::ios::fixed);
                std::cout << "  " << std::left << std::setw(24)
                          << variantName(baseOrder[v]) << " mean "
                          << std::right << std::setw(10) << std::fixed
                          << std::setprecision(1) << stats.mean << " us  (n="
                          << s.verticesVisited << ", e=" << s.edgesExamined
                          << ", f=" << s.maxFrontier;
                if (baseOrder[v] == Variant::B6DirectionOptimizing)
                    std::cout << ", pull=" << s.modeSwitches;
                std::cout << ")\n";
                std::cout.unsetf(std::ios::fixed);
            }
        }
    }
    return timedCalls;
}

} // namespace bench
