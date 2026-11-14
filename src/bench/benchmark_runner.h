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
//   Output      : <outputDir>/benchmarks_raw.csv (one row per trial) +
//                 <outputDir>/benchmarks_summary.csv (one row per
//                 variant×graph) + <outputDir>/benchmark_env.txt (environment
//                 record). Schema version 1 (columns documented in
//                 ARCHITECTURE §6). Pass results/frozen for the final frozen
//                 run — earlier runs' files are never overwritten.
//   Fixture     : the real scanned sample project runs as its own case
//                 (Phase 6), same endpoint rules, seed column 0.
//
// Honesty rule (AGENTS.md): every published number comes from these real
// runs; the runner writes only what it measured. The bench target keeps
// asserts ACTIVE under Release (-UNDEBUG) so the correctness checks —
// acyclicity, B3-reachability agreement, counter-drift — execute where the
// results are produced; every check sits outside the timed region.

#include "bench/graph_generators.h"
#include "graph/analysis.h"
#include "graph/bfs.h"
#include "io/project_scanner.h"

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

// Fills the deterministic endpoint selection + untimed correctness
// validation shared by synthetic and scanned cases (asserts active even in
// the Release bench binary — see CMakeLists -UNDEBUG note).
//
// `requireAcyclic` is true for the synthetic shapes (acyclic by
// construction) and FALSE for scanned projects: the bundled fixture
// deliberately contains the cycle_a.h ↔ cycle_b.h pair the Phase 2/4 cycle
// tests rely on. BFS variants handle cycles via discovery guards (asserted
// since Phase 5.1), so cyclic real-world graphs are valid benchmark input.
inline void finishCase(CaseSetup& setup, const bool requireAcyclic)
{
    graph::DependencyGraph& g = setup.data.graph;
    const int nodes = g.nodeCount();
    assert(nodes >= 1 && "finishCase: empty benchmark graph");

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
    setup.data.nodes = nodes;

    // Untimed correctness validation: synthetic shapes are acyclic by
    // construction (assert); scanned projects are NOT assumed acyclic. For
    // every case the two pathfinders must agree on reachability for the
    // chosen pair.
    if (requireAcyclic)
    {
        const analysis::CycleReport cycles = analysis::findCycles(g);
        assert(cycles.hasCycles == false &&
               "synthetic benchmark graph must be acyclic");
    }
    setup.pathExists =
        analysis::dependencyPath(g, from, to).found ==
        bfs::bidirectionalPath(g, from, to).found;
    assert(setup.pathExists && "B3 baselines disagree on reachability");
}

// Builds one synthetic case: graph + untimed seed selection + validation.
inline CaseSetup buildCase(const Shape shape, const int nodes,
                           const std::uint64_t seed)
{
    CaseSetup setup;
    setup.data.kind = shapeName(shape);
    setup.data.seed = seed;
    generateGraph(shape, nodes, seed, setup.data.graph);
    finishCase(setup, /*requireAcyclic=*/true);
    return setup;
}

// Builds the scanned-fixture case (Phase 6): the REAL sample project graph,
// same deterministic endpoint rules, `seed` column 0 = "not synthetic".
inline CaseSetup buildFixtureCase(const std::string& fixtureRoot)
{
    CaseSetup setup;
    setup.data.kind = "fixture_sample_project";
    setup.data.seed = 0;
    io::ScanSummary scan;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;
    const bool scanned =
        io::scanProject(fixtureRoot, setup.data.graph, scan, issues, warnings);
    assert(scanned && "buildFixtureCase: fixture project failed to scan");
    (void)scanned; // unused under NDEBUG (project pattern, Phase 4)
    assert(setup.data.graph.isFinalized() &&
           "buildFixtureCase: scanner left the graph unfinalized");
    finishCase(setup, /*requireAcyclic=*/false);
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
    // Real config from CMake — NDEBUG is undefine'd in bench (-UNDEBUG), so
    // it must NOT be used as a build-type proxy here.
#ifdef BENCH_BUILD_TYPE
    return BENCH_BUILD_TYPE;
#elif defined(NDEBUG)
    return "Release";
#else
    return "Debug";
#endif
}

inline const char* optFlags()
{
#ifdef BENCH_OPT_FLAGS
    return BENCH_OPT_FLAGS;
#else
    return "unknown";
#endif
}

inline const char* cpuArch()
{
#if defined(__aarch64__) || defined(__arm64__)
    return "arm64";
#elif defined(__x86_64__) || defined(__amd64__) || defined(__i386__)
    return "x86_64";
#else
    return "unknown";
#endif
}

// The bench target deliberately undefines NDEBUG (CMakeLists -UNDEBUG):
// the runner's correctness checks MUST execute where the results are
// produced. Unit-tests keep the normal config behaviour.
inline const char* assertState()
{
#ifdef NDEBUG
    return "disabled (NDEBUG)";
#else
    return "active (correctness checks execute)";
#endif
}

// Runs the warm-up + 10-trial rotated loop for ONE case and emits its raw +
// summary rows. `seedCell` is the CSV seed column ("0" for the scanned
// fixture = not synthetic). Graph construction and validation happened
// before this call (untimed). Returns the number of timed trials executed.
inline std::size_t measureCase(const GraphCase& data,
                               const std::string& seedCell,
                               const std::string& stamp,
                               const std::string& schema,
                               std::ofstream& raw, std::ofstream& summary,
                               const Variant* baseOrder,
                               const int kVariantCount, const int kTrials)
{
    const std::string label = data.kind + "/" + std::to_string(data.nodes);
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

    std::size_t timedCalls = 0;
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
            // Deterministic variants: counters must not drift. This check
            // sits OUTSIDE the timed region and runs in the bench binary
            // (asserts kept active there — see assertState()).
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
            baseOrder[v] == Variant::B5Bounded ? static_cast<int>(kBenchBound)
                                               : 0;
        for (int trial = 0; trial < kTrials; ++trial)
        {
            const Sample& s = samples[v][trial];
            raw << schema << ',' << stamp << ',' << data.kind << ','
                << data.nodes << ',' << data.graph.edgeCount() << ','
                << seedCell << ',' << variantName(baseOrder[v]) << ','
                << bound << ',' << trial << ',' << std::fixed
                << std::setprecision(3) << times[v][trial] << ','
                << s.verticesVisited << ',' << s.edgesExamined << ','
                << s.maxFrontier << ',' << s.modeSwitches << '\n';
        }
        raw << std::defaultfloat; // not needed after setprecision reset
        raw.unsetf(std::ios::fixed);

        const Stats stats = summarize(times[v]);
        const Sample& s = samples[v].front();
        summary << schema << ',' << stamp << ',' << data.kind << ','
                << data.nodes << ',' << data.graph.edgeCount() << ','
                << seedCell << ',' << variantName(baseOrder[v]) << ','
                << bound << ",8," << std::fixed << std::setprecision(3)
                << stats.mean << ',' << stats.median << ',' << stats.stdev
                << ',' << stats.min << ',' << stats.max << ','
                << s.verticesVisited << ',' << s.edgesExamined << ','
                << s.maxFrontier << ',' << s.modeSwitches << '\n';
        summary.unsetf(std::ios::fixed);
        std::cout << "  " << std::left << std::setw(24)
                  << variantName(baseOrder[v]) << " mean " << std::right
                  << std::setw(10) << std::fixed << std::setprecision(1)
                  << stats.mean << " us  (n=" << s.verticesVisited
                  << ", e=" << s.edgesExamined << ", f=" << s.maxFrontier;
        if (baseOrder[v] == Variant::B6DirectionOptimizing)
            std::cout << ", pull=" << s.modeSwitches;
        std::cout << ")\n";
        std::cout.unsetf(std::ios::fixed);
    }
    return timedCalls;
}

// Locates tests/fixtures/sample_project walking up from the CWD (same
// discipline as the unit tests; the bench binary may run from anywhere).
inline std::string locateFixtureRoot()
{
    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth)
    {
        const std::filesystem::path candidate =
            current / "tests/fixtures/sample_project";
        if (std::filesystem::is_directory(candidate))
            return candidate.string();
        if (!current.has_parent_path())
            break;
        current = current.parent_path();
    }
    return std::string();
}

// Runs the full approved matrix + the scanned-fixture case and writes raw +
// summary CSVs. Returns the number of timed trials executed.
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
            << "arch: " << cpuArch()
            << "\noptimization: " << optFlags()
            << "\nasserts: " << assertState()
            << "\noutput_dir: " << outputDir << "\n"
            << "methodology: warmup 1 + 10 trials, drop min/max, stats over 8; "
               "rotated variant order; steady_clock; single-threaded\n"
            << "cases: 5 shapes x {1000,5000,10000} + scanned fixture "
               "tests/fixtures/sample_project\n";
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
            timedCalls += measureCase(
                setup.data, std::to_string(seed), stamp, schema, raw, summary,
                baseOrder, kVariantCount, kTrials);
        }
    }

    // Phase 6: the REAL scanned project as its own case (ruling §9.26).
    // Missing fixture → warning + skip, never a fatal error (AGENTS §6).
    const std::string fixtureRoot = locateFixtureRoot();
    if (fixtureRoot.empty())
    {
        std::cout
            << "warning: tests/fixtures/sample_project not found from CWD — "
               "fixture case skipped\n";
    }
    else
    {
        const CaseSetup setup = buildFixtureCase(fixtureRoot);
        timedCalls +=
            measureCase(setup.data, "0", stamp, schema, raw, summary,
                        baseOrder, kVariantCount, kTrials);
    }
    return timedCalls;
}

} // namespace bench
