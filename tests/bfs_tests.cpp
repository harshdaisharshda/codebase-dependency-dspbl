// tests/bfs_tests.cpp
// Phase 5.1 tests for the shared BFS core and variants B1/B2/B4/B5, per
// TESTING_STRATEGY.md Layer 2 (hand-built graphs, no filesystem) plus one
// Layer-4 integration test running the Phase 3 scanner into the core.
//
// Phase 4 stays the untouched oracle (ruling §9.21):
//   B4 ≡ analysis::impactOf levels; B1 reachability ≡ analysis::dependencyPath.
//
// Counter semantics under test (exact values on known graphs — counters are
// features, not byproducts): verticesVisited / edgesExamined / maxFrontier,
// plus the B5 boundHit definition "the bound prevented ≥1 edge scan".

#include "graph/analysis.h"
#include "graph/bfs.h"
#include "graph/dependency_graph.h"
#include "io/project_scanner.h"
#include "bench/graph_generators.h"

#include <filesystem>
#include <string>
#include <vector>

#include "test_framework.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// helpers (test infrastructure only)
// ---------------------------------------------------------------------------

// Comma-joined table for human-readable exact-array assertions.
static std::string tableToString(const ds::DynArray<int>& table, const int n)
{
    std::string result;
    for (int i = 0; i < n; ++i)
    {
        if (i > 0)
            result += ',';
        result += std::to_string(table[static_cast<std::size_t>(i)]);
    }
    return result;
}

// Full-equality check for two runs of the same graph: locked equivalence
// contract (levels, parents, all counters, flags).
static bool reportsIdentical(const bfs::TraversalReport& a,
                             const bfs::TraversalReport& b, const int n)
{
    for (int i = 0; i < n; ++i)
    {
        const std::size_t id = static_cast<std::size_t>(i);
        if (a.level[id] != b.level[id] || a.parent[id] != b.parent[id])
            return false;
    }
    return a.counters.verticesVisited == b.counters.verticesVisited &&
           a.counters.edgesExamined == b.counters.edgesExamined &&
           a.counters.maxFrontier == b.counters.maxFrontier &&
           a.boundHit == b.boundHit;
}

// Validates a B3 path: right endpoints, and every consecutive pair is a real
// forward edge (guards against mis-stitched bidirectional reconstruction).
static bool isValidPath(const graph::DependencyGraph& g,
                        const std::vector<int>& path, const int from,
                        const int to)
{
    if (path.empty() || path.front() != from || path.back() != to)
        return false;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
    {
        bool edge = false;
        for (const int v : g.successors(path[i]))
            if (v == path[i + 1])
            {
                edge = true;
                break;
            }
        if (!edge)
            return false;
    }
    return true;
}

// Replicates tests/fixtures/sample_project in memory (scanner-independent):
// 12 nodes, ids follow the scanner's sorted relative-path order (same replica
// as analysis_tests.cpp — independent TU, shared hand-computed answers).
static graph::DependencyGraph makeFixtureGraph()
{
    graph::DependencyGraph g;
    g.addNode("api/api.cpp", "api.cpp");                 // 0
    g.addNode("api/api.h", "api.h");                     // 1
    g.addNode("cycle_a.h", "cycle_a.h");                 // 2
    g.addNode("cycle_b.h", "cycle_b.h");                 // 3
    g.addNode("database/database.cpp", "database.cpp");  // 4
    g.addNode("database/database.h", "database.h");      // 5
    g.addNode("graph.cpp", "graph.cpp");                 // 6
    g.addNode("graph.h", "graph.h");                     // 7
    g.addNode("main.cpp", "main.cpp");                   // 8
    g.addNode("network/api.h", "api.h");                 // 9
    g.addNode("shared/util.h", "util.h");                // 10
    g.addNode("utils/utils.h", "utils.h");               // 11

    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 5));
    CHECK(g.addEdge(2, 3));
    CHECK(g.addEdge(3, 2));
    CHECK(g.addEdge(6, 7));
    CHECK(g.addEdge(6, 10));
    CHECK(g.addEdge(8, 1));
    CHECK(g.addEdge(8, 11));
    CHECK(g.addEdge(11, 10));
    g.finalize();
    return g;
}

// ===========================================================================
// B1 — counters and tables on known graphs
// ===========================================================================

TEST(bfs, chain_counters_and_parent_chain)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 5; ++i)
        g.addNode("f" + std::to_string(i) + ".cpp",
                  "f" + std::to_string(i) + ".cpp");
    for (int i = 0; i + 1 < 5; ++i)
        CHECK(g.addEdge(i, i + 1));
    g.finalize();

    const bfs::TraversalReport r = bfs::explore(g, 0);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{5});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{4});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{1}); // chain: never > 1 pending
    CHECK(r.boundHit == false);
    CHECK_EQ(tableToString(r.level, 5), std::string("0,1,2,3,4"));
    CHECK_EQ(tableToString(r.parent, 5), std::string("0,0,1,2,3"));
}

TEST(bfs, star_hub_widest_frontier)
{
    graph::DependencyGraph g; // hub 0, five leaves depending on it
    for (int i = 0; i < 6; ++i)
        g.addNode("s" + std::to_string(i) + ".h", "s" + std::to_string(i) + ".h");
    for (int leaf = 1; leaf < 6; ++leaf)
        CHECK(g.addEdge(0, leaf));
    g.finalize();

    const bfs::TraversalReport r = bfs::explore(g, 0);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{6});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{5});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{5}); // all leaves pending at once
    CHECK_EQ(tableToString(r.level, 6), std::string("0,1,1,1,1,1"));
    CHECK_EQ(tableToString(r.parent, 6), std::string("0,0,0,0,0,0"));
}

TEST(bfs, diamond_levels_and_frontier)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 4; ++i)
        g.addNode("d" + std::to_string(i) + ".h", "d" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(0, 2));
    CHECK(g.addEdge(1, 3));
    CHECK(g.addEdge(2, 3));
    g.finalize();

    const bfs::TraversalReport r = bfs::explore(g, 0);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{4});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{4});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{2});
    CHECK_EQ(tableToString(r.level, 4), std::string("0,1,1,2"));
    CHECK_EQ(tableToString(r.parent, 4), std::string("0,0,0,1"));
}

TEST(bfs, self_loop_seed_visited_exactly_once)
{
    graph::DependencyGraph g;
    g.addNode("x.h", "x.h"); // 0: includes itself
    g.addNode("y.h", "y.h"); // 1: includes x
    CHECK(g.addEdge(0, 0));
    CHECK(g.addEdge(1, 0));
    g.finalize();

    const bfs::TraversalReport forward = bfs::explore(g, 0);
    CHECK_EQ(forward.counters.verticesVisited, std::size_t{1});
    CHECK_EQ(forward.counters.edgesExamined, std::size_t{1}); // self-edge scanned
    CHECK_EQ(forward.level[static_cast<std::size_t>(0)], 0);  // no level bump
    CHECK(forward.parent[static_cast<std::size_t>(0)] == 0);

    // B4 sees the self-edge AND y.h (y depends on x → x impacts y).
    const bfs::TraversalReport reverse = bfs::exploreReverse(g, 0);
    CHECK_EQ(reverse.counters.verticesVisited, std::size_t{2});
    CHECK_EQ(reverse.counters.edgesExamined, std::size_t{2});
    CHECK_EQ(tableToString(reverse.level, 2), std::string("0,1"));
    CHECK_EQ(tableToString(reverse.parent, 2), std::string("0,0"));
}

TEST(bfs, disconnected_nodes_stay_unreached)
{
    graph::DependencyGraph g;
    g.addNode("a.cpp", "a.cpp"); // 0
    g.addNode("b.cpp", "b.cpp"); // 1
    g.addNode("lone.cpp", "lone.cpp"); // 2 (no edges)
    CHECK(g.addEdge(0, 1));
    g.finalize();

    const bfs::TraversalReport r = bfs::explore(g, 0);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{2});
    CHECK(r.level[static_cast<std::size_t>(2)] == -1);
    CHECK(r.parent[static_cast<std::size_t>(2)] == -1);
}

TEST(bfs, deterministic_across_runs)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const bfs::TraversalReport first = bfs::explore(g, 8);
    const bfs::TraversalReport second = bfs::explore(g, 8);
    CHECK(reportsIdentical(first, second, 12));
}

// ===========================================================================
// B2 — multi-source semantics
// ===========================================================================

TEST(multi_source, tie_between_seeds_is_deterministic)
{
    graph::DependencyGraph g; // 0→2 and 1→2: both seeds reach 2 in one hop
    for (int i = 0; i < 3; ++i)
        g.addNode("m" + std::to_string(i) + ".h", "m" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 2));
    CHECK(g.addEdge(1, 2));
    g.finalize();

    const bfs::TraversalReport r = bfs::exploreMultiSource(g, {0, 1});
    CHECK_EQ(tableToString(r.level, 3), std::string("0,0,1"));
    CHECK_EQ(r.parent[static_cast<std::size_t>(2)], 0); // first seed's wave wins
    CHECK_EQ(r.counters.verticesVisited, std::size_t{3});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{2});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{2});
}

TEST(multi_source, nearest_seed_wins_on_uneven_paths)
{
    graph::DependencyGraph g; // 0→1→2→4 (3 hops) vs 3→4 (1 hop)
    for (int i = 0; i < 5; ++i)
        g.addNode("n" + std::to_string(i) + ".h", "n" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 4));
    CHECK(g.addEdge(3, 4));
    g.finalize();

    const bfs::TraversalReport r = bfs::exploreMultiSource(g, {0, 3});
    CHECK_EQ(tableToString(r.level, 5), std::string("0,1,2,0,1"));
    CHECK_EQ(r.parent[static_cast<std::size_t>(4)], 3); // via the near seed
}

TEST(multi_source, equals_union_of_sequential_b1_runs)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const std::vector<int> seeds = {8, 0, 6};
    const bfs::TraversalReport multi = bfs::exploreMultiSource(g, seeds);

    for (int v = 0; v < 12; ++v)
    {
        int nearest = -1;
        for (const int seed : seeds)
        {
            const bfs::TraversalReport single = bfs::explore(g, seed);
            const int level = single.level[static_cast<std::size_t>(v)];
            if (level != -1 && (nearest == -1 || level < nearest))
                nearest = level;
        }
        CHECK_EQ(multi.level[static_cast<std::size_t>(v)], nearest);
    }
}

TEST(multi_source, one_seed_and_duplicates_equal_b1)
{
    const graph::DependencyGraph g = makeFixtureGraph();

    const bfs::TraversalReport plain = bfs::explore(g, 8);
    const bfs::TraversalReport oneSeed = bfs::exploreMultiSource(g, {8});
    CHECK(reportsIdentical(oneSeed, plain, 12)); // locked equivalence §9.23

    const bfs::TraversalReport dupSeeds = bfs::exploreMultiSource(g, {8, 8, 8});
    CHECK(reportsIdentical(dupSeeds, plain, 12)); // duplicates are skipped
}

TEST(multi_source, seed_reached_by_earlier_seed_keeps_its_level)
{
    graph::DependencyGraph g; // chain 0→1: seed 1 is also reachable from seed 0
    g.addNode("a.h", "a.h");
    g.addNode("b.h", "b.h");
    CHECK(g.addEdge(0, 1));
    g.finalize();

    const bfs::TraversalReport r = bfs::exploreMultiSource(g, {0, 1});
    CHECK_EQ(r.level[static_cast<std::size_t>(1)], 0); // seed status wins
    CHECK_EQ(r.parent[static_cast<std::size_t>(1)], 1);
}

// ===========================================================================
// B4 — reverse BFS (Phase 4 oracle checks)
// ===========================================================================

TEST(reverse, levels_match_impact_of_on_fixture_for_every_seed)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    for (int seed = 0; seed < 12; ++seed)
    {
        const bfs::TraversalReport r = bfs::exploreReverse(g, seed);
        const analysis::ImpactReport impact = analysis::impactOf(g, seed);
        for (int v = 0; v < 12; ++v)
            CHECK_EQ(r.level[static_cast<std::size_t>(v)],
                     impact.level[static_cast<std::size_t>(v)]);
        // affectedCount counts level >= 0 — same definition as verticesVisited.
        CHECK_EQ(r.counters.verticesVisited, impact.affectedCount);
    }
}

TEST(reverse, equals_b1_on_manually_reversed_graph)
{
    const graph::DependencyGraph g = makeFixtureGraph();

    graph::DependencyGraph reversed; // same node order, edges flipped
    for (int id = 0; id < 12; ++id)
        reversed.addNode(g.node(id).path, g.node(id).name);
    for (int id = 0; id < 12; ++id)
        for (const int v : g.successors(id))
            CHECK(reversed.addEdge(v, id)); // insertion order preserved per node
    reversed.finalize();

    const bfs::TraversalReport asReverse = bfs::exploreReverse(g, 10);
    const bfs::TraversalReport asForward = bfs::explore(reversed, 10);
    CHECK(reportsIdentical(asReverse, asForward, 12));
}

TEST(reverse, direction_check_dependents_not_dependencies)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const bfs::TraversalReport r = bfs::exploreReverse(g, 1);

    CHECK_EQ(r.level[static_cast<std::size_t>(0)], 1); // api.cpp includes api.h
    CHECK_EQ(r.level[static_cast<std::size_t>(8)], 1); // main.cpp includes api.h
    CHECK_EQ(r.level[static_cast<std::size_t>(5)], -1); // dependency direction:
                                                        // NOT impacted
    CHECK_EQ(r.counters.verticesVisited, std::size_t{3});
}

TEST(reverse, fixture_hub_counters_exact)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const bfs::TraversalReport r = bfs::exploreReverse(g, 10);
    // preds(10) = {6, 11} → 2 edges; preds(6) = {} ; preds(11) = {8} → 1;
    // preds(8) = {}.
    CHECK_EQ(r.counters.verticesVisited, std::size_t{4});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{3});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{2});
    CHECK_EQ(r.level[static_cast<std::size_t>(10)], 0); // the hub itself
    CHECK_EQ(r.level[static_cast<std::size_t>(6)], 1);
    CHECK_EQ(r.level[static_cast<std::size_t>(11)], 1);
    CHECK_EQ(r.level[static_cast<std::size_t>(8)], 2);
    CHECK_EQ(r.level[static_cast<std::size_t>(1)], -1); // other component
}

// ===========================================================================
// B5 — bounded-depth semantics
// ===========================================================================

TEST(bounded, huge_and_unbounded_budgets_equal_plain_explore)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const bfs::TraversalReport plain = bfs::explore(g, 8);
    const bfs::TraversalReport unbounded =
        bfs::exploreBounded(g, 8, bfs::kUnboundedDepth);
    CHECK(reportsIdentical(unbounded, plain, 12));

    // Any budget ≥ INT_MAX binds never (levels stay ≤ node count).
    const bfs::TraversalReport huge =
        bfs::exploreBounded(g, 8, 1000000);
    CHECK(reportsIdentical(huge, plain, 12));
    CHECK(huge.boundHit == false);
}

TEST(bounded, cutoff_levels_counters_and_bound_hit)
{
    graph::DependencyGraph g; // chain 0→1→2→3
    for (int i = 0; i < 4; ++i)
        g.addNode("c" + std::to_string(i) + ".cpp",
                  "c" + std::to_string(i) + ".cpp");
    for (int i = 0; i + 1 < 4; ++i)
        CHECK(g.addEdge(i, i + 1));
    g.finalize();

    { // depth 1: node 1 sits at the horizon with a pending edge → boundHit
        const bfs::TraversalReport r = bfs::exploreBounded(g, 0, 1);
        CHECK_EQ(tableToString(r.level, 4), std::string("0,1,-1,-1"));
        CHECK_EQ(r.counters.verticesVisited, std::size_t{2});
        CHECK_EQ(r.counters.edgesExamined, std::size_t{1});
        CHECK(r.boundHit == true);
    }
    { // depth 3: everything discovered; node 3 at the horizon is a LEAF —
      // the bound changed nothing → boundHit stays false
        const bfs::TraversalReport r = bfs::exploreBounded(g, 0, 3);
        CHECK_EQ(tableToString(r.level, 4), std::string("0,1,2,3"));
        CHECK_EQ(r.counters.verticesVisited, std::size_t{4});
        CHECK_EQ(r.counters.edgesExamined, std::size_t{3});
        CHECK(r.boundHit == false);
    }
    { // depth 4 > diameter: identical to the unbounded run
        const bfs::TraversalReport r = bfs::exploreBounded(g, 0, 4);
        const bfs::TraversalReport plain = bfs::explore(g, 0);
        CHECK(reportsIdentical(r, plain, 4));
    }
}

TEST(bounded, bound_hit_false_when_horizon_holds_only_leaves)
{
    graph::DependencyGraph g; // star: leaves at level 1 have no successors
    for (int i = 0; i < 4; ++i)
        g.addNode("t" + std::to_string(i) + ".h", "t" + std::to_string(i) + ".h");
    for (int leaf = 1; leaf < 4; ++leaf)
        CHECK(g.addEdge(0, leaf));
    g.finalize();

    const bfs::TraversalReport r = bfs::exploreBounded(g, 0, 1);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{4});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{3});
    CHECK(r.boundHit == false); // nothing was cut off vs. the unbounded run
}

TEST(bounded, fixture_direct_deps_only)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const bfs::TraversalReport r = bfs::exploreBounded(g, 8, 1);
    CHECK_EQ(r.level[static_cast<std::size_t>(1)], 1); // api/api.h
    CHECK_EQ(r.level[static_cast<std::size_t>(11)], 1); // utils/utils.h
    CHECK_EQ(r.level[static_cast<std::size_t>(5)], -1); // 2 hops — beyond bound
    CHECK_EQ(r.counters.verticesVisited, std::size_t{3});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{2});
    CHECK(r.boundHit == true); // both horizon nodes still have dependencies
}

// ===========================================================================
// Oracle cross-check — B1 reachability ≡ analysis::dependencyPath (§9.21)
// ===========================================================================

TEST(oracle, b1_reachability_matches_dependency_path_all_pairs)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    for (int seed = 0; seed < 12; ++seed)
    {
        const bfs::TraversalReport r = bfs::explore(g, seed);
        for (int v = 0; v < 12; ++v)
        {
            const analysis::PathResult path = analysis::dependencyPath(g, seed, v);
            CHECK_EQ(r.level[static_cast<std::size_t>(v)] != -1, path.found);
        }
    }
}

// ===========================================================================
// Layer 4 — scanned fixture drives the BFS core end-to-end
// ===========================================================================

TEST(integration, scanned_fixture_graph_drives_bfs_core)
{
    const std::string fixtureRoot = [] {
        fs::path current = fs::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            const fs::path candidate = current / "tests/fixtures/sample_project";
            if (fs::is_directory(candidate))
                return candidate.string();
            if (!current.has_parent_path())
                break;
            current = current.parent_path();
        }
        return std::string();
    }();
    CHECK(!fixtureRoot.empty()); // fixture must exist for this suite

    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;
    CHECK(io::scanProject(fixtureRoot, graph, summary, issues, warnings));
    CHECK(graph.isFinalized());

    // B1 from main.cpp (id 8): exact levels/counters on the real scan.
    const bfs::TraversalReport forward = bfs::explore(graph, 8);
    CHECK_EQ(forward.level[static_cast<std::size_t>(1)], 1);
    CHECK_EQ(forward.level[static_cast<std::size_t>(11)], 1);
    CHECK_EQ(forward.level[static_cast<std::size_t>(5)], 2);
    CHECK_EQ(forward.level[static_cast<std::size_t>(10)], 2);
    CHECK_EQ(forward.counters.verticesVisited, std::size_t{5});
    CHECK_EQ(forward.counters.edgesExamined, std::size_t{4});
    CHECK_EQ(forward.counters.maxFrontier, std::size_t{2});

    // B4 ≡ Phase 4 oracle on the scanned graph.
    const bfs::TraversalReport reverse = bfs::exploreReverse(graph, 10);
    const analysis::ImpactReport impact = analysis::impactOf(graph, 10);
    for (int v = 0; v < 12; ++v)
        CHECK_EQ(reverse.level[static_cast<std::size_t>(v)],
                 impact.level[static_cast<std::size_t>(v)]);

    // B2 nearest-seed levels on the scanned graph.
    const bfs::TraversalReport multi = bfs::exploreMultiSource(graph, {8, 0});
    CHECK_EQ(multi.level[static_cast<std::size_t>(8)], 0);
    CHECK_EQ(multi.level[static_cast<std::size_t>(0)], 0);
    CHECK_EQ(multi.level[static_cast<std::size_t>(1)], 1);
    CHECK_EQ(multi.level[static_cast<std::size_t>(11)], 1);
    CHECK_EQ(multi.counters.verticesVisited, std::size_t{6});

    // B5 bounded to direct dependencies only.
    const bfs::TraversalReport bounded = bfs::exploreBounded(graph, 8, 1);
    CHECK_EQ(bounded.counters.verticesVisited, std::size_t{3});
    CHECK(bounded.boundHit == true);
}

// ===========================================================================
// B3 — unbounded bidirectional shortest path (Phase 5.2)
// ===========================================================================

TEST(bidirectional, length_and_found_match_dependency_path_all_pairs)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    for (int from = 0; from < 12; ++from)
    {
        for (int to = 0; to < 12; ++to)
        {
            const bfs::PathReport r = bfs::bidirectionalPath(g, from, to);
            const analysis::PathResult plain =
                analysis::dependencyPath(g, from, to);
            CHECK_EQ(r.found, plain.found);
            if (plain.found)
            {
                // Locked equivalence: same LENGTH; the exact route may differ
                // (both are shortest) but every hop must be a real edge.
                CHECK_EQ(r.path.size(), plain.path.size());
                CHECK(isValidPath(g, r.path, from, to));
            }
            else
            {
                CHECK(r.path.empty());
            }
        }
    }
}

TEST(bidirectional, direct_edge_wins_over_longer_alternative)
{
    // Same graph as the Phase 4 shortest-path test: direct 0→3 must beat
    // the 0→1→2→3 alternative — and here the exact routes must be identical.
    graph::DependencyGraph g;
    for (int i = 0; i < 4; ++i)
        g.addNode("p" + std::to_string(i) + ".cpp", "p" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 3));
    CHECK(g.addEdge(0, 3));
    g.finalize();

    const bfs::PathReport r = bfs::bidirectionalPath(g, 0, 3);
    const analysis::PathResult plain = analysis::dependencyPath(g, 0, 3);
    CHECK(r.found);
    CHECK_EQ(r.path.size(), std::size_t{2});
    CHECK_EQ(r.path[0], 0);
    CHECK_EQ(r.path[1], 3);
    CHECK(r.path == plain.path);
}

TEST(bidirectional, same_file_single_entry_zero_counters)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const bfs::PathReport r = bfs::bidirectionalPath(g, 6, 6);
    CHECK(r.found);
    CHECK_EQ(r.path.size(), std::size_t{1});
    CHECK_EQ(r.path[0], 6);
    // No traversal ran: counters stay zero (documented trivial-case contract).
    CHECK_EQ(r.counters.verticesVisited, std::size_t{0});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{0});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{0});
}

TEST(bidirectional, no_path_is_explicitly_reported)
{
    const graph::DependencyGraph g = makeFixtureGraph();

    // Reverse direction: shared/util.h does not depend on main.cpp.
    const bfs::PathReport backward = bfs::bidirectionalPath(g, 10, 8);
    CHECK(backward.found == false);
    CHECK(backward.path.empty());
    CHECK(backward.counters.verticesVisited > std::size_t{0}); // work recorded

    // Isolated node 9 has no outgoing edges: the forward frontier dies.
    const bfs::PathReport isolated = bfs::bidirectionalPath(g, 9, 0);
    CHECK(isolated.found == false);
    CHECK(isolated.path.empty());
}

TEST(bidirectional, hub_meeting_counters_and_route_exact)
{
    // 0,4,5 depend on hub 1; hub 1 includes hub 2; hub 2 → 3, and 6,7 also
    // depend on hub 2. Dependency path 0 → 1 → 2 → 3 (3 edges). Rounds 1–2
    // expand the two seeds (2 edges); round 3's forward expansion meets hub 2
    // already claimed by the reverse search (1 edge).
    graph::DependencyGraph g;
    for (int i = 0; i < 8; ++i)
        g.addNode("h" + std::to_string(i) + ".cpp", "h" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1)); // leaf → hubA
    CHECK(g.addEdge(4, 1));
    CHECK(g.addEdge(5, 1));
    CHECK(g.addEdge(1, 2)); // hubA includes hubB
    CHECK(g.addEdge(2, 3)); // hubB → leaf (dependency direction!)
    CHECK(g.addEdge(6, 2));
    CHECK(g.addEdge(7, 2));
    g.finalize();

    const bfs::PathReport r = bfs::bidirectionalPath(g, 0, 3);
    CHECK(r.found);
    CHECK_EQ(r.path.size(), std::size_t{4}); // 0,1,2,3 = 3 edges (shortest)
    CHECK_EQ(r.counters.verticesVisited, std::size_t{4}); // seeds + both hubs
    CHECK_EQ(r.counters.edgesExamined, std::size_t{3});
    CHECK_EQ(r.counters.maxFrontier, std::size_t{2}); // |F|+|B| combined
    CHECK_EQ(r.path[0], 0);
    CHECK_EQ(r.path[1], 1);
    CHECK_EQ(r.path[2], 2);
    CHECK_EQ(r.path[3], 3);
}

TEST(bidirectional, cycle_on_the_route_terminates_with_shortest_path)
{
    // 0→1→2→0 cycle; asking 0→2 must terminate and match plain BFS exactly.
    graph::DependencyGraph g;
    for (int i = 0; i < 3; ++i)
        g.addNode("cy" + std::to_string(i) + ".h", "cy" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 0));
    g.finalize();

    const bfs::PathReport r = bfs::bidirectionalPath(g, 0, 2);
    const analysis::PathResult plain = analysis::dependencyPath(g, 0, 2);
    CHECK(r.found);
    CHECK(r.path == plain.path); // [0,1,2]: the cycle adds nothing
    CHECK_EQ(r.path.size(), std::size_t{3});
}

TEST(bidirectional, wider_frontiers_are_counted_combined)
{
    // Frontiers of different widths; B3 must record |F|+|B| peaks, and the
    // smaller side expands first (here B's single-node frontier after F's
    // wide first wave).
    graph::DependencyGraph g;
    for (int i = 0; i < 10; ++i)
        g.addNode("w" + std::to_string(i) + ".cpp", "w" + std::to_string(i) + ".cpp");
    // from = 0 with three direct dependencies 1,2,3; 1→4→5→9 = the route;
    // to = 9 with a single dependent 8 (so B's frontier stays thin).
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(0, 2));
    CHECK(g.addEdge(0, 3));
    CHECK(g.addEdge(1, 4));
    CHECK(g.addEdge(4, 5));
    CHECK(g.addEdge(5, 9));
    CHECK(g.addEdge(8, 9));
    g.finalize();

    const bfs::PathReport r = bfs::bidirectionalPath(g, 0, 9);
    const analysis::PathResult plain = analysis::dependencyPath(g, 0, 9);
    CHECK(r.found);
    CHECK_EQ(r.path.size(), plain.path.size()); // [0,1,4,5,9]: 4 edges
    CHECK(r.path == plain.path);
    // seeds {0,9} + F wave {1,2,3} + B wave {5,8} + F wave {4} = 8 distinct
    CHECK_EQ(r.counters.verticesVisited, std::size_t{8});
    CHECK_EQ(r.counters.edgesExamined, std::size_t{7}); // 3+2+1+1 scans
    CHECK_EQ(r.counters.maxFrontier, std::size_t{5});   // |F|=3 + |B|=2 peak
}

TEST(integration, scanned_fixture_bidirectional_path)
{
    const std::string fixtureRoot = [] {
        fs::path current = fs::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            const fs::path candidate = current / "tests/fixtures/sample_project";
            if (fs::is_directory(candidate))
                return candidate.string();
            if (!current.has_parent_path())
                break;
            current = current.parent_path();
        }
        return std::string();
    }();
    CHECK(!fixtureRoot.empty());

    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;
    CHECK(io::scanProject(fixtureRoot, graph, summary, issues, warnings));
    CHECK(graph.isFinalized());

    // main.cpp → shared/util.h: exact 2-hop route on the real scan.
    const bfs::PathReport r = bfs::bidirectionalPath(graph, 8, 10);
    CHECK(r.found);
    CHECK_EQ(r.path.size(), std::size_t{3}); // 8 → 11 → 10
    CHECK_EQ(r.path[0], 8);
    CHECK_EQ(r.path[1], 11);
    CHECK_EQ(r.path[2], 10);
    CHECK(r.path == analysis::dependencyPath(graph, 8, 10).path);

    // main.cpp → database/database.h: exact 2-hop route via api/api.h.
    const bfs::PathReport viaApi = bfs::bidirectionalPath(graph, 8, 5);
    CHECK(viaApi.found);
    CHECK_EQ(viaApi.path.size(), std::size_t{3});
    CHECK(viaApi.path == analysis::dependencyPath(graph, 8, 5).path);

    // Unreachable pair agrees with the plain-BFS oracle.
    const bfs::PathReport unreachable = bfs::bidirectionalPath(graph, 10, 8);
    CHECK(unreachable.found == false);
    CHECK(analysis::dependencyPath(graph, 10, 8).found == false);
}

// ===========================================================================
// B6 — direction-optimizing BFS (Phase 5.3)
// ===========================================================================
//
// Ruling §9.19: tests verify correctness (levels ≡ B1) and counter
// consistency — they NEVER force a switching direction.

TEST(direction_optimizing, levels_match_explore_on_hand_built_graphs)
{
    // chain, star, diamond, self-loop: four hand-built disciplines.
    {
        graph::DependencyGraph g; // chain 0→1→2→3→4
        for (int i = 0; i < 5; ++i)
            g.addNode("c" + std::to_string(i) + ".cpp",
                      "c" + std::to_string(i) + ".cpp");
        for (int i = 0; i + 1 < 5; ++i)
            CHECK(g.addEdge(i, i + 1));
        g.finalize();
        const bfs::TraversalReport plain = bfs::explore(g, 0);
        const bfs::TraversalReport optim = bfs::exploreDirectionOptimizing(g, 0);
        for (int i = 0; i < 5; ++i)
            CHECK_EQ(optim.level[static_cast<std::size_t>(i)],
                     plain.level[static_cast<std::size_t>(i)]);
        CHECK_EQ(optim.counters.verticesVisited,
                 plain.counters.verticesVisited);
    }
    {
        graph::DependencyGraph g; // star hub 0, leaves 1..5
        for (int i = 0; i < 6; ++i)
            g.addNode("s" + std::to_string(i) + ".h",
                      "s" + std::to_string(i) + ".h");
        for (int leaf = 1; leaf < 6; ++leaf)
            CHECK(g.addEdge(0, leaf));
        g.finalize();
        const bfs::TraversalReport plain = bfs::explore(g, 0);
        const bfs::TraversalReport optim = bfs::exploreDirectionOptimizing(g, 0);
        for (int i = 0; i < 6; ++i)
            CHECK_EQ(optim.level[static_cast<std::size_t>(i)],
                     plain.level[static_cast<std::size_t>(i)]);
    }
    {
        graph::DependencyGraph g; // diamond + a second component + self-loop
        for (int i = 0; i < 7; ++i)
            g.addNode("m" + std::to_string(i) + ".h",
                      "m" + std::to_string(i) + ".h");
        CHECK(g.addEdge(0, 1));
        CHECK(g.addEdge(0, 2));
        CHECK(g.addEdge(1, 3));
        CHECK(g.addEdge(2, 3));
        CHECK(g.addEdge(4, 4)); // self-loop: claim guard must hold
        CHECK(g.addEdge(5, 6)); // separate component: unreachable from 0
        g.finalize();
        const bfs::TraversalReport plain = bfs::explore(g, 0);
        const bfs::TraversalReport optim = bfs::exploreDirectionOptimizing(g, 0);
        for (int i = 0; i < 7; ++i)
            CHECK_EQ(optim.level[static_cast<std::size_t>(i)],
                     plain.level[static_cast<std::size_t>(i)]);
        CHECK_EQ(optim.level[static_cast<std::size_t>(4)], -1);
        CHECK_EQ(optim.level[static_cast<std::size_t>(5)], -1);
        CHECK_EQ(optim.level[static_cast<std::size_t>(6)], -1);
    }
}

TEST(direction_optimizing, generated_shapes_levels_match_explore)
{
    // All five approved shapes at test scale (n=120, fixed seed). B6 must
    // reproduce B1's levels and visited count everywhere; mode counters must
    // stay consistent (Σ modes == expanded levels == deepest level + 1).
    const bench::Shape shapes[] = {
        bench::Shape::Chain, bench::Shape::Tree,    bench::Shape::Sparse,
        bench::Shape::Dense, bench::Shape::HubHeavy,
    };
    const int n = 120;
    for (const bench::Shape shape : shapes)
    {
        graph::DependencyGraph g;
        bench::generateGraph(shape, n, 20261115, g);

        const int seed = n / 2;
        const bfs::TraversalReport plain = bfs::explore(g, seed);
        const bfs::TraversalReport optim =
            bfs::exploreDirectionOptimizing(g, seed);

        int deepest = 0;
        for (int i = 0; i < n; ++i)
        {
            CHECK_EQ(optim.level[static_cast<std::size_t>(i)],
                     plain.level[static_cast<std::size_t>(i)]);
            if (plain.level[static_cast<std::size_t>(i)] > deepest)
                deepest = plain.level[static_cast<std::size_t>(i)];
        }
        CHECK_EQ(optim.counters.verticesVisited,
                 plain.counters.verticesVisited);
        CHECK_EQ(static_cast<int>(optim.topDownLevels + optim.bottomUpLevels),
                 deepest + 1);
        CHECK(optim.boundHit == false);
        // On these instances the first level's push estimate always wins
        // (level-1 pull cost ≈ n·avgDeg ≫ outDegree of a single seed) — pins
        // THIS heuristic's behaviour on THIS instance, not a general law.
        CHECK(optim.topDownLevels >= 1);
    }
}

TEST(direction_optimizing, seed_and_empty_frontier_termination)
{
    graph::DependencyGraph g; // one isolated node + one node depending on it
    g.addNode("only.cpp", "only.cpp"); // 0
    g.addNode("dep.cpp", "dep.cpp");   // 1
    CHECK(g.addEdge(1, 0));
    g.finalize();

    // Seed with no dependencies: one top-down level, empty frontier, done.
    const bfs::TraversalReport r = bfs::exploreDirectionOptimizing(g, 0);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{1});
    CHECK_EQ(r.level[static_cast<std::size_t>(0)], 0);
    CHECK_EQ(r.level[static_cast<std::size_t>(1)], -1);
    CHECK_EQ(static_cast<int>(r.topDownLevels + r.bottomUpLevels), 1);

    // Reverse role: seed 1 pulls... heuristic decides; only invariants hold.
    const bfs::TraversalReport r2 = bfs::exploreDirectionOptimizing(g, 1);
    CHECK_EQ(r2.counters.verticesVisited, std::size_t{2});
    CHECK_EQ(r2.level[static_cast<std::size_t>(0)], 1);
    CHECK_EQ(static_cast<int>(r2.topDownLevels + r2.bottomUpLevels), 2);
}

TEST(integration, scanned_fixture_direction_optimizing_matches_explore)
{
    const std::string fixtureRoot = [] {
        fs::path current = fs::current_path();
        for (int depth = 0; depth < 8; ++depth)
        {
            const fs::path candidate = current / "tests/fixtures/sample_project";
            if (fs::is_directory(candidate))
                return candidate.string();
            if (!current.has_parent_path())
                break;
            current = current.parent_path();
        }
        return std::string();
    }();
    CHECK(!fixtureRoot.empty());

    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;
    CHECK(io::scanProject(fixtureRoot, graph, summary, issues, warnings));
    CHECK(graph.isFinalized());

    const bfs::TraversalReport plain = bfs::explore(graph, 8);
    const bfs::TraversalReport optim = bfs::exploreDirectionOptimizing(graph, 8);
    for (int v = 0; v < 12; ++v)
        CHECK_EQ(optim.level[static_cast<std::size_t>(v)],
                 plain.level[static_cast<std::size_t>(v)]);
    CHECK_EQ(optim.counters.verticesVisited,
             plain.counters.verticesVisited); // 5, as in the Phase 5.1 test
    CHECK_EQ(static_cast<int>(optim.topDownLevels + optim.bottomUpLevels), 3);
}
