// tests/bench_tests.cpp
// Phase 5.4 tests for the benchmark graph generators (TESTING_STRATEGY
// Layer 5): determinism, exact edge budgets, acyclicity (Phase 4 oracle) and
// hub-mass concentration. Small sizes keep the suite fast; the benchmark
// binary exercises the full 1k/5k/10k matrix.

#include "bench/graph_generators.h"
#include "bench/benchmark_runner.h"
#include "graph/analysis.h"
#include "graph/bfs.h"
#include "graph/dependency_graph.h"

#include <filesystem>
#include <string>
#include <vector>

#include "test_framework.h"

// Edge-list equality: node paths, per-node successor sequences, edge count.
static bool sameEdgeList(const graph::DependencyGraph& a,
                         const graph::DependencyGraph& b)
{
    if (a.nodeCount() != b.nodeCount() || a.edgeCount() != b.edgeCount())
        return false;
    for (int id = 0; id < a.nodeCount(); ++id)
    {
        if (a.node(id).path != b.node(id).path)
            return false;
        const ds::LinkedList<int>& as = a.successors(id);
        const ds::LinkedList<int>& bs = b.successors(id);
        if (as.size() != bs.size())
            return false;
        auto ia = as.begin();
        auto ib = bs.begin();
        while (ia != as.end())
        {
            if (*ia != *ib)
                return false;
            ++ia;
            ++ib;
        }
    }
    return true;
}

TEST(generators, same_seed_reproduces_the_exact_edge_list)
{
    const bench::Shape shapes[] = {
        bench::Shape::Chain, bench::Shape::Tree,    bench::Shape::Sparse,
        bench::Shape::Dense, bench::Shape::HubHeavy,
    };
    for (const bench::Shape shape : shapes)
    {
        graph::DependencyGraph first;
        bench::generateGraph(shape, 200, 77, first);
        graph::DependencyGraph second;
        bench::generateGraph(shape, 200, 77, second);
        CHECK(sameEdgeList(first, second)); // determinism contract
    }
}

TEST(generators, different_seed_changes_the_random_shapes)
{
    // Structural shapes are seed-independent by definition; the random ones
    // must differ (probability of an identical 500-edge list ≈ 0).
    for (const bench::Shape shape : {bench::Shape::Sparse, bench::Shape::Dense,
                                     bench::Shape::HubHeavy,
                                     bench::Shape::Tree})
    {
        graph::DependencyGraph a;
        bench::generateGraph(shape, 100, 1, a);
        graph::DependencyGraph b;
        bench::generateGraph(shape, 100, 2, b);
        CHECK(sameEdgeList(a, b) == false);
    }
}

TEST(generators, edge_budgets_are_exact)
{
    const int n = 300;
    {
        graph::DependencyGraph g;
        bench::generateGraph(bench::Shape::Chain, n, 5, g);
        CHECK_EQ(g.edgeCount(), std::size_t{299}); // n−1
    }
    {
        graph::DependencyGraph g;
        bench::generateGraph(bench::Shape::Tree, n, 5, g);
        CHECK_EQ(g.edgeCount(), std::size_t{299}); // n−1
    }
    {
        graph::DependencyGraph g;
        bench::generateGraph(bench::Shape::Sparse, n, 5, g);
        CHECK_EQ(g.edgeCount(), std::size_t{5 * n / 2}); // 2.5 per node
    }
    {
        graph::DependencyGraph g;
        bench::generateGraph(bench::Shape::Dense, n, 5, g);
        CHECK_EQ(g.edgeCount(), std::size_t{8 * n}); // approved dense cap
    }
    {
        graph::DependencyGraph g;
        bench::generateGraph(bench::Shape::HubHeavy, n, 5, g);
        CHECK_EQ(g.edgeCount(), std::size_t{2 * n});
    }
}

TEST(generators, all_shapes_acyclic_per_construction_rule)
{
    // Every generator only draws targets strictly below the source id, so
    // even the cycle detector (Phase 4 oracle) must find nothing.
    const bench::Shape shapes[] = {
        bench::Shape::Chain, bench::Shape::Tree,    bench::Shape::Sparse,
        bench::Shape::Dense, bench::Shape::HubHeavy,
    };
    for (const bench::Shape shape : shapes)
    {
        graph::DependencyGraph g;
        bench::generateGraph(shape, 500, 42, g);
        const analysis::CycleReport report = analysis::findCycles(g);
        CHECK(report.hasCycles == false);
        CHECK_EQ(report.cyclesFound, std::size_t{0});
    }
}

TEST(generators, hub_heavy_concentrates_in_degree_on_the_hub_set)
{
    const int n = 1000;
    graph::DependencyGraph g;
    bench::generateGraph(bench::Shape::HubHeavy, n, 99, g);

    // Hubs: ids 19, 39, … (5% of nodes). By construction 40% of the 2n edge
    // budget targets them, so Σ hub in-degree ≥ 0.8n — versus an average
    // in-degree of only 2 per node overall.
    std::size_t hubInDegree = 0;
    int hubCount = 0;
    for (int id = 0; id < n; ++id)
        if (bench::isHubId(id))
        {
            ++hubCount;
            hubInDegree += g.inDegree(id);
        }
    CHECK_EQ(hubCount, n / 20);
    CHECK(hubInDegree >= static_cast<std::size_t>(8) * n / 10);

    // And the concentration is real: the busiest hub beats the average by an
    // order of magnitude.
    std::size_t maxIn = 0;
    for (int id = 0; id < n; ++id)
        if (g.inDegree(id) > maxIn)
            maxIn = g.inDegree(id);
    CHECK(maxIn >= 5 * (g.edgeCount() / static_cast<std::size_t>(n)));
}

TEST(generators, chain_orientation_points_to_earlier_ids)
{
    // Ruling §9.25c: chain = node i depends on node i−1.
    graph::DependencyGraph g;
    bench::generateGraph(bench::Shape::Chain, 50, 3, g);
    for (int i = 1; i < 50; ++i)
    {
        const ds::LinkedList<int>& succs = g.successors(i);
        CHECK_EQ(succs.size(), std::size_t{1});
        CHECK_EQ(succs.front(), i - 1);
    }
    CHECK_EQ(g.successors(0).size(), std::size_t{0});
    // Every node reaches node 0 → the whole chain is one dependency path.
    const bfs::TraversalReport r = bfs::explore(g, 49);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{50});
}

TEST(generators, tree_is_an_arborescence_rooted_at_node_zero)
{
    const int n = 200;
    graph::DependencyGraph g;
    bench::generateGraph(bench::Shape::Tree, n, 11, g);
    // Exactly one outgoing edge for every node except the root.
    CHECK_EQ(g.outDegree(0), std::size_t{0});
    for (int i = 1; i < n; ++i)
        CHECK_EQ(g.outDegree(i), std::size_t{1});
    // Every node's parent chain terminates at the root, so a REVERSE BFS
    // from node 0 (who depends on me) covers the whole tree. A forward BFS
    // from a leaf would only visit that leaf's subtree.
    const bfs::TraversalReport r = bfs::exploreReverse(g, 0);
    CHECK_EQ(r.counters.verticesVisited, std::size_t{n});
}

// ===========================================================================
// Phase 6 — the scanned fixture as a benchmark case
// ===========================================================================

TEST(fixture_case, scanned_project_endpoints_deterministic_and_valid)
{
    // Same upward walk as the integration tests (bench may run from a
    // build/<config> subdirectory).
    std::string fixtureRoot;
    {
        std::filesystem::path current = std::filesystem::current_path();
        for (int depth = 0; depth < 8 && fixtureRoot.empty(); ++depth)
        {
            const std::filesystem::path candidate =
                current / "tests/fixtures/sample_project";
            if (std::filesystem::is_directory(candidate))
                fixtureRoot = candidate.string();
            else if (current.has_parent_path())
                current = current.parent_path();
            else
                break;
        }
    }
    CHECK(!fixtureRoot.empty());

    const bench::CaseSetup setup = bench::buildFixtureCase(fixtureRoot);
    const graph::DependencyGraph& g = setup.data.graph;
    CHECK(g.isFinalized());
    CHECK_EQ(g.nodeCount(), 12);  // fixture totals asserted since Phase 3
    CHECK_EQ(g.edgeCount(), std::size_t{9});
    CHECK_EQ(setup.data.kind, std::string("fixture_sample_project"));
    CHECK_EQ(setup.data.seed, std::uint64_t{0}); // seed column 0 = scanned
    CHECK_EQ(setup.data.nodes, 12);
    CHECK(setup.data.center >= 0 && setup.data.center < 12);
    CHECK(setup.data.hub >= 0 && setup.data.hub < 12);
    CHECK(setup.data.pathFrom >= 0 && setup.data.pathFrom < 12);
    CHECK(setup.data.pathTo >= 0 && setup.data.pathTo < 12);
    CHECK(setup.pathExists); // B3 baselines agreed on reachability

    // The endpoint rules are pure functions of the graph → rebuilding the
    // case must pick identical endpoints (reproducibility contract).
    const bench::CaseSetup again = bench::buildFixtureCase(fixtureRoot);
    CHECK_EQ(again.data.center, setup.data.center);
    CHECK_EQ(again.data.hub, setup.data.hub);
    CHECK_EQ(again.data.pathFrom, setup.data.pathFrom);
    CHECK_EQ(again.data.pathTo, setup.data.pathTo);
}
