#pragma once
// src/app/menu_actions.h
//
// Phase 7 — the nine menu option handlers (free functions over
// AppState + istream/ostream; main.cpp owns the loop).
//
// Rules honored here:
//  - NO algorithm is duplicated: option 3 calls bfs::explore (B1), option 4
//    calls analysis::impactOf (F3), option 5 calls bfs::bidirectionalPath
//    (B3), option 6 calls the F7 Trie path (buildNameIndex /
//    filesWithPrefix / fileExistsByBasename), option 7 calls the F2/F4/F5/F6
//    analytics, option 9 calls the Phase 5/6 benchmark runner untouched.
//  - option 8 operates on the REAL src/ds structures; internal-state views
//    use the existing diagnostic APIs (MaxHeap::rawEntryAt,
//    HashTable::hashFor/bucketCount/loadFactor, DynArray::capacity) plus the
//    one Phase 7 read-only walk added to Trie (visitNodes) for drawing
//    branches — the same diagnostic-view precedent as rawEntryAt.
//  - tolerant input everywhere: garbage re-prompts/cancels, never crashes;
//    EOF cancels the current flow and the main loop exits cleanly.

#include "app/app_context.h"
#include "bench/benchmark_runner.h" // runBenchmarkSuite + locateFixtureRoot
#include "graph/analysis.h"
#include "graph/bfs.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace app
{

// --------------------------------------------------------------------------
// Shared helpers
// --------------------------------------------------------------------------

inline void printDivider(std::ostream& out)
{
    out << "--------------------------------------------------\n";
}

inline std::string nodePath(const AppState& state, const int id)
{
    return state.graph.node(id).path;
}

inline bool graphNotReady(std::ostream& out, const AppState& state)
{
    if (state.graphReady())
        return false;
    out << "  No project scanned yet (or the scan found no files) — "
           "use option 1 first.\n";
    return true;
}

// File picker: exact project-relative path first, then basename prefix via
// the production Trie index (F7). Returns a node id, or -1 on cancel /
// no match / EOF. `prompt` explains whose file is being picked.
inline int pickFile(const AppState& state, std::istream& in,
                    std::ostream& out, const char* prompt)
{
    out << "  " << prompt
        << " (path, or basename prefix; empty line = cancel): ";
    std::string line;
    if (!std::getline(in, line) || line.empty())
        return -1;

    int id = -1;
    if (state.graph.findId(line, id))
        return id;

    const ds::Trie nameIndex = analysis::buildNameIndex(state.graph);
    const std::vector<int> matches =
        analysis::filesWithPrefix(state.graph, nameIndex, line);
    if (matches.empty())
    {
        out << "  No file matches \"" << line << "\".\n";
        return -1;
    }
    if (matches.size() == 1)
        return matches.front();

    out << "  " << matches.size() << " files match \"" << line << "\":\n";
    for (std::size_t i = 0; i < matches.size(); ++i)
        out << "   " << (i + 1) << ". " << nodePath(state, matches[i])
            << "\n";
    while (true)
    {
        out << "  Select 1-" << matches.size()
            << " (empty line = cancel): ";
        if (!std::getline(in, line) || line.empty())
            return -1;
        int choice = 0;
        if (parseMenuChoice(line, choice) && choice >= 1 &&
            static_cast<std::size_t>(choice) <= matches.size())
            return matches[static_cast<std::size_t>(choice - 1)];
        out << "  Invalid selection.\n";
    }
}

// Groups ids by BFS level and prints one line per level (id order within
// a level keeps output deterministic). `levelOf(id)` maps id -> level.
template <typename LevelFn>
void printLevelGroups(std::ostream& out, const AppState& state,
                      const int nodeCount, LevelFn levelOf,
                      const char* zeroLabel)
{
    int maxLevel = -1;
    for (int id = 0; id < nodeCount; ++id)
        if (levelOf(id) > maxLevel)
            maxLevel = levelOf(id);
    for (int k = 0; k <= maxLevel; ++k)
    {
        out << (k == 0 ? zeroLabel : "  files at level")
            << (k == 0 ? "" : " " + std::to_string(k) + ":");
        int shown = 0;
        for (int id = 0; id < nodeCount; ++id)
            if (levelOf(id) == k)
            {
                out << (shown++ == 0 ? " " : ", ") << nodePath(state, id);
            }
        out << "\n";
    }
}

// --------------------------------------------------------------------------
// Option 1 — Scan a Project
// --------------------------------------------------------------------------

inline void scanProjectMenu(AppState& state, std::istream& in,
                            std::ostream& out)
{
    out << "  Project directory (empty line = bundled sample fixture): ";
    std::string path;
    if (!std::getline(in, path))
        return;
    if (path.empty())
    {
        const std::string fixtureRoot = bench::locateFixtureRoot();
        if (fixtureRoot.empty())
        {
            out << "  Sample fixture not found — enter a directory.\n";
            return;
        }
        path = fixtureRoot;
        out << "  Using: " << path << "\n";
    }

    // scanProject requires an EMPTY graph (asserted), so scan into fresh
    // locals and only replace the live state on success — a failed scan
    // keeps the previous graph (report-then-keep, AGENTS §6).
    graph::DependencyGraph freshGraph;
    io::ScanSummary freshSummary;
    std::vector<io::ResolutionIssue> freshIssues;
    std::vector<std::string> freshWarnings;
    if (!io::scanProject(path, freshGraph, freshSummary, freshIssues,
                         freshWarnings))
    {
        out << "  Could not scan \"" << path << "\":\n";
        for (const std::string& warning : freshWarnings)
            out << "    " << warning << "\n";
        out << "  Previous scan (if any) is kept.\n";
        return;
    }

    state.graph = std::move(freshGraph);
    state.summary = freshSummary;
    state.issues = std::move(freshIssues);
    state.warnings = std::move(freshWarnings);
    state.hasGraph = true;

    out << "  Scan complete: " << path << "\n"
        << "    files scanned:        " << state.summary.filesScanned
        << "\n"
        << "    dependency edges:     " << state.graph.edgeCount() << "\n"
        << "    quoted includes:      " << state.summary.quotedIncludes
        << "\n"
        << "    angle includes:       " << state.summary.angleIncludes
        << "\n"
        << "    unresolvable (macro/malformed): "
        << state.summary.unresolvableIncludes << "\n"
        << "    unresolved (no project file):   "
        << state.summary.unresolvedIncludes << "\n"
        << "    self-includes:        " << state.summary.selfIncludes
        << "\n";
    if (!state.issues.empty())
        for (const io::ResolutionIssue& issue : state.issues)
            out << "    unresolved: " << issue.includerPath << " -> "
                << issue.target << "\n";
    for (const std::string& warning : state.warnings)
        out << "    warning: " << warning << "\n";
}

// --------------------------------------------------------------------------
// Option 2 — View Dependency Graph
// --------------------------------------------------------------------------

inline void viewGraphMenu(const AppState& state, std::ostream& out)
{
    if (graphNotReady(out, state))
        return;
    const int n = state.graph.nodeCount();
    out << "  Files (" << n << "):\n";
    for (int id = 0; id < n; ++id)
        out << "   " << id << ": " << nodePath(state, id) << "\n";

    out << "  Dependency edges (A -> B means A depends on B):\n";
    if (state.graph.edgeCount() == 0)
        out << "    (none)\n";
    for (int id = 0; id < n; ++id)
        for (const int succ : state.graph.successors(id))
            out << "   " << nodePath(state, id) << " -> "
                << nodePath(state, succ) << "\n";
}

// --------------------------------------------------------------------------
// Option 3 — Explore Dependencies (BFS, variant B1)
// --------------------------------------------------------------------------

inline void exploreMenu(const AppState& state, std::istream& in,
                        std::ostream& out)
{
    if (graphNotReady(out, state))
        return;
    const int id = pickFile(state, in, out, "Explore dependencies of file");
    if (id < 0)
        return;

    const bfs::TraversalReport report = bfs::explore(state.graph, id);
    const int n = state.graph.nodeCount();
    out << "  Transitive dependencies of " << nodePath(state, id)
        << " (B1 forward BFS; level = dependency distance):\n";
    printLevelGroups(
        out, state, n,
        [&report](const int i) {
            return report.level[static_cast<std::size_t>(i)];
        },
        "  level 0 (the file itself):");
    out << "  not reachable from this file: "
        << (n - static_cast<int>(report.counters.verticesVisited))
        << " file(s)\n"
        << "  traversal counters: visited "
        << report.counters.verticesVisited << ", edges examined "
        << report.counters.edgesExamined << ", max frontier "
        << report.counters.maxFrontier << "\n";
}

// --------------------------------------------------------------------------
// Option 4 — Analyze Change Impact (F3, reverse BFS)
// --------------------------------------------------------------------------

inline void impactMenu(const AppState& state, std::istream& in,
                       std::ostream& out)
{
    if (graphNotReady(out, state))
        return;
    const int id = pickFile(state, in, out, "Changed file");
    if (id < 0)
        return;

    const analysis::ImpactReport report = analysis::impactOf(state.graph, id);
    out << "  Change impact of " << nodePath(state, id)
        << " (who transitively depends on it; level 0 = the file):\n";
    printLevelGroups(
        out, state, state.graph.nodeCount(),
        [&report](const int i) { return report.level[i]; },
        "  level 0 (the changed file):");
    out << "  affected files total: " << report.affectedCount << "\n";
}

// --------------------------------------------------------------------------
// Option 5 — Find Dependency Path (B3 bidirectional BFS)
// --------------------------------------------------------------------------

inline void pathMenu(const AppState& state, std::istream& in,
                     std::ostream& out)
{
    if (graphNotReady(out, state))
        return;
    const int from =
        pickFile(state, in, out, "Path START file (the dependent)");
    if (from < 0)
        return;
    const int to =
        pickFile(state, in, out, "Path TARGET file (the dependency)");
    if (to < 0)
        return;

    if (from == to)
    {
        out << "  Path: [ " << nodePath(state, from) << " ] (start == "
               "target, zero hops)\n";
        return;
    }
    const bfs::PathReport report =
        bfs::bidirectionalPath(state.graph, from, to);
    if (!report.found)
    {
        out << "  No dependency path from " << nodePath(state, from)
            << " to " << nodePath(state, to) << ".\n";
        return;
    }
    out << "  Shortest path (" << report.path.size() - 1 << " hop(s)):\n   ";
    for (std::size_t i = 0; i < report.path.size(); ++i)
        out << (i == 0 ? " " : " -> ") << nodePath(state, report.path[i]);
    out << "\n";
}

// --------------------------------------------------------------------------
// Option 6 — Search Files by Prefix (F7 Trie)
// --------------------------------------------------------------------------

inline void prefixSearchMenu(const AppState& state, std::istream& in,
                             std::ostream& out)
{
    if (graphNotReady(out, state))
        return;
    out << "  Filename prefix (empty line = cancel): ";
    std::string prefix;
    if (!std::getline(in, prefix) || prefix.empty())
        return;

    const ds::Trie nameIndex = analysis::buildNameIndex(state.graph);
    const std::vector<int> matches =
        analysis::filesWithPrefix(state.graph, nameIndex, prefix);
    if (matches.empty())
    {
        out << "  No stored file name starts with \"" << prefix << "\".\n";
        return;
    }
    out << "  " << matches.size() << " file name(s) start with \"" << prefix
        << "\":\n";
    for (const int id : matches)
        out << "   " << id << ": " << nodePath(state, id) << "\n";
    out << "  Exact basename \"" << prefix << "\" exists: "
        << (analysis::fileExistsByBasename(nameIndex, prefix) ? "yes"
                                                             : "no")
        << "\n";
}

// --------------------------------------------------------------------------
// Option 7 — View Graph Statistics
// --------------------------------------------------------------------------

inline void statsMenu(const AppState& state, std::ostream& out)
{
    if (graphNotReady(out, state))
        return;
    const int n = state.graph.nodeCount();

    out << "  Project statistics\n";
    out << "    files: " << n << ", edges: " << state.graph.edgeCount()
        << "\n"
        << "    unresolved includes: " << state.summary.unresolvedIncludes
        << " (repeats counted; " << state.issues.size()
        << " distinct target(s))\n"
        << "    unresolvable directives: "
        << state.summary.unresolvableIncludes << "\n"
        << "    self-includes: " << state.summary.selfIncludes << "\n"
        << "    scanner warnings: " << state.warnings.size() << "\n";

    // Cycles (F2).
    const analysis::CycleReport cycles = analysis::findCycles(state.graph);
    if (!cycles.hasCycles)
        out << "    cycles: none (the graph is a DAG)\n";
    else
    {
        out << "    cycles: " << cycles.cyclesFound
            << " back edge(s) detected";
        if (cycles.depthGuardTripped)
            out << " (depth guard tripped: detection incomplete)";
        out << "\n";
        for (const std::vector<int>& cycle : cycles.cycleInstances)
        {
            out << "      cycle:";
            for (const int id : cycle)
                out << " " << nodePath(state, id);
            out << "\n";
        }
    }

    // Dependency ranking (F6, MaxHeap).
    const std::vector<int> top =
        analysis::mostDependedUpon(state.graph, 10);
    out << "    most depended-upon files (in-degree):\n";
    for (const int id : top)
        out << "      " << nodePath(state, id) << " ("
            << state.graph.inDegree(id) << " dependent(s))\n";

    // Deepest chain (F5).
    const analysis::ChainReport chain = analysis::longestChain(state.graph);
    if (chain.defined)
    {
        out << "    deepest dependency chain (" << chain.chain.size()
            << " files):";
        for (std::size_t i = 0; i < chain.chain.size(); ++i)
            out << (i == 0 ? " " : " -> ")
                << nodePath(state, chain.chain[i]);
        out << "\n";
    }
    else
        out << "    deepest chain: undefined (the graph has cycles)\n";

    // Compile order (F4, Kahn).
    const analysis::TopoReport topo = analysis::topologicalOrder(state.graph);
    if (topo.cycleBlocked.empty())
    {
        out << "    valid compile order (dependencies first";
        if (topo.ordered.size() > 10)
            out << ", first 10 shown";
        out << "):";
        const std::size_t shown =
            std::min<std::size_t>(topo.ordered.size(), 10);
        for (std::size_t i = 0; i < shown; ++i)
            out << " " << nodePath(state, topo.ordered[i]);
        out << "\n";
    }
    else
        out << "    compile order: blocked for " << topo.cycleBlocked.size()
            << " file(s) (cycle members / their dependents)\n";
}

// --------------------------------------------------------------------------
// Option 8 — Demonstrate Custom Data Structures
//
// Every section performs REAL operations on the REAL src/ds class and prints
// the actual resulting state. No simulated or hardcoded representations.
// --------------------------------------------------------------------------

// Arrow-diagram of a LinkedList's live contents via its iterator.
template <typename T>
void printLinkedList(std::ostream& out, const ds::LinkedList<T>& list)
{
    out << "      head ->";
    for (const T& value : list)
        out << " " << value << " ->";
    out << " nullptr  (size " << list.size() << ")\n";
}

inline void demoLinkedList(std::ostream& out)
{
    out << "    LinkedList<std::string> — append / pushFront / removeFirst\n";
    ds::LinkedList<std::string> list;
    list.append("main.cpp");
    list.append("graph.h");
    out << "      after append(main.cpp), append(graph.h):\n";
    printLinkedList(out, list);
    list.pushFront("config.h");
    out << "      after pushFront(config.h):\n";
    printLinkedList(out, list);
    list.removeFirst("main.cpp");
    out << "      after removeFirst(main.cpp):\n";
    printLinkedList(out, list);
}

inline void demoStack(std::ostream& out)
{
    out << "    Stack<int> — LIFO discipline (facade over ds::LinkedList)\n";
    ds::Stack<int> stack;
    for (const int value : {1, 2, 3})
    {
        stack.push(value);
        out << "      push(" << value << ")   -> top = " << stack.top()
            << "\n";
    }
    while (!stack.isEmpty())
        out << "      pop()        -> returned " << stack.pop() << "\n";
    out << "      empty now: " << (stack.isEmpty() ? "yes" : "no") << "\n";
}

inline void demoQueue(std::ostream& out)
{
    out << "    Queue<std::string> — FIFO discipline (facade over "
           "ds::LinkedList)\n";
    ds::Queue<std::string> queue;
    for (const char* value : {"scan.cpp", "parse.cpp", "report.cpp"})
    {
        queue.enqueue(value);
        out << "      enqueue(" << value << ") -> front = " << queue.front()
            << "\n";
    }
    while (!queue.isEmpty())
        out << "      dequeue()    -> returned " << queue.dequeue() << "\n";
    out << "      empty now: " << (queue.isEmpty() ? "yes" : "no") << "\n";
}

inline void demoHashTable(std::ostream& out)
{
    out << "    HashTable<int> — FNV-1a + separate chaining on "
           "LinkedList\n";
    ds::HashTable<int> table;
    const char* keys[] = {"main.cpp", "graph.h", "config.h",
                          "api.cpp",  "api.h",   "util.h"};
    for (int i = 0; i < 6; ++i)
        table.insert(keys[i], i);
    out << "      inserted 6 entries: size " << table.size()
        << ", buckets " << table.bucketCount() << ", load factor "
        << table.loadFactor() << "\n";
    // Real placement: the table's own published hash + bucket count.
    out << "      actual bucket placement (hashFor(key) % bucketCount):\n";
    for (std::size_t b = 0; b < table.bucketCount(); ++b)
    {
        bool any = false;
        for (const char* key : keys)
            if (ds::HashTable<int>::hashFor(key) % table.bucketCount() == b)
            {
                const int* value = table.find(key);
                out << (any ? " -> " : "        bucket ") << key << "="
                    << *value;
                any = true;
            }
        if (any)
            out << "\n";
    }
    const int* found = table.find("graph.h");
    out << "      find(\"graph.h\") -> "
        << (found != nullptr ? std::to_string(*found) : "absent") << "\n";
    out << "      remove(\"graph.h\") -> "
        << (table.remove("graph.h") ? "removed" : "absent")
        << ", contains now: "
        << (table.contains("graph.h") ? "yes" : "no") << "\n";
}

inline void demoDynArray(std::ostream& out)
{
    out << "    DynArray<int> — amortized-doubling growth\n";
    ds::DynArray<int> array;
    for (int i = 1; i <= 20; ++i)
    {
        array.emplaceBack(i * 10);
        if (i == 1 || i == 16 || i == 17 || i == 20)
            out << "      after " << i << " emplaceBack(s): size "
                << array.size() << ", capacity " << array.capacity()
                << "\n";
    }
    out << "      contents:";
    for (std::size_t i = 0; i < array.size(); ++i)
        out << " " << array[i];
    out << "\n";
}

inline void demoTrie(std::ostream& out)
{
    out << "    Trie — basename-keyed prefix tree (real branch structure)\n";
    ds::Trie trie;
    trie.insert("api.h", 0);
    trie.insert("api.h", 9); // duplicate basename, second graph node
    trie.insert("api.cpp", 3);
    trie.insert("app.cpp", 5);
    trie.insert("util.h", 2);
    out << "      inserted 5 (name, id) pairs; size = " << trie.size()
        << "\n";
    out << "      branch structure (via Trie::visitNodes):\n";
    trie.visitNodes([&out](const std::string& prefix, const bool isTerminal,
                           const ds::LinkedList<int>& ids) {
        if (prefix.empty())
        {
            out << "        (root)\n";
            return true;
        }
        out << "        ";
        for (std::size_t i = 0; i + 1 < prefix.size(); ++i)
            out << "| ";
        out << "- " << prefix[prefix.size() - 1];
        if (isTerminal)
        {
            out << "  [TERMINAL";
            for (const int id : ids)
                out << " " << id;
            out << "]";
        }
        out << "\n";
        return true;
    });
    out << "      searchPrefix(\"api\") -> ids:";
    const ds::LinkedList<int> hits = trie.searchPrefix("api");
    for (const int id : hits)
        out << " " << id;
    out << "\n      containsExact(\"api.h\"): "
        << (trie.containsExact("api.h") ? "yes" : "no")
        << ", containsExact(\"ap\"): "
        << (trie.containsExact("ap") ? "yes" : "no") << "\n";
}

inline void demoMaxHeap(std::ostream& out)
{
    out << "    MaxHeap — (in-degree, fileId) ranking, live tree levels\n";
    ds::MaxHeap heap;
    const ds::MaxHeap::Entry entries[] = {
        {3, 1}, {5, 0}, {2, 2}, {5, 4}, {1, 3},
    };
    for (const ds::MaxHeap::Entry& entry : entries)
        heap.insert(entry);
    out << "      inserted 5 entries; size " << heap.size() << "\n";
    // Real binary-heap layout via the published diagnostic view.
    std::size_t index = 0;
    for (int level = 0; index < heap.size(); ++level)
    {
        const std::size_t count =
            std::min<std::size_t>(std::size_t{1} << level,
                                  heap.size() - index);
        out << "        level " << level << ":";
        for (std::size_t i = 0; i < count; ++i, ++index)
        {
            const ds::MaxHeap::Entry& e = heap.rawEntryAt(index);
            out << " (" << e.score << "," << e.fileId << ")";
        }
        out << "\n";
    }
    for (int i = 0; i < 3 && !heap.isEmpty(); ++i)
    {
        const ds::MaxHeap::Entry top = heap.extractMax();
        out << "      extractMax() -> (" << top.score << "," << top.fileId
            << ")\n";
    }
    out << "      remaining: " << heap.size() << " entr"
        << (heap.size() == 1 ? "y" : "ies") << "\n";
}

inline void dsDemoMenu(std::ostream& out)
{
    out << "  Custom data-structure demonstrations (live src/ds objects)\n";
    demoLinkedList(out);
    demoStack(out);
    demoQueue(out);
    demoHashTable(out);
    demoDynArray(out);
    demoTrie(out);
    demoMaxHeap(out);
}

// --------------------------------------------------------------------------
// Option 9 — Run BFS Benchmarks (Phase 5/6 runner, untouched)
// --------------------------------------------------------------------------

inline void benchmarkMenu(std::ostream& out)
{
#ifndef NDEBUG
    out << "  NOTE: this is a Debug/sanitizer build — the printed timings\n"
           "  are NOT citable. Run the Release binary for real numbers.\n";
#endif
    out << "  Running the BFS benchmark matrix (5 shapes x 1k/5k/10k +\n"
           "  scanned fixture; 7 variants; warm-up + 10 trials)...\n"
        << std::flush;
    const std::size_t timedCalls =
        bench::runBenchmarkSuite("results/menu_run");
    out << "  Done: " << timedCalls << " timed variant executions.\n"
        << "  Results written to results/menu_run/ (benchmarks_raw.csv,\n"
           "  benchmarks_summary.csv, benchmark_env.txt).\n"
        << "  The frozen Phase 6 record in results/frozen/ is NOT touched.\n";
}

} // namespace app
