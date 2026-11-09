// tests/analysis_tests.cpp
// Phase 4 tests for the analytics engine (F2–F8), per TESTING_STRATEGY.md
// Layer 2 (hand-built graphs, no filesystem) plus one Layer-4 integration
// test that runs the Phase 3 scanner and the algorithms end-to-end.
//
// The fixture graph replicates tests/fixtures/sample_project EXACTLY (12
// nodes / 9 edges, same paths/order) so hand-computed expectations are
// shared with the scanner test suite.
//
// Risk-based approach: every synopsis edge-case class (acyclic, 2-cycle,
// 3-cycle, self-include, disconnected, diamond, duplicate basenames,
// empty/single graphs, guard limits) has at least one focused test; no
// trivial repetition.

#include "graph/analysis.h"
#include "graph/dependency_graph.h"
#include "io/project_scanner.h"

#include <filesystem>
#include <string>
#include <vector>

#include "test_framework.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// helpers (test infrastructure only)
// ---------------------------------------------------------------------------

// Comma-joined ids for human-readable exact-list assertions.
static std::string seqToString(const std::vector<int>& values)
{
    std::string result;
    bool first = true;
    for (const int value : values)
    {
        if (!first)
            result += ',';
        result += std::to_string(value);
        first = false;
    }
    return result;
}

// Replicates tests/fixtures/sample_project in memory (scanner-independent):
// 12 nodes, ids follow the scanner's sorted relative-path order.
static graph::DependencyGraph makeFixtureGraph()
{
    graph::DependencyGraph g;
    g.addNode("api/api.cpp", "api.cpp");         // 0
    g.addNode("api/api.h", "api.h");             // 1
    g.addNode("cycle_a.h", "cycle_a.h");         // 2
    g.addNode("cycle_b.h", "cycle_b.h");         // 3
    g.addNode("database/database.cpp", "database.cpp"); // 4  (angle only)
    g.addNode("database/database.h", "database.h");     // 5  (unresolved)
    g.addNode("graph.cpp", "graph.cpp");         // 6
    g.addNode("graph.h", "graph.h");             // 7
    g.addNode("main.cpp", "main.cpp");           // 8
    g.addNode("network/api.h", "api.h");         // 9  (duplicate basename)
    g.addNode("shared/util.h", "util.h");        // 10
    g.addNode("utils/utils.h", "utils.h");       // 11

    CHECK(g.addEdge(0, 1));   // api.cpp → api.h
    CHECK(g.addEdge(1, 5));   // api.h → database.h ("missing.h" unresolved)
    CHECK(g.addEdge(2, 3));   // 2-cycle
    CHECK(g.addEdge(3, 2));
    CHECK(g.addEdge(6, 7));   // graph.cpp → graph.h
    CHECK(g.addEdge(6, 10));  // graph.cpp → shared/util.h
    CHECK(g.addEdge(8, 1));   // main.cpp → api/api.h
    CHECK(g.addEdge(8, 11));  // main.cpp → utils/utils.h
    CHECK(g.addEdge(11, 10)); // utils/utils.h → shared/util.h
    g.finalize();
    return g;
}

// Validates an F4 order: every non-blocked node exactly once, and every edge
// u→v compiles v (the dependency) strictly before u (the dependent).
static bool isValidCompileOrder(const graph::DependencyGraph& g,
                                const std::vector<int>& order,
                                const std::vector<int>& blocked)
{
    std::vector<int> position(g.nodeCount(), -1);
    std::vector<bool> seen(g.nodeCount(), false);
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const int id = order[i];
        if (id < 0 || id >= g.nodeCount() || seen[id])
            return false;
        seen[id] = true;
        position[id] = static_cast<int>(i);
    }
    for (const int blockedId : blocked)
        if (blockedId >= 0 && blockedId < g.nodeCount() && seen[blockedId])
            return false; // blocked files must not appear as ordered
    for (int u = 0; u < g.nodeCount(); ++u)
    {
        if (!seen[u])
            continue; // cycle-blocked: validated above
        for (const int v : g.successors(u))
            if (seen[v] && position[v] >= position[u])
                return false; // dependency not compiled before dependent
    }
    return true;
}

// ===========================================================================
// F2 — cycle detection
// ===========================================================================

TEST(cycles, acyclic_graphs_report_no_cycles)
{
    // Diamond + disconnected node + a separate two-node chain: nothing loops.
    graph::DependencyGraph g;
    for (int i = 0; i < 7; ++i)
        g.addNode("f" + std::to_string(i) + ".cpp", "f" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(0, 2));
    CHECK(g.addEdge(1, 3));
    CHECK(g.addEdge(2, 3));
    CHECK(g.addEdge(3, 4));
    CHECK(g.addEdge(5, 6)); // second component
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g);
    CHECK(report.hasCycles == false);
    CHECK_EQ(report.cyclesFound, std::size_t{0});
    CHECK(report.cycleInstances.empty());
    CHECK(report.depthGuardTripped == false);
}

TEST(cycles, two_cycle_reports_exact_closed_path)
{
    graph::DependencyGraph g;
    g.addNode("a.h", "a.h"); // 0
    g.addNode("b.h", "b.h"); // 1
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 0));
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g);
    CHECK(report.hasCycles);
    CHECK_EQ(report.cyclesFound, std::size_t{1});
    CHECK_EQ(report.cycleInstances.size(), std::size_t{1});
    // Closed path: first == last, in edge order.
    CHECK_EQ(seqToString(report.cycleInstances[0]), std::string("0,1,0"));
    CHECK_EQ(report.cycleInstances[0].front(),
             report.cycleInstances[0].back());
}

TEST(cycles, three_cycle_reports_exact_closed_path)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 3; ++i)
        g.addNode("c" + std::to_string(i) + ".h", "c" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 0));
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g);
    CHECK(report.hasCycles);
    CHECK_EQ(report.cycleInstances.size(), std::size_t{1});
    CHECK_EQ(seqToString(report.cycleInstances[0]), std::string("0,1,2,0"));
}

TEST(cycles, self_loop_reports_length_one_cycle)
{
    graph::DependencyGraph g;
    g.addNode("x.cpp", "x.cpp");
    CHECK(g.addEdge(0, 0)); // ruling B1: self-edges are real edges
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g);
    CHECK(report.hasCycles);
    CHECK_EQ(seqToString(report.cycleInstances[0]), std::string("0,0"));
}

TEST(cycles, report_cap_limits_storage_not_detection)
{
    // Three disjoint 2-cycles; cap = 2 stored instances. Detection must
    // still see ALL cycles (ruling: cap bounds the report, not the DFS).
    graph::DependencyGraph g;
    for (int i = 0; i < 6; ++i)
        g.addNode("n" + std::to_string(i) + ".h", "n" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 0));
    CHECK(g.addEdge(2, 3));
    CHECK(g.addEdge(3, 2));
    CHECK(g.addEdge(4, 5));
    CHECK(g.addEdge(5, 4));
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g, 2);
    CHECK(report.hasCycles);
    CHECK_EQ(report.cyclesFound, std::size_t{3});       // uncapped counter
    CHECK_EQ(report.cycleInstances.size(), std::size_t{2}); // capped storage
}

TEST(cycles, disconnected_components_all_examined)
{
    graph::DependencyGraph g; // component A: clean chain; component B: cycle
    for (int i = 0; i < 4; ++i)
        g.addNode("d" + std::to_string(i) + ".h", "d" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(2, 3));
    CHECK(g.addEdge(3, 2));
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g);
    CHECK(report.hasCycles);
    CHECK_EQ(report.cycleInstances.size(), std::size_t{1});
    CHECK_EQ(seqToString(report.cycleInstances[0]), std::string("2,3,2"));
}

TEST(cycles, depth_guard_trips_gracefully)
{
    // 14-deep chain, guard at 6: recursion stops, flag raised, NO crash —
    // and no cycle claims (results are partial when guarded).
    graph::DependencyGraph g;
    for (int i = 0; i < 14; ++i)
        g.addNode("deep_" + std::to_string(i) + ".cpp",
                  "deep_" + std::to_string(i) + ".cpp");
    for (int i = 0; i + 1 < 14; ++i)
        CHECK(g.addEdge(i, i + 1));
    g.finalize();

    const analysis::CycleReport report = analysis::findCycles(g, 16, 6);
    CHECK(report.depthGuardTripped);
    // guarded run: must not claim a full verdict either way
    CHECK(report.cycleInstances.empty());
}

// ===========================================================================
// F3 — impact analysis
// ===========================================================================

TEST(impact, fixture_shared_util_change_levels)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::ImpactReport report = analysis::impactOf(g, 10);

    CHECK_EQ(report.level[10], 0);  // changed file itself (ruling R3)
    CHECK_EQ(report.level[6], 1);   // graph.cpp includes it directly
    CHECK_EQ(report.level[11], 1);  // utils/utils.h includes it directly
    CHECK_EQ(report.level[8], 2);   // main.cpp depends transitively
    CHECK_EQ(report.level[2], -1);  // cycle group untouched
    CHECK_EQ(report.level[3], -1);
    CHECK_EQ(report.level[7], -1);  // graph.h is a dependency OF graph.cpp,
                                    // not a dependent: reverse direction
    CHECK_EQ(report.affectedCount, std::size_t{4});
}

TEST(impact, fixture_api_header_change_levels)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::ImpactReport report = analysis::impactOf(g, 1);

    // Reverse-view sanity: impact flows over DEPENDENTS only. database.h is
    // a dependency OF api.h (edge 1→5), so a change to api/api.h must NOT
    // mark it affected.
    CHECK_EQ(report.level[1], 0);
    CHECK_EQ(report.level[0], 1); // api.cpp includes api.h
    CHECK_EQ(report.level[8], 1); // main.cpp
    CHECK_EQ(report.level[5], -1); // dependency direction: NOT impacted
    CHECK_EQ(report.level[9], -1); // same basename, different file: unaffected
    CHECK_EQ(report.affectedCount, std::size_t{3});
}

TEST(impact, diamond_converges_through_both_branches)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 4; ++i)
        g.addNode("f" + std::to_string(i) + ".h", "f" + std::to_string(i) + ".h");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(0, 2));
    CHECK(g.addEdge(1, 3));
    CHECK(g.addEdge(2, 3));
    g.finalize();

    const analysis::ImpactReport report = analysis::impactOf(g, 3);
    CHECK_EQ(report.level[1], 1);
    CHECK_EQ(report.level[2], 1); // both parents reached at their BFS level
    CHECK_EQ(report.level[0], 2); // shared ancestor via EITHER branch
    CHECK_EQ(report.affectedCount, std::size_t{4});
}

TEST(impact, unrelated_component_untouched)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::ImpactReport report = analysis::impactOf(g, 0);
    // 0 is upstream of its own includes in reverse view: almost everyone is
    // unaffected; only files that (transitively) include 0 are affected.
    CHECK_EQ(report.level[0], 0);
    CHECK_EQ(report.level[1], -1); // direction check: 0 depends on 1, NOT
                                   // the other way round
    CHECK_EQ(report.affectedCount, std::size_t{1});
}

TEST(impact, self_loop_seed_stays_sane)
{
    graph::DependencyGraph g;
    g.addNode("x.h", "x.h"); // 0: includes itself
    g.addNode("y.h", "y.h"); // 1: includes x
    CHECK(g.addEdge(0, 0));
    CHECK(g.addEdge(1, 0));
    g.finalize();

    const analysis::ImpactReport report = analysis::impactOf(g, 0);
    CHECK_EQ(report.level[0], 0);
    CHECK_EQ(report.level[1], 1);
    CHECK_EQ(report.affectedCount, std::size_t{2}); // no self-loop echo
}

// ===========================================================================
// F4 — topological compilation order
// ===========================================================================

TEST(topo, linear_chain_is_dependency_first)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 4; ++i)
        g.addNode("l" + std::to_string(i) + ".cpp", "l" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 3));
    g.finalize();

    const analysis::TopoReport report = analysis::topologicalOrder(g);
    CHECK(report.cycleBlocked.empty());
    CHECK_EQ(seqToString(report.ordered), std::string("3,2,1,0"));
}

TEST(topo, fixture_order_is_valid_and_cycle_blocked_leftover)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::TopoReport report = analysis::topologicalOrder(g);

    CHECK_EQ(report.ordered.size(), std::size_t{10}); // all non-cycle files
    CHECK_EQ(seqToString(report.cycleBlocked), std::string("2,3"));
    CHECK(isValidCompileOrder(g, report.ordered, report.cycleBlocked));
    // Deterministic seed order: the smallest out-degree-0 id comes first.
    CHECK_EQ(report.ordered.front(), 4);
}

TEST(topo, disconnected_components_all_covered)
{
    graph::DependencyGraph g; // two independent chains
    for (int i = 0; i < 6; ++i)
        g.addNode("m" + std::to_string(i) + ".cpp", "m" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(3, 4));
    CHECK(g.addEdge(4, 5));
    g.finalize();

    const analysis::TopoReport report = analysis::topologicalOrder(g);
    CHECK(report.cycleBlocked.empty());
    CHECK_EQ(report.ordered.size(), std::size_t{6});
    CHECK(isValidCompileOrder(g, report.ordered, report.cycleBlocked));
}

TEST(topo, self_loop_blocked_without_halting_others)
{
    graph::DependencyGraph g;
    g.addNode("selfy.cpp", "selfy.cpp"); // 0: self-include
    g.addNode("pend.cpp", "pend.cpp");   // 1 (depends on 2)
    g.addNode("base.cpp", "base.cpp");   // 2
    CHECK(g.addEdge(0, 0));
    CHECK(g.addEdge(1, 2));
    g.finalize();

    const analysis::TopoReport report = analysis::topologicalOrder(g);
    CHECK_EQ(seqToString(report.cycleBlocked), std::string("0"));
    CHECK(isValidCompileOrder(g, report.ordered, report.cycleBlocked));
    CHECK_EQ(report.ordered.size(), std::size_t{2}); // the rest still compiles
}

TEST(topo, empty_and_single_graphs)
{
    graph::DependencyGraph empty;
    empty.finalize();
    const analysis::TopoReport emptyReport = analysis::topologicalOrder(empty);
    CHECK(emptyReport.ordered.empty());
    CHECK(emptyReport.cycleBlocked.empty());

    graph::DependencyGraph single;
    single.addNode("only.cpp", "only.cpp");
    single.finalize();
    const analysis::TopoReport singleReport = analysis::topologicalOrder(single);
    CHECK_EQ(seqToString(singleReport.ordered), std::string("0"));
    CHECK(singleReport.cycleBlocked.empty());
}

// ===========================================================================
// F5 — deepest dependency chain
// ===========================================================================

TEST(chain, linear_dag_returns_whole_sequence)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 4; ++i)
        g.addNode("l" + std::to_string(i) + ".cpp", "l" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 3));
    g.finalize();

    const analysis::ChainReport report = analysis::longestChain(g);
    CHECK(report.defined);
    CHECK(report.depthGuardTripped == false);
    CHECK_EQ(seqToString(report.chain), std::string("0,1,2,3"));
}

TEST(chain, diamond_picks_longer_leg)
{
    graph::DependencyGraph g;
    for (int i = 0; i < 5; ++i)
        g.addNode("f" + std::to_string(i) + ".cpp", "f" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 3)); // leg A: 0→1→2→3 (4 nodes)
    CHECK(g.addEdge(0, 4)); // leg B: 0→4 (2 nodes)
    g.finalize();

    const analysis::ChainReport report = analysis::longestChain(g);
    CHECK(report.defined);
    CHECK_EQ(seqToString(report.chain), std::string("0,1,2,3"));
}

TEST(chain, cycles_make_the_answer_undefined)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::ChainReport report = analysis::longestChain(g);

    // Locked policy: undefined; a closed cycle instance is reported instead.
    CHECK(report.defined == false);
    CHECK(report.chain.empty());
    CHECK_EQ(seqToString(report.firstCycle), std::string("2,3,2"));
    CHECK_EQ(report.firstCycle.front(), report.firstCycle.back());
}

TEST(chain, guard_trip_reports_incomplete_not_partial)
{
    graph::DependencyGraph g; // 14-deep chain, guard at 6 (review ruling #3)
    for (int i = 0; i < 14; ++i)
        g.addNode("deep_" + std::to_string(i) + ".cpp",
                  "deep_" + std::to_string(i) + ".cpp");
    for (int i = 0; i + 1 < 14; ++i)
        CHECK(g.addEdge(i, i + 1));
    g.finalize();

    const analysis::ChainReport report = analysis::longestChain(g, 6);
    CHECK(report.defined == false);
    CHECK(report.depthGuardTripped);   // flagged as incomplete
    CHECK(report.chain.empty());       // NO partially computed chain
    CHECK(report.firstCycle.empty());  // cycles were not the reason
}

TEST(chain, empty_and_single_graphs)
{
    graph::DependencyGraph empty;
    empty.finalize();
    const analysis::ChainReport emptyReport = analysis::longestChain(empty);
    CHECK(emptyReport.defined);
    CHECK(emptyReport.chain.empty());

    graph::DependencyGraph single;
    single.addNode("only.cpp", "only.cpp");
    single.finalize();
    const analysis::ChainReport singleReport = analysis::longestChain(single);
    CHECK(singleReport.defined);
    CHECK_EQ(seqToString(singleReport.chain), std::string("0"));
}

// ===========================================================================
// F6 — highly connected modules (hub ranking)
// ===========================================================================

TEST(hubs, fixture_top_two_with_tie_broken_by_id)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    // api/api.h (1) and shared/util.h (10) both have in-degree 2; the
    // deterministic tie-break prefers the smaller id (= lexicographic path).
    CHECK_EQ(seqToString(analysis::mostDependedUpon(g, 2)), std::string("1,10"));
    CHECK_EQ(seqToString(analysis::mostDependedUpon(g, 1)), std::string("1"));
}

TEST(hubs, k_boundaries_and_full_extraction)
{
    const graph::DependencyGraph g = makeFixtureGraph();

    CHECK(analysis::mostDependedUpon(g, 0).empty()); // k = 0 → nothing

    // Full extraction (k > V): descending in-degree, ascending id on ties.
    // expected: [1,10] (score 2), [2,3,5,7,11] (score 1), [0,4,6,8,9] (0)
    const std::vector<int> full = analysis::mostDependedUpon(g, 99);
    CHECK_EQ(full.size(), std::size_t{12});
    CHECK_EQ(seqToString(full), std::string("1,10,2,3,5,7,11,0,4,6,8,9"));
}

// ===========================================================================
// F7 — filename prefix search
// ===========================================================================

TEST(search, fixture_prefix_hits_and_duplicate_basenames)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const ds::Trie index = analysis::buildNameIndex(g); // caller-owned

    // Duplicate basenames both match one prefix, distinct ids AND distinct
    // paths preserved (approved decision #4). "api.cpp" matches too — the
    // prefix keys on basenames, so id 0 joins its header (1) and the network
    // twin (9).
    const std::vector<int> apiHits = analysis::filesWithPrefix(g, index, "api");
    CHECK_EQ(seqToString(apiHits), std::string("0,1,9"));
    CHECK_EQ(g.node(1).path, std::string("api/api.h"));
    CHECK_EQ(g.node(9).path, std::string("network/api.h"));

    CHECK_EQ(seqToString(analysis::filesWithPrefix(g, index, "graph")),
             std::string("6,7"));
    CHECK_EQ(seqToString(analysis::filesWithPrefix(g, index, "utils")),
             std::string("11"));

    // Case-sensitive byte-exact matching (Phase 1 policy).
    CHECK(analysis::filesWithPrefix(g, index, "Graph").empty());
    // Prefixes key on BASENAMES, not paths.
    CHECK(analysis::filesWithPrefix(g, index, "network").empty());
}

TEST(search, existence_check_and_empty_prefix)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const ds::Trie index = analysis::buildNameIndex(g);

    CHECK(analysis::fileExistsByBasename(index, "api.h"));
    CHECK(analysis::fileExistsByBasename(index, "cycle_a.h"));
    CHECK(analysis::fileExistsByBasename(index, "missing.h") == false);

    // Ruling R5: empty prefix → empty result (documented Trie behavior).
    CHECK(analysis::filesWithPrefix(g, index, "").empty());
}

// ===========================================================================
// F8 — dependency path between two files
// ===========================================================================

TEST(path, fixture_main_to_shared_util_is_minimal)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::PathResult result = analysis::dependencyPath(g, 8, 10);

    CHECK(result.found);
    CHECK_EQ(seqToString(result.path), std::string("8,11,10")); // 2 hops
}

TEST(path, missing_paths_are_clearly_reported)
{
    const graph::DependencyGraph g = makeFixtureGraph();

    // Reverse direction: shared/util.h does not depend on main.cpp.
    const analysis::PathResult backward = analysis::dependencyPath(g, 10, 8);
    CHECK(backward.found == false);
    CHECK(backward.path.empty());

    // Node 9 is isolated: no dependency path leaves it.
    const analysis::PathResult isolated = analysis::dependencyPath(g, 9, 0);
    CHECK(isolated.found == false);
    CHECK(isolated.path.empty());
}

TEST(path, direct_edge_wins_over_longer_alternative)
{
    // 0→1→2→3 (3 hops) AND the direct edge 0→3 (1 hop): BFS must report the
    // shortest path regardless of adjacency insertion order.
    graph::DependencyGraph g;
    for (int i = 0; i < 4; ++i)
        g.addNode("p" + std::to_string(i) + ".cpp", "p" + std::to_string(i) + ".cpp");
    CHECK(g.addEdge(0, 1));
    CHECK(g.addEdge(1, 2));
    CHECK(g.addEdge(2, 3));
    CHECK(g.addEdge(0, 3));
    g.finalize();

    const analysis::PathResult result = analysis::dependencyPath(g, 0, 3);
    CHECK(result.found);
    CHECK_EQ(seqToString(result.path), std::string("0,3"));
    CHECK_EQ(result.path.size(), std::size_t{2});
}

TEST(path, same_file_is_single_entry)
{
    const graph::DependencyGraph g = makeFixtureGraph();
    const analysis::PathResult result = analysis::dependencyPath(g, 6, 6);
    CHECK(result.found);
    CHECK_EQ(seqToString(result.path), std::string("6"));
}

// ===========================================================================
// Layer 4 — scanned fixture + algorithms end-to-end
// ===========================================================================

TEST(integration, scanned_fixture_graph_drives_all_features)
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
    CHECK(graph.isFinalized()); // scanner finalized; algorithms may proceed

    // Identical graph to the hand-built fixture → same hand-computed
    // answers for every feature (cross-validates Phases 3 and 4).
    const analysis::ImpactReport impact = analysis::impactOf(graph, 10);
    CHECK_EQ(impact.level[6], 1);
    CHECK_EQ(impact.level[8], 2);
    CHECK_EQ(impact.affectedCount, std::size_t{4});

    const analysis::TopoReport topo = analysis::topologicalOrder(graph);
    CHECK_EQ(topo.ordered.size(), std::size_t{10});
    CHECK_EQ(seqToString(topo.cycleBlocked), std::string("2,3"));
    CHECK(isValidCompileOrder(graph, topo.ordered, topo.cycleBlocked));

    const analysis::CycleReport cycles = analysis::findCycles(graph);
    CHECK(cycles.hasCycles);
    CHECK_EQ(seqToString(cycles.cycleInstances[0]), std::string("2,3,2"));

    const analysis::ChainReport chain = analysis::longestChain(graph);
    CHECK(chain.defined == false); // fixture contains the 2-cycle

    const std::vector<int> hubs = analysis::mostDependedUpon(graph, 2);
    CHECK_EQ(seqToString(hubs), std::string("1,10"));

    const ds::Trie index = analysis::buildNameIndex(graph);
    CHECK_EQ(seqToString(analysis::filesWithPrefix(graph, index, "api")),
             std::string("0,1,9"));

    const analysis::PathResult path = analysis::dependencyPath(graph, 8, 10);
    CHECK_EQ(seqToString(path.path), std::string("8,11,10"));
}
