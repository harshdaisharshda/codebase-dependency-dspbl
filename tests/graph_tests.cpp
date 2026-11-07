// tests/graph_tests.cpp
// Phase 2 tests for the DependencyGraph model, following TESTING_STRATEGY.md
// §4.1 (graph model tests) and the Phase 2 exit criteria in
// DEVELOPMENT_PLAN.md: a known 7-node hand-authored graph round-trips exactly
// (counts, degrees, ids); duplicate/null/self edges behave per the approved
// spec; reverse adjacency is the exact edge-set mirror.
//
// Graphs here are built DIRECTLY by code (Layer 2 — no filesystem), exactly as
// the Phase 4-5 algorithm tests will consume them.

#include "graph/dependency_graph.h"

#include <string>

#include "test_framework.h"

using graph::DependencyGraph;

// Fixture builder: node "x" becomes path "src/x.cpp" with basename "x.cpp" so
// path != name — proves the two fields are stored independently.
// (Test infrastructure only — not a project data structure.)
static int addNamed(DependencyGraph& g, const std::string& key)
{
    return g.addNode("src/" + key + ".cpp", key + ".cpp");
}

// Counts edges accepted during a build (duplicates/self-rejections included)
// so stress tests can assert the counter matches reality exactly.
static std::size_t countedAddEdge(DependencyGraph& g, int from, int to)
{
    const bool accepted = g.addEdge(from, to);
    return accepted ? 1u : 0u;
}

TEST(graph, empty_graph)
{
    DependencyGraph g;
    CHECK_EQ(g.nodeCount(), 0);
    CHECK_EQ(g.edgeCount(), std::size_t{0});
    CHECK(g.isFinalized() == false);

    int id = DependencyGraph::kInvalidNodeId;
    CHECK(g.findId("src/a.cpp", id) == false);

    g.finalize(); // vacuous: 0 nodes, 0 edges
    CHECK(g.isFinalized());
    g.finalize(); // idempotent no-op
    CHECK(g.isFinalized());
}

TEST(graph, seven_node_hand_graph_round_trip)
{
    // Exit-criterion fixture (DEVELOPMENT_PLAN Phase 2). Shape:
    //
    //     0 a → 1 b     (diamond top: a depends on b and c)
    //     0 a → 2 c
    //     1 b → 3 d
    //     2 c → 3 d     (diamond bottom: d has in-degree 2)
    //     3 d → 4 e
    //     4 e → 5 f     (chain)
    //     6 g           (disconnected: no edges either way)
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");
    const int c = addNamed(g, "c");
    const int d = addNamed(g, "d");
    const int e = addNamed(g, "e");
    const int f = addNamed(g, "f");
    const int dis = addNamed(g, "dis");

    CHECK_EQ(a, 0); // ids are insertion-order stable
    CHECK_EQ(b, 1);
    CHECK_EQ(c, 2);
    CHECK_EQ(d, 3);
    CHECK_EQ(e, 4);
    CHECK_EQ(f, 5);
    CHECK_EQ(dis, 6);
    CHECK_EQ(g.nodeCount(), 7);

    CHECK_EQ(countedAddEdge(g, a, b), std::size_t{1});
    CHECK_EQ(countedAddEdge(g, a, c), std::size_t{1});
    CHECK_EQ(countedAddEdge(g, b, d), std::size_t{1});
    CHECK_EQ(countedAddEdge(g, c, d), std::size_t{1});
    CHECK_EQ(countedAddEdge(g, d, e), std::size_t{1});
    CHECK_EQ(countedAddEdge(g, e, f), std::size_t{1});
    CHECK_EQ(g.edgeCount(), std::size_t{6}); // hand-checked: 6 accepted edges

    // Node record round-trip: path and basename stored independently.
    CHECK_EQ(g.node(a).name, std::string("a.cpp"));
    CHECK_EQ(g.node(dis).unresolvedIncludes, std::size_t{0});

    // Hash-table lookup round-trips every node; unknown path rejected.
    for (int expect = 0; expect < 7; ++expect)
    {
        int id = DependencyGraph::kInvalidNodeId;
        CHECK(g.findId("src/" + (expect == 6 ? std::string("dis")
                                             : std::string(1, static_cast<char>('a' + expect))) +
                            ".cpp",
                        id));
        CHECK_EQ(id, expect);
    }
    int id = 999;
    CHECK(g.findId("src/zz.cpp", id) == false);
    CHECK_EQ(id, 999); // untouched on absence

    // Pre-finalize: forward adjacency already valid, in reverse-order states.
    CHECK(g.isFinalized() == false);
    CHECK_EQ(g.outDegree(a), std::size_t{2});
    CHECK_EQ(g.outDegree(f), std::size_t{0});

    g.finalize();
    CHECK(g.isFinalized());

    // Forward: insertion order preserved (determinism ruling #4).
    {
        const DependencyGraph& cg = g;
        const ds::LinkedList<int>& succA = cg.successors(a);
        CHECK_EQ(succA.at(0), 1);
        CHECK_EQ(succA.at(1), 2); // NOT 1 — reverse order would sort
    }

    // Reverse: exact mirror, also in edge order (c→d was added after b→d).
    const ds::LinkedList<int>& predD = g.predecessors(d);
    CHECK_EQ(predD.at(0), 1);
    CHECK_EQ(predD.at(1), 2);
    CHECK(g.predecessors(a).isEmpty());
    CHECK(g.predecessors(dis).isEmpty());

    // Degrees on the diamond shape, hand-computed:
    CHECK_EQ(g.outDegree(a), std::size_t{2});
    CHECK_EQ(g.outDegree(b), std::size_t{1});
    CHECK_EQ(g.outDegree(c), std::size_t{1});
    CHECK_EQ(g.outDegree(d), std::size_t{1});
    CHECK_EQ(g.outDegree(e), std::size_t{1});
    CHECK_EQ(g.outDegree(f), std::size_t{0});
    CHECK_EQ(g.outDegree(dis), std::size_t{0});
    CHECK_EQ(g.inDegree(a), std::size_t{0});
    CHECK_EQ(g.inDegree(b), std::size_t{1});
    CHECK_EQ(g.inDegree(c), std::size_t{1});
    CHECK_EQ(g.inDegree(d), std::size_t{2});
    CHECK_EQ(g.inDegree(e), std::size_t{1});
    CHECK_EQ(g.inDegree(f), std::size_t{1});
    CHECK_EQ(g.inDegree(dis), std::size_t{0});

    // Global consistency: Σin = Σout = E, and every edge u→v has u mirrored in
    // predecessors(v) (TESTING_STRATEGY §4.1 mirror property).
    std::size_t sumIn = 0;
    std::size_t sumOut = 0;
    for (int u = 0; u < g.nodeCount(); ++u)
    {
        sumOut += g.outDegree(u);
        sumIn += g.inDegree(u);
        for (const int v : g.successors(u))
        {
            bool mirrored = false;
            for (const int back : g.predecessors(v))
                if (back == u)
                {
                    mirrored = true;
                    break;
                }
            CHECK(mirrored);
        }
    }
    CHECK_EQ(sumIn, g.edgeCount());
    CHECK_EQ(sumOut, g.edgeCount());
}

TEST(graph, add_node_dedup_keeps_stable_ids)
{
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");
    const int c = addNamed(g, "c");
    CHECK_EQ(g.nodeCount(), 3);

    CHECK_EQ(g.addNode("src/a.cpp", "a.cpp"), a); // duplicate: same id
    CHECK_EQ(g.addNode("src/a.cpp", "whatever.cpp"), a); // name ignored on dedup
    CHECK_EQ(g.nodeCount(), 3); // nothing appended

    const int fresh = addNamed(g, "d"); // later insertion keeps old ids valid
    CHECK_EQ(fresh, 3);
    CHECK_EQ(g.nodeCount(), 4);
    CHECK_EQ(g.node(a).name, std::string("a.cpp")); // first name retained
    CHECK_EQ(g.node(b).name, std::string("b.cpp"));
    CHECK_EQ(g.node(c).name, std::string("c.cpp"));

    // Duplicate nodes never carried edges: a's adjacency untouched.
    CHECK_EQ(g.outDegree(a), std::size_t{0});
    CHECK_EQ(g.edgeCount(), std::size_t{0});
}

TEST(graph, duplicate_edge_rejected)
{
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");

    CHECK_EQ(g.addEdge(a, b), true);
    CHECK_EQ(g.addEdge(a, b), false); // duplicate → no-op, per spec
    CHECK_EQ(g.successors(a).size(), std::size_t{1});
    CHECK_EQ(g.edgeCount(), std::size_t{1});

    // Duplicate self-edge behaves like any other duplicate.
    CHECK_EQ(g.addEdge(b, b), true);
    CHECK_EQ(g.addEdge(b, b), false);
    CHECK_EQ(g.successors(b).size(), std::size_t{1});
    CHECK_EQ(g.edgeCount(), std::size_t{2});
}

TEST(graph, self_edge_recorded)
{
    // B1 ruling: a self-include is a real (length-1) dependency cycle that
    // must survive into cycle detection / topological ordering phases.
    DependencyGraph g;
    const int a = addNamed(g, "a");
    CHECK_EQ(g.addEdge(a, a), true);
    CHECK_EQ(g.edgeCount(), std::size_t{1});

    g.finalize();
    CHECK_EQ(g.outDegree(a), std::size_t{1});
    CHECK_EQ(g.inDegree(a), std::size_t{1});
    const ds::LinkedList<int>& succ = g.successors(a);
    CHECK_EQ(succ.size(), std::size_t{1});
    CHECK_EQ(succ.front(), a); // the node is its own successor...
    CHECK_EQ(g.predecessors(a).front(), a); // ...and its own predecessor
}

TEST(graph, out_of_range_edge_rejected_without_mutation)
{
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");

    // Policy (AGENTS.md §6): report-and-skip, never assert/throw — an
    // invalid caller id is a recoverable caller bug, so it returns false.
    CHECK_EQ(g.addEdge(DependencyGraph::kInvalidNodeId, b), false);
    CHECK_EQ(g.addEdge(a, DependencyGraph::kInvalidNodeId), false);
    CHECK_EQ(g.addEdge(g.nodeCount(), b), false); // one past the last id
    CHECK_EQ(g.addEdge(b, g.nodeCount()), false);
    CHECK_EQ(g.edgeCount(), std::size_t{0});
    CHECK_EQ(g.successors(a).size(), std::size_t{0});
    CHECK_EQ(g.successors(b).size(), std::size_t{0});

    // A valid edge after rejected attempts still works normally.
    CHECK_EQ(g.addEdge(a, b), true);
    CHECK_EQ(g.edgeCount(), std::size_t{1});
}

TEST(graph, finalize_contract_invalidation)
{
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");

    g.finalize();
    CHECK(g.isFinalized());

    // Structural mutation #1: a new edge invalidates the finalized state.
    CHECK_EQ(g.addEdge(a, b), true);
    CHECK_EQ(g.isFinalized(), false);

    g.finalize();
    CHECK(g.isFinalized());
    CHECK_EQ(g.inDegree(b), std::size_t{1}); // reverse now sees the edge

    // Structural mutation #2: a new node invalidates it as well.
    const int c = addNamed(g, "c");
    CHECK_EQ(c, 2);
    CHECK_EQ(g.isFinalized(), false);

    g.finalize();
    CHECK(g.isFinalized());
    CHECK(g.predecessors(c).isEmpty()); // fresh node has no dependents
    CHECK_EQ(g.inDegree(c), std::size_t{0});

    // Idempotent double finalize changes nothing observable.
    g.finalize();
    CHECK(g.isFinalized());
    CHECK_EQ(g.edgeCount(), std::size_t{1});
    CHECK_EQ(g.nodeCount(), 3);
}

TEST(graph, unresolved_include_counter)
{
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");

    g.addUnresolvedInclude(a);
    g.addUnresolvedInclude(a);
    CHECK_EQ(g.node(a).unresolvedIncludes, std::size_t{2});
    CHECK_EQ(g.node(b).unresolvedIncludes, std::size_t{0});

    // Unresolved counts do not affect structure: no edge, no invalidation.
    CHECK_EQ(g.edgeCount(), std::size_t{0});
    CHECK_EQ(g.isFinalized(), false); // was never finalized in the first place
    g.finalize();
    CHECK(g.isFinalized());
    CHECK_EQ(g.successors(a).size(), std::size_t{0});
    CHECK_EQ(g.inDegree(a), std::size_t{0});
}

TEST(graph, clear_resets_everything)
{
    DependencyGraph g;
    const int a = addNamed(g, "a");
    const int b = addNamed(g, "b");
    CHECK_EQ(g.addEdge(a, b), true);
    g.finalize();
    CHECK(g.isFinalized());

    g.clear();
    CHECK_EQ(g.nodeCount(), 0);
    CHECK_EQ(g.edgeCount(), std::size_t{0});
    CHECK_EQ(g.isFinalized(), false);
    int id = 123;
    CHECK(g.findId("src/a.cpp", id) == false); // lookup table emptied
    CHECK_EQ(id, 123);                         // untouched on absence

    // Re-scan style reuse: ids restart from 0 (clear() ruling #2).
    const int again = addNamed(g, "x");
    CHECK_EQ(again, 0);
    CHECK_EQ(g.node(again).path, std::string("src/x.cpp"));
    CHECK_EQ(g.node(again).name, std::string("x.cpp"));
    CHECK_EQ(g.node(again).unresolvedIncludes, std::size_t{0});
}

TEST(graph, stress_2000_nodes_5000_edges)
{
    // Scalability sanity check, NOT a benchmark (Phase 2 ruling #5): asserts
    // exact counting and the reverse-mirror invariant on a graph at the top
    // of the planned benchmark size range. Timing is never measured here.
    DependencyGraph g;
    const int NODES = 2000;
    std::size_t accepted = 0;

    for (int i = 0; i < NODES; ++i)
    {
        const std::string name = "node_" + std::to_string(i) + ".cpp";
        g.addNode("src/" + name, name);
    }
    CHECK_EQ(g.nodeCount(), NODES);

    // 1999 chain edges (i → i+1) + a deterministic chord every 3rd node.
    for (int i = 0; i + 1 < NODES; ++i)
        accepted += countedAddEdge(g, i, i + 1);
    for (int i = 0; i < NODES; i += 3)
        accepted += countedAddEdge(g, i, (i * 7 + 11) % NODES);

    // A deterministic sprinkling of rejections: duplicates + out-of-range.
    CHECK_EQ(g.addEdge(0, 1), false);          // chain edge, already there
    CHECK_EQ(g.addEdge(10, 10), true);         // one self-edge is recorded
    accepted += 1;
    CHECK_EQ(g.addEdge(10, 10), false);        // ...but not twice
    CHECK_EQ(g.addEdge(NODES, 0), false);      // out of range
    CHECK_EQ(g.addEdge(0, -1), false);         // out of range

    CHECK_EQ(g.edgeCount(), accepted); // counter == reality, exactly

    g.finalize();
    std::size_t sumIn = 0;
    std::size_t sumOut = 0;
    for (int u = 0; u < g.nodeCount(); ++u)
    {
        sumOut += g.outDegree(u);
        sumIn += g.inDegree(u);
        for (const int v : g.successors(u))
        {
            // Mirror property on every single edge (O(E) total small walks).
            bool mirrored = false;
            for (const int back : g.predecessors(v))
                if (back == u)
                {
                    mirrored = true;
                    break;
                }
            CHECK(mirrored);
        }
    }
    CHECK_EQ(sumIn, accepted);
    CHECK_EQ(sumOut, accepted);

    // Hand-checkable spot values on the chain part.
    CHECK(g.inDegree(5) >= 1); // 4→5 chain edge
    CHECK_EQ(g.predecessors(0).isEmpty(), true);
    CHECK_EQ(g.inDegree(NODES - 1), std::size_t{1}); // only the chain edge in
}
