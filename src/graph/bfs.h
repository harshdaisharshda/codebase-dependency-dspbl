#pragma once
// src/graph/bfs.h
//
// Phase 5 — the six BFS variations (instructor addendum). As-built:
// Phase 5.1 shipped the shared core plus B1/B2/B4/B5; Phase 5.2 shipped B3
// (unbounded bidirectional path). B6 (direction-optimizing) is a separate
// engine, planned for Phase 5.3.
//
// House rules (identical to analysis.h): free functions over a FINALIZED
// graph::DependencyGraph; algorithms only READ the graph; results are
// caller-owned move-only values; asserts document preconditions (invalid
// seed ids / empty seed lists are programming errors, never runtime input).
//
// Edge convention (locked): A → B = "A includes B" = "A depends on B".
//   forward adjacency (successors)   = follow dependencies (B1/B2/B5)
//   reverse adjacency (predecessors) = follow dependents    (B4)
//
// Counter definitions (computed DURING traversal, never derived afterwards —
// they are features with exact unit-tested values, not byproducts):
//   verticesVisited — nodes whose level was assigned (distinct); equals the
//                     number of level[i] != -1 entries when the call returns.
//   edgesExamined   — every adjacency scan of one edge (B3/B6 add their own
//                     semantics later; the core counts plain scans).
//   maxFrontier     — largest |queue| observed, sampled on every enqueue.
//
// Determinism: seeds are processed in the order given; a seed already
// reached by an earlier seed keeps its earlier level (nearest-seed rule);
// adjacency lists keep insertion order — runs are fully reproducible.
//
// Approved rulings (ARCHITECTURE §9.19–25): B2 with one seed ≡ B1 (same
// core; asserted by tests); B4 ≡ analysis::impactOf levels (Phase 4 stays
// the untouched oracle); B5 boundHit = "the depth bound prevented at least
// one edge scan" — precise, testable, and false when every node sitting at
// the horizon is a leaf.

#include "ds/dyn_array.h"
#include "ds/linked_list.h"
#include "ds/queue.h"
#include "ds/stack.h"
#include "graph/dependency_graph.h"

#include <cassert>
#include <cstddef>
#include <limits>
#include <vector>

namespace bfs
{

// Depth budget meaning "no bound". Any value ≥ INT_MAX behaves identically
// (levels can never reach it), so callers may pass any huge number too.
inline constexpr std::size_t kUnboundedDepth =
    std::numeric_limits<std::size_t>::max();

struct Counters
{
    std::size_t verticesVisited = 0;
    std::size_t edgesExamined = 0;
    std::size_t maxFrontier = 0;
};

// Move-only (ds::DynArray is non-copyable): the caller owns the result, per
// AGENTS §5. level/parent are plain node-id tables exactly like analysis.h's
// internal ones; -1 fills use explicit loops (DynArray has no assign()).
struct TraversalReport
{
    ds::DynArray<int> level;
    ds::DynArray<int> parent;
    Counters counters;
    bool boundHit = false;  // B5 only: the bound cut off at least one edge scan
    int topDownLevels = 0;  // B6 only; always 0 in Phase 5.1
    int bottomUpLevels = 0; // B6 only; always 0 in Phase 5.1
};

// ---------------------------------------------------------------------------
// Shared core — ONE implementation behind B1/B2/B4/B5:
//   B1 = single seed, forward,   unbounded
//   B2 = many seeds,  forward,   unbounded (level = distance to NEAREST seed)
//   B4 = single seed, reverse,   unbounded
//   B5 = single seed, forward,   bounded
// A dequeued node whose level equals the depth limit is NOT expanded (the
// "look only N hops ahead" contract): it counts as visited, but its edges are
// never scanned. If such a node still has edges, the bound genuinely
// prevented work and boundHit records it.
// ---------------------------------------------------------------------------
inline TraversalReport runCore(const graph::DependencyGraph& graph,
                               const std::vector<int>& seeds,
                               const bool useReverseAdjacency,
                               const std::size_t maxDepth)
{
    assert(graph.isFinalized() && "bfs: graph must be finalized");
    assert(!seeds.empty() && "bfs: empty seed list is a programming error");

    TraversalReport report;
    const std::size_t n = static_cast<std::size_t>(graph.nodeCount());
    report.level = ds::DynArray<int>(n);
    report.parent = ds::DynArray<int>(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        report.level[i] = -1;
        report.parent[i] = -1;
    }

    // Depth budgets at/above INT_MAX can never bind (levels stay ≤ node
    // count), so clamping reduces the unbounded case to a plain int
    // comparison — no implementation-defined size_t→int casts anywhere.
    const int depthLimit =
        maxDepth >= static_cast<std::size_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(maxDepth);

    ds::Queue<int> frontier; // required Queue role: BFS working frontier
    for (const int seed : seeds)
    {
        assert(seed >= 0 && seed < graph.nodeCount() && "bfs: invalid seed id");
        if (report.level[seed] != -1)
            continue; // duplicate seed — or already reached by an earlier one
        report.level[seed] = 0;
        report.parent[seed] = seed;
        ++report.counters.verticesVisited;
        frontier.enqueue(seed);
        if (frontier.size() > report.counters.maxFrontier)
            report.counters.maxFrontier = frontier.size();
    }

    while (!frontier.isEmpty())
    {
        const int u = frontier.dequeue();
        if (report.level[u] >= depthLimit)
        {
            // At the horizon: visited but not expanded. boundHit only fires
            // when this skip genuinely prevented edge scans — a leaf at the
            // horizon changes nothing versus an unbounded run.
            const std::size_t pendingEdges =
                useReverseAdjacency ? graph.inDegree(u) : graph.outDegree(u);
            if (pendingEdges > 0)
                report.boundHit = true;
            continue;
        }

        const ds::LinkedList<int>& adjacency = useReverseAdjacency
                                                   ? graph.predecessors(u)
                                                   : graph.successors(u);
        for (const int v : adjacency)
        {
            ++report.counters.edgesExamined;
            if (report.level[v] != -1)
                continue; // already discovered (self-loop re-entry blocked too)
            report.level[v] = report.level[u] + 1;
            report.parent[v] = u;
            ++report.counters.verticesVisited;
            frontier.enqueue(v);
            if (frontier.size() > report.counters.maxFrontier)
                report.counters.maxFrontier = frontier.size();
        }
    }
    return report;
}

// B1 — single-source forward BFS: what does `seed` transitively depend on?
inline TraversalReport explore(const graph::DependencyGraph& graph, int seed)
{
    return runCore(graph, std::vector<int>{seed}, false, kUnboundedDepth);
}

// B2 — multi-source BFS: level = distance to the NEAREST seed. With exactly
// one seed this is provably identical to explore() (same core; the locked
// equivalence is asserted by tests, ruling §9.23).
inline TraversalReport exploreMultiSource(const graph::DependencyGraph& graph,
                                          const std::vector<int>& seeds)
{
    return runCore(graph, seeds, false, kUnboundedDepth);
}

// B4 — reverse BFS over PREDECESSORS: who transitively depends on `seed`?
// Same levels/counters contract as analysis::impactOf (the untouched Phase 4
// oracle); verified equal by tests rather than by shared code.
inline TraversalReport exploreReverse(const graph::DependencyGraph& graph,
                                      int seed)
{
    return runCore(graph, std::vector<int>{seed}, true, kUnboundedDepth);
}

// B5 — bounded-depth forward BFS ("look only N hops ahead"). maxDepth =
// kUnboundedDepth (or any value ≥ INT_MAX) makes this exactly explore().
inline TraversalReport exploreBounded(const graph::DependencyGraph& graph,
                                      int seed, const std::size_t maxDepth)
{
    return runCore(graph, std::vector<int>{seed}, false, maxDepth);
}

// ---------------------------------------------------------------------------
// B3 — unbounded bidirectional BFS (Phase 5.2). Two level-synchronized
// searches: forward from `from` over successors, reverse from `to` over
// predecessors. Each round expands ONE level of the SMALLER frontier
// (tie → forward, deterministic). A vertex discovered by one side that the
// other side already claimed is a meeting candidate; the round finishes its
// scan and the candidate with the smallest levelF + levelB sum wins. This is
// what keeps the result provably shortest even when one frontier races ahead
// of the other: BFS levels are exact distances, so every candidate's sum is
// a real path length, and the earliest second-discovery round always contains
// a vertex of a shortest path (proof sketch in ARCHITECTURE §5).
//
// No hop budget (ruling §9.22): the search runs until a meeting is found or
// a frontier is exhausted (one side can no longer discover anything → no
// path). from == to → found with the single-node path, counters zeroed (no
// traversal ran — mirrors F8's [from] ruling R4). Cycles are harmless:
// discovery guards stop re-entry. Counters: verticesVisited = distinct nodes
// claimed by either side; edgesExamined = scans from both frontiers;
// maxFrontier = max of |frontierF| + |frontierB| COMBINED, sampled after
// seeding and after every round swap.
// ---------------------------------------------------------------------------
struct PathReport
{
    bool found = false;
    std::vector<int> path; // [from, ..., to] in edge order; empty when not found
    Counters counters;
};

inline PathReport bidirectionalPath(const graph::DependencyGraph& graph,
                                    const int from, const int to)
{
    assert(graph.isFinalized() &&
           "bidirectionalPath: graph must be finalized");
    assert(from >= 0 && from < graph.nodeCount() &&
           "bidirectionalPath: invalid from id");
    assert(to >= 0 && to < graph.nodeCount() &&
           "bidirectionalPath: invalid to id");

    PathReport report;
    if (from == to)
    {
        report.found = true;
        report.path.push_back(from);
        return report;
    }

    const std::size_t n = static_cast<std::size_t>(graph.nodeCount());
    ds::DynArray<int> levelF(n), parentF(n), levelB(n), parentB(n);
    for (std::size_t i = 0; i < n; ++i)
    {
        levelF[i] = -1;
        levelB[i] = -1;
        parentF[i] = -1;
        parentB[i] = -1;
    }
    levelF[static_cast<std::size_t>(from)] = 0;
    parentF[static_cast<std::size_t>(from)] = from;
    levelB[static_cast<std::size_t>(to)] = 0;
    parentB[static_cast<std::size_t>(to)] = to;
    report.counters.verticesVisited = 2;

    ds::Queue<int> frontierF; // required Queue role: both BFS frontiers
    ds::Queue<int> frontierB;
    frontierF.enqueue(from);
    frontierB.enqueue(to);
    report.counters.maxFrontier = 2; // |F| + |B| at seed time

    std::vector<int> candidates; // meeting vertices discovered this round
    bool met = false;
    bool lastForward = false;
    while (!met && !frontierF.isEmpty() && !frontierB.isEmpty())
    {
        candidates.clear();
        // Smaller frontier expands; on a size tie the sides STRICTLY
        // ALTERNATE (lastForward flips). A constant "ties → forward" would
        // starve the reverse search whenever both frontiers stay the same
        // size — caught by the hub-meeting test.
        const bool expandForward =
            frontierF.size() != frontierB.size()
                ? frontierF.size() < frontierB.size()
                : !lastForward;
        lastForward = expandForward;
        ds::Queue<int>& frontier = expandForward ? frontierF : frontierB;
        ds::DynArray<int>& levelThis = expandForward ? levelF : levelB;
        ds::DynArray<int>& levelOther = expandForward ? levelB : levelF;
        ds::DynArray<int>& parentThis = expandForward ? parentF : parentB;

        ds::Queue<int> next;
        while (!frontier.isEmpty())
        {
            const int u = frontier.dequeue();
            const ds::LinkedList<int>& adjacency =
                expandForward ? graph.successors(u) : graph.predecessors(u);
            for (const int v : adjacency)
            {
                ++report.counters.edgesExamined;
                if (levelThis[static_cast<std::size_t>(v)] != -1)
                    continue; // this side already claimed v (self-loops too)
                levelThis[static_cast<std::size_t>(v)] =
                    levelThis[static_cast<std::size_t>(u)] + 1;
                parentThis[static_cast<std::size_t>(v)] = u;
                if (levelOther[static_cast<std::size_t>(v)] == -1)
                    ++report.counters.verticesVisited; // first side to see v
                else
                    candidates.push_back(v); // second discovery: meeting
                next.enqueue(v);
            }
        }
        if (expandForward)
            frontierF = std::move(next);
        else
            frontierB = std::move(next);

        const std::size_t combined = frontierF.size() + frontierB.size();
        if (combined > report.counters.maxFrontier)
            report.counters.maxFrontier = combined;

        if (candidates.empty())
            continue;

        // Smallest sum wins (tie → first in scan order = deterministic).
        int best = candidates.front();
        int bestSum = levelF[static_cast<std::size_t>(best)] +
                      levelB[static_cast<std::size_t>(best)];
        for (const int v : candidates)
        {
            const int sum = levelF[static_cast<std::size_t>(v)] +
                            levelB[static_cast<std::size_t>(v)];
            if (sum < bestSum)
            {
                bestSum = sum;
                best = v;
            }
        }

        // Forward half: from ... → best, walked onto the required Stack and
        // popped back in edge order (same reconstruction role as F2/F8).
        ds::Stack<int> prefix;
        int p = best;
        while (p != from)
        {
            prefix.push(p);
            p = parentF[static_cast<std::size_t>(p)];
        }
        report.path.push_back(from);
        while (!prefix.isEmpty())
            report.path.push_back(prefix.pop());

        // Reverse-search half: best → ... → to is ALREADY in edge order
        // (parentB[v] = u means the edge v → u exists).
        p = best;
        while (p != to)
        {
            p = parentB[static_cast<std::size_t>(p)];
            report.path.push_back(p);
        }
        report.found = true;
        met = true;
    }
    return report;
}

} // namespace bfs
