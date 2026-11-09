#pragma once
// src/graph/analysis.h
//
// Core analytics engine (Phase 4, features F2–F8). Free functions operating
// on a FINALIZED graph::DependencyGraph — algorithms only READ the graph;
// every result (path/level/order tables, chain sequences) is a plain array
// owned by the caller (AGENTS.md §5). Graph storage and analytics stay in
// separate layers; the menu layer (Phase 7) will map user files to ids.
//
// Edge convention (locked): A → B means "A includes B" = "A depends on B".
//   forward adjacency  (successors)   = "what does this file depend on"
//   reverse adjacency  (predecessors) = "who depends on this file"
//
// Required-structure roles (approved synopsis mapping — each load-bearing):
//   ds::Queue    — BFS frontier (F3 impact, F8 path)
//   ds::Stack    — path/cycle reconstruction; results are popped into edge
//                  order for F2's cycle paths and F8's dependency path
//   ds::MaxHeap  — F6 in-degree ranking via buildFrom (O(V) heapify: all V
//                  in-degrees are known at once, see Phase 1.6)
//   ds::Trie     — F7 basename index (caller-owned, no graph coupling)
//   ds::DynArray — color/level/parent/remaining/memo tables
//
// Approved Phase 4 rulings (2026-11-09):
//   R1  no Tarjan/SCC: F2 reports cycle *instances* (capped storage, UNcapped
//       detection) instead of SCC groups.
//   R2  recursive DFS with a maxDepth guard, default 10 000; a tripped guard
//       degrades gracefully ("graph too deep for recursive analysis") — it
//       NEVER crashes and NEVER publishes partially computed results.
//   R3  impact level 0 = the changed file itself (self); dependents start at 1.
//   R4  F8 from == to → found with the single-node path [from].
//   R5  F7 empty prefix → empty result (documented Trie behavior).
//
// Determinism: every traversal seeds/visits in node-id order and adjacency
// list order is preserved by the graph, so all outputs are reproducible.

#include "ds/dyn_array.h"
#include "ds/max_heap.h"
#include "ds/queue.h"
#include "ds/stack.h"
#include "ds/trie.h"
#include "graph/dependency_graph.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace analysis
{

inline constexpr std::size_t kDefaultMaxDepth = 10000;
inline constexpr std::size_t kDefaultCycleReportCap = 16;

// ===========================================================================
// F2 — Circular dependency detection (recursive 3-color DFS + Stack)
// ===========================================================================
//
// state: 0 = white (unvisited), 1 = gray (on the recursion stack), 2 = black
// (fully explored). An edge to a GRAY node closes a cycle: the node is on the
// DFS recursion stack, so the synopsis' wording applies verbatim.
//
// depthGuardTripped is a "results partial" marker:
//   * it does NOT stop the DFS from having counted cycles found so far;
//   * it means the DFS cannot claim the graph is (or is not) fully explored.
//     Callers must not rely on absence of cycles when it is set.
// The report cap limits only stored instances; every back edge is counted in
// cyclesFound regardless (the DFS keeps walking), per review ruling #1.
struct CycleReport
{
    bool hasCycles = false;
    bool depthGuardTripped = false;
    std::size_t cyclesFound = 0; // total back edges detected (uncapped)
    std::vector<std::vector<int>> cycleInstances; // ≤ cap, closed paths
};

// Shared per-run state for one findCycles() call.
struct CycleDfsContext
{
    const graph::DependencyGraph& graph;
    std::size_t maxDepth;
    std::size_t reportCap;
    ds::DynArray<int> state;
    ds::DynArray<int> parent; // DFS-tree parent; reconstructs cycle paths
    CycleReport& report;
};

// Builds the closed path [v, ..., u, v] for the back edge u → v (v is v's own
// first AND last node, per review ruling). The DFS-tree parent chain is
// walked onto the required ds::Stack and popped back in edge order.
static std::vector<int> closedCyclePath(CycleDfsContext& ctx, int v, int u)
{
    std::vector<int> cycle;
    if (u == v)
    {
        cycle.push_back(v); // self-loop: [x, x] — still a closed path
        cycle.push_back(v);
        return cycle;
    }
    ds::Stack<int> chain; // required Stack role: reconstruction
    int p = u;
    while (p != v)
    {
        chain.push(p);
        p = ctx.parent[p]; // v IS a DFS-tree ancestor of u: always terminates
    }
    // chain (bottom→top) holds [u, parent(u), ..., child-of-v]; its TOP is the
    // child of v. Popping therefore yields the walk v → child-of-v → ... → u
    // in correct edge direction: list v, then the pops, then v to close it.
    cycle.push_back(v);
    while (!chain.isEmpty())
        cycle.push_back(chain.pop());
    cycle.push_back(v);
    return cycle;
}

// Returns when the subtree rooted at u is finished (or the guard trips).
static void cycleDfsVisit(CycleDfsContext& ctx, int u, int depth)
{
    if (depth > static_cast<int>(ctx.maxDepth))
    {
        ctx.report.depthGuardTripped = true;
        return; // graceful "too deep" — that subtree stays unexplored (flagged)
    }
    ctx.state[u] = 1; // gray: on the recursion stack (synopsis wording)
    for (const int v : ctx.graph.successors(u))
    {
        if (ctx.report.depthGuardTripped)
            break; // stop descending once the guard trips
        if (ctx.state[v] == 0)
        {
            ctx.parent[v] = u;
            cycleDfsVisit(ctx, v, depth + 1);
        }
        else if (ctx.state[v] == 1)
        {
            // back edge u → v: the synopsis' "edge going back to a file
            // already on the stack" = a dependency cycle.
            ++ctx.report.cyclesFound;
            ctx.report.hasCycles = true;
            if (ctx.report.cycleInstances.size() < ctx.reportCap)
                ctx.report.cycleInstances.push_back(closedCyclePath(ctx, v, u));
        }
    }
    ctx.state[u] = 2; // black: fully explored from every angle available
}

inline CycleReport findCycles(const graph::DependencyGraph& graph,
                              std::size_t maxReportedInstances = kDefaultCycleReportCap,
                              std::size_t maxDepth = kDefaultMaxDepth)
{
    assert(graph.isFinalized() && "findCycles: graph must be finalized");
    CycleReport report;
    CycleDfsContext ctx{graph, maxDepth, maxReportedInstances,
                        ds::DynArray<int>(graph.nodeCount()),
                        ds::DynArray<int>(graph.nodeCount()), report};
    for (int id = 0; id < graph.nodeCount(); ++id)
    {
        ctx.parent[id] = -1;
        // state value-initialised to 0 = white
    }
    for (int id = 0; id < graph.nodeCount(); ++id)
        if (ctx.state[id] == 0) // every node once: disconnected components too
            cycleDfsVisit(ctx, id, 1);
    return report;
}

// ===========================================================================
// F3 — Change impact analysis (BFS over reverse edges)
// ===========================================================================
//
// level[node] -1 = unaffected, 0 = the changed file itself (ruling R3),
// 1..k = number of include steps between the file and the changed file.
// affectedCount counts everything with level >= 0 (the file itself included).
struct ImpactReport
{
    std::vector<int> level;       // per node id
    std::size_t affectedCount = 0;
};

inline ImpactReport impactOf(const graph::DependencyGraph& graph, int changedId)
{
    assert(graph.isFinalized() && "impactOf: graph must be finalized");
    assert(changedId >= 0 && changedId < graph.nodeCount() &&
           "impactOf: invalid node id");
    ImpactReport report;
    report.level.assign(graph.nodeCount(), -1);
    if (graph.nodeCount() == 0)
        return report; // vacuous (defensive; assert above catches misuse)

    ds::Queue<int> frontier; // required Queue role: BFS / B4 correspondence
    report.level[changedId] = 0;
    frontier.enqueue(changedId);
    while (!frontier.isEmpty())
    {
        const int u = frontier.dequeue();
        for (const int dependent : graph.predecessors(u)) // REVERSE adjacency
            if (report.level[dependent] == -1)
            {
                report.level[dependent] = report.level[u] + 1;
                frontier.enqueue(dependent);
            }
    }
    for (const int level : report.level)
        if (level >= 0)
            ++report.affectedCount;
    return report;
}

// ===========================================================================
// F4 — Topological compilation order (Kahn with the required Queue)
// ===========================================================================
//
// Orientation: dependencies compile FIRST. Kahn therefore runs on the
// dependency direction: a node is ready when all its SUCCESSORS (its own
// dependencies) are already emitted. Seeds are all out-degree-0 nodes in id
// order — deterministic.
//
// cycleBlocked = Kahn leftovers. Review ruling #2: these are the files that
// can NEVER compile in the current graph — which includes files ON a cycle
// and files DEPENDING (transitively) on a cycle member. It is a
// "blocked" set, not strictly "cycle membership" (use F2 for instances).
struct TopoReport
{
    std::vector<int> ordered;      // dependencies-first compile order
    std::vector<int> cycleBlocked; // unorderable leftovers, ascending id order
};

inline TopoReport topologicalOrder(const graph::DependencyGraph& graph)
{
    assert(graph.isFinalized() && "topologicalOrder: graph must be finalized");
    TopoReport report;
    const int n = graph.nodeCount();
    if (n == 0)
        return report;

    ds::DynArray<int> remaining(n); // unprocessed dependencies per node...
    // ...which is exactly "how many of my dependencies still need to be
    // emitted" — seeded from the forward out-degree.
    ds::Queue<int> ready;
    for (int id = 0; id < n; ++id)
    {
        remaining[id] = static_cast<int>(graph.outDegree(id));
        if (remaining[id] == 0)
            ready.enqueue(id);
    }
    while (!ready.isEmpty())
    {
        const int u = ready.dequeue();
        report.ordered.push_back(u);
        for (const int dependent : graph.predecessors(u))
        {
            --remaining[dependent]; // u is now done: one dependency resolved
            if (remaining[dependent] == 0)
                ready.enqueue(dependent);
        }
    }
    for (int id = 0; id < n; ++id)
        if (remaining[id] > 0) // never reached zero: permanently blocked
            report.cycleBlocked.push_back(id);
    return report;
}

// ===========================================================================
// F5 — Deepest dependency chain (memoized recursive DFS on DAGs only)
// ===========================================================================
//
// Locked policy (ARCHITECTURE §9): the longest dependency chain is DEFINED
// only for acyclic graphs. With cycles → undefined (this layer never
// presents an SCC-condensation as a file-level chain); a tripped guard
// means "incomplete" — reviewed ruling #3: no partial chain is returned.
// chain is [startFile, ..., deepestFile] in dependency-following order.
struct ChainReport
{
    bool defined = false;
    bool depthGuardTripped = false;
    std::vector<int> chain;     // valid only when defined
    std::vector<int> firstCycle; // non-empty when a cycle made it undefined
};

struct ChainDfsContext
{
    const graph::DependencyGraph& graph;
    std::size_t maxDepth;
    ds::DynArray<int>& memo; // longest chain of NODES from u; -1 = uncomputed
    ds::DynArray<int>& next; // chosen first hop on that chain; -1 = leaf
    bool guardTripped = false;
};

// Longest chain of dependency nodes starting at u (u included). Returns the
// node count, or -1 only after/with guardTripped.
static int chainDepthFrom(ChainDfsContext& ctx, int u, int depth)
{
    if (depth > static_cast<int>(ctx.maxDepth))
    {
        ctx.guardTripped = true;
        return -1;
    }
    if (ctx.memo[u] != -1)
        return ctx.memo[u]; // fully computed subtree: memory, not state guess

    int best = 1;       // the node alone is always a valid chain
    int bestHop = -1;
    for (const int v : ctx.graph.successors(u))
    {
        if (ctx.guardTripped)
            return -1; // no memo writes after a guard trip (ruling #3)
        const int sub = chainDepthFrom(ctx, v, depth + 1);
        if (ctx.guardTripped)
            return -1;
        if (sub + 1 > best) // strict: first-seen (deterministic) wins ties
        {
            best = sub + 1;
            bestHop = v;
        }
    }
    ctx.memo[u] = best;
    ctx.next[u] = bestHop;
    return best;
}

inline ChainReport longestChain(const graph::DependencyGraph& graph,
                                std::size_t maxDepth = kDefaultMaxDepth)
{
    assert(graph.isFinalized() && "longestChain: graph must be finalized");
    ChainReport report;
    const int n = graph.nodeCount();
    if (n == 0)
    {
        report.defined = true; // an empty graph trivially has a (empty) chain
        return report;
    }

    // Step 1 — the locked policy check: any cycle makes the answer undefined.
    const CycleReport cycles =
        findCycles(graph, kDefaultCycleReportCap, maxDepth);
    if (cycles.hasCycles)
    {
        if (!cycles.cycleInstances.empty())
            report.firstCycle = cycles.cycleInstances.front();
        return report; // defined stays false; no misleading chain published
    }
    if (cycles.depthGuardTripped)
    {
        // Guard tripped during the cycle check: we cannot rule cycles out,
        // so the DAG-only computation must not run. Partial-undefined, R3.
        report.depthGuardTripped = true;
        return report;
    }

    // Step 2 — genuine DAG: memoized DFS. Each node computed exactly once.
    ds::DynArray<int> memo(n);
    ds::DynArray<int> next(n);
    for (int id = 0; id < n; ++id)
    {
        memo[id] = -1;
        next[id] = -1;
    }
    ChainDfsContext ctx{graph, maxDepth, memo, next, false};

    int bestStart = -1;
    int bestLength = 0;
    for (int u = 0; u < n; ++u) // ascending ids → smaller starter wins ties
    {
        const int length = chainDepthFrom(ctx, u, 1);
        if (ctx.guardTripped)
        {
            report.depthGuardTripped = true; // incomplete: publish nothing
            return report;
        }
        if (length > bestLength) // strict: smaller id kept on ties
        {
            bestLength = length;
            bestStart = u;
        }
    }
    // (bestStart is always a real node here: every chain length ≥ 1.)
    report.defined = true;
    int p = bestStart;
    while (p != -1)
    {
        report.chain.push_back(p);
        p = ctx.next[p];
    }
    return report;
}

// ===========================================================================
// F6 — Highly connected modules (MaxHeap over in-degrees)
// ===========================================================================
//
// Ranks files by in-degree (= how many files depend on them). The heap is
// built with O(V) buildFrom heapification (the Phase 1.6 justification:
// every score is known before any extraction). Ties → smaller fileId →
// (via the graph's sorted-path id assignment) lexicographic path order.
inline std::vector<int> mostDependedUpon(const graph::DependencyGraph& graph,
                                         std::size_t k)
{
    assert(graph.isFinalized() && "mostDependedUpon: graph must be finalized");
    const std::size_t n = graph.nodeCount();
    if (n == 0 || k == 0)
        return {};

    ds::DynArray<ds::MaxHeap::Entry> entries(n);
    for (int id = 0; id < graph.nodeCount(); ++id)
        entries[id] = ds::MaxHeap::Entry{static_cast<int>(graph.inDegree(id)), id};
    ds::MaxHeap heap;
    heap.buildFrom(entries); // O(V) bottom-up; NOT repeated inserts

    std::vector<int> top;
    top.reserve(k < n ? k : n);
    while (top.size() < k && !heap.isEmpty())
        top.push_back(heap.extractMax().fileId);
    return top;
}

// ===========================================================================
// F7 — Filename prefix search (Trie over basenames)
// ===========================================================================
//
// buildNameIndex returns a CALLER-OWNED Trie and keeps NO reference to the
// graph (reviewed ruling #4): key = basename (so duplicate basenames share a
// terminal but keep their distinct ids), terminal = all matching file ids.
// Search results map ids back to paths via the graph by the caller/menu.
inline ds::Trie buildNameIndex(const graph::DependencyGraph& graph)
{
    assert(graph.isFinalized() && "buildNameIndex: graph must be finalized");
    ds::Trie index;
    for (int id = 0; id < graph.nodeCount(); ++id)
        index.insert(graph.node(id).name, id);
    return index; // move-constructed on return; independent of `graph`
}

// Prefix search over basenames; ids returned ascending (deterministic, and
// therefore lexicographic-path order). Empty prefix → empty (ruling R5).
inline std::vector<int> filesWithPrefix(const graph::DependencyGraph& graph,
                                        const ds::Trie& nameIndex,
                                        const std::string& prefix)
{
    assert(graph.isFinalized() && "filesWithPrefix: graph must be finalized");
    (void)graph; // assertion context only; Release builds drop asserts
    std::vector<int> files;
    if (prefix.empty())
        return files;
    const ds::LinkedList<int> hits = nameIndex.searchPrefix(prefix);
    for (const int id : hits)
        files.push_back(id);
    std::sort(files.begin(), files.end());
    return files;
}

// "Does a file with this basename exist?" — the synopsis' existence check.
inline bool fileExistsByBasename(const ds::Trie& nameIndex,
                                 const std::string& name)
{
    return nameIndex.containsExact(name);
}

// ===========================================================================
// F8 — Dependency path between two files (classic BFS; Phase 5's B3
//      bidirectional variant benchmarks against exactly this fallback)
// ===========================================================================
//
// Searches FORWARD adjacency: from → ... → to ("does A depend on B transit-
// ively?"). Shortest hop count is guaranteed by BFS; the parent chain is
// reconstructed through ds::Stack so the path prints in edge order.
// from == to → found, path [from] (single entry, reviewed ruling R4).
struct PathResult
{
    bool found = false;
    std::vector<int> path; // [from, ..., to]; empty when not found
};

inline PathResult dependencyPath(const graph::DependencyGraph& graph,
                                 int from, int to)
{
    assert(graph.isFinalized() && "dependencyPath: graph must be finalized");
    assert(from >= 0 && from < graph.nodeCount() && "dependencyPath: bad from");
    assert(to >= 0 && to < graph.nodeCount() && "dependencyPath: bad to");
    PathResult result;
    if (from == to)
    {
        result.found = true;
        result.path.push_back(from);
        return result;
    }

    ds::DynArray<int> parent(graph.nodeCount()); // -1 undiscovered
    for (int id = 0; id < graph.nodeCount(); ++id)
        parent[id] = -1;
    parent[from] = from; // marked discovered; blocks any self-loop re-entry

    ds::Queue<int> frontier;
    frontier.enqueue(from);
    while (!result.found && !frontier.isEmpty())
    {
        const int u = frontier.dequeue();
        for (const int v : graph.successors(u))
        {
            if (parent[v] != -1)
                continue;
            parent[v] = u;
            if (v == to)
            {
                result.found = true;
                break;
            }
            frontier.enqueue(v);
        }
    }

    if (result.found)
    {
        // Backward walk → Stack → forward order (required Stack role).
        ds::Stack<int> backward;
        int p = to;
        while (p != from)
        {
            backward.push(p);
            p = parent[p];
        }
        result.path.push_back(from);
        while (!backward.isEmpty())
            result.path.push_back(backward.pop());
    }
    return result;
}

} // namespace analysis
