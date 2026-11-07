#pragma once
// src/graph/dependency_graph.h
//
// The dependency graph model (Phase 2): nodes = project files, directed edge
// A → B = "A #includes B" = "A depends on B" (ARCHITECTURE.md §1).
//
// Layering: this class sits between the custom DS library (below it) and the
// analytics engines (above it). Algorithms will only READ a finalized graph;
// traversal results are owned by their callers — this class stores nothing on
// their behalf (AGENTS.md §5 ownership model).
//
// Required-structure roles (approved synopsis mapping — load-bearing, not
// decorative):
//  - ds::HashTable   : file path → node id, O(1) expected, while building and
//                      while resolving includes (Phase 3);
//  - ds::LinkedList  : every adjacency list (forward AND reverse) — insertion
//                      order is preserved so later output/tie-break rules are
//                      deterministic (Phase 2 ruling #4: no sorting here);
//  - ds::DynArray    : flat indexed storage for the node table and adjacency
//                      lists (node ids 0..V-1 index straight into it). It is
//                      the support primitive from Phase 1.7, NEVER a
//                      replacement for a required structure.
//
// Decisions ruled by the team for Phase 2 (2026-11-07):
//  - Node identity = project-relative path (ARCHITECTURE §9 #4); duplicate
//    basenames stay separate nodes. The basename is stored too (name field)
//    so Phase 3 can hand the exact Trie key without a re-derivation rule.
//  - Node ids are stable integers 0..V-1 in insertion order and never change
//    once assigned; clear() resets the whole graph, ids included.
//  - Self-edges ARE recorded (B1): a file including itself is a legitimate
//    (length-1) dependency cycle, so cycle detection and topological ordering
//    must see it, not a parse-level counter only.
//  - Duplicate edges are rejected (no-op → false): the SAME #include line
//    twice must not double-weight degree counts.
//
// Finalization contract:
//  - forward adjacency is valid immediately (it IS what addEdge builds);
//  - predecessors()/inDegree() are valid only after finalize() built the
//    reverse adjacency — calling them unfinalized is a programming error
//    (assert, like every ds precondition);
//  - any structural mutation (new node, new edge) flips the graph back to
//    unfinalized; a no-op mutation (duplicate node/edge) does not, because
//    the reverse mirror is still exact for the unchanged edge set;
//  - finalize() is idempotent and may be re-run after mutations.

#include "ds/dyn_array.h"
#include "ds/hash_table.h"
#include "ds/linked_list.h"

#include <cassert>
#include <cstddef>
#include <string>
#include <utility>

namespace graph
{

class DependencyGraph
{
public:
    static constexpr int kInvalidNodeId = -1;

    // One file of the analyzed project. `path` is the identity used by the
    // id hash table; `name` is the basename (the Trie key) kept alongside so
    // Phase 3 never re-derives one from the other.
    struct Node
    {
        std::string path;
        std::string name;
        std::size_t unresolvedIncludes = 0; // counted at parse time (Phase 3)
    };

    DependencyGraph() = default;

    // Defaulted move is correct: every member has a hand-verified move
    // implementation (DynArray steals its buffer, HashTable resets count_)
    // and there are no cross-member invariants beyond edgeCount_, which moves
    // by value together with the arrays it describes.
    DependencyGraph(DependencyGraph&&) noexcept = default;
    DependencyGraph& operator=(DependencyGraph&&) noexcept = default;

    // Copying a whole graph is never wanted in our flows (re-scan starts from
    // clear(); analytics read, they do not clone). Deleted per AGENTS.md.
    DependencyGraph(const DependencyGraph&) = delete;
    DependencyGraph& operator=(const DependencyGraph&) = delete;

    ~DependencyGraph() = default; // RAII: all owned storage frees in members

    // --- building ----------------------------------------------------------

    // Returns the id of the node for `path`, creating the node (next id in
    // insertion order) when new. O(1) expected. A duplicate call is a no-op
    // that returns the existing id — ids already assigned never change.
    int addNode(const std::string& path, const std::string& name)
    {
        if (const int* existing = idByPath_.find(path))
            return *existing;

        const int freshId = static_cast<int>(nodes_.size());
        const bool inserted = idByPath_.insert(path, freshId);
        assert(inserted && "absence verified above; absent-node insert");
        (void)inserted; // keep Release builds free of unused-value warnings
        nodes_.emplaceBack();
        Node& fresh = nodes_[static_cast<std::size_t>(freshId)];
        fresh.path = path;
        fresh.name = name;
        forwardAdjacency_.emplaceBack();
        finalized_ = false; // reverse adjacency must be rebuilt for new ids
        return freshId;
    }

    // Resolves a path to its node id into `idOut`. False (and untouched
    // idOut) when unknown. O(1) expected — the Phase 3 include resolver asks
    // this for every candidate resolution.
    bool findId(const std::string& path, int& idOut) const
    {
        if (const int* id = idByPath_.find(path))
        {
            idOut = *id;
            return true;
        }
        return false;
    }

    // Adds the edge from → to (dependency = from includes to). Returns false
    // — and changes nothing — when either id is invalid or the edge already
    // exists (duplicate rejection). Self-edges are recorded as real edges
    // (approved B1). Insertion order is preserved in the adjacency list.
    bool addEdge(int from, int to)
    {
        if (!validNodeId(from) || !validNodeId(to))
            return false;
        ds::LinkedList<int>& source = forwardAdjacency_[from];
        if (source.contains(to))
            return false;
        source.append(to);
        ++edgeCount_;
        finalized_ = false; // the reverse mirror must be rebuilt
        return true;
    }

    // Records that the file with this id contained an include that resolved
    // to no project file (Phase 3 scanner reports; we never invent edges).
    // Purely informational — does not touch adjacency or finalize state.
    void addUnresolvedInclude(int nodeId)
    {
        assert(validNodeId(nodeId) && "addUnresolvedInclude invalid node id");
        ++nodes_[nodeId].unresolvedIncludes;
    }

    // Builds the reverse adjacency lists (mirror of every edge) exactly once
    // per structural state. After it returns, in-degrees and predecessor
    // lists are valid until the next structural mutation. Idempotent: calls
    // on an already-finalized, unmutated graph return immediately. O(V + E).
    void finalize()
    {
        if (finalized_)
            return;
        AdjacencyArray reverse(nodes_.size());
        for (std::size_t u = 0; u < forwardAdjacency_.size(); ++u)
            for (const int v : forwardAdjacency_[u])
                reverse[v].append(static_cast<int>(u));
        reverseAdjacency_ = std::move(reverse);
        finalized_ = true;
    }

    // Full reset for a re-scan (mirrors Trie::clear). Ids restart at 0 —
    // allowed because the whole graph is reset, not one node (Phase 2
    // ruling #2).
    void clear()
    {
        nodes_ = NodeArray();
        forwardAdjacency_ = AdjacencyArray();
        reverseAdjacency_ = AdjacencyArray();
        idByPath_ = IdTable();
        edgeCount_ = 0;
        finalized_ = false;
    }

    // --- reading -----------------------------------------------------------

    bool isFinalized() const { return finalized_; }

    int nodeCount() const { return static_cast<int>(nodes_.size()); }
    std::size_t edgeCount() const { return edgeCount_; }

    // Valid id range: 0 .. nodeCount()-1 (kInvalidNodeId is the "absent"
    // marker for searches, never an argument here — precondition assert).
    const Node& node(int id) const
    {
        assert(validNodeId(id) && "node() invalid node id");
        return nodes_[id];
    }

    // Files that `id` includes (outgoing edges). Valid any time. Iteration
    // order = insertion order (determinism ruling).
    const ds::LinkedList<int>& successors(int id) const
    {
        assert(validNodeId(id) && "successors() invalid node id");
        return forwardAdjacency_[id];
    }

    // Files that include `id` (incoming edges). FINALIZED ONLY: built by
    // finalize(), asserts otherwise — an empty result must never be
    // confusable with a real "no dependencies" answer.
    const ds::LinkedList<int>& predecessors(int id) const
    {
        assert(validNodeId(id) && "predecessors() invalid node id");
        assert(finalized_ && "predecessors() before finalize()");
        return reverseAdjacency_[id];
    }

    std::size_t outDegree(int id) const { return successors(id).size(); }

    std::size_t inDegree(int id) const
    {
        assert(finalized_ && "inDegree() before finalize()");
        return predecessors(id).size();
    }

private:
    bool validNodeId(int id) const
    {
        return id >= 0 && id < static_cast<int>(nodes_.size());
    }

    using NodeArray = ds::DynArray<Node>;
    using AdjacencyArray = ds::DynArray<ds::LinkedList<int>>;
    using IdTable = ds::HashTable<int>;

    NodeArray nodes_;              // index = node id
    AdjacencyArray forwardAdjacency_;     // successors per id
    AdjacencyArray reverseAdjacency_;     // predecessors per id (finalize)
    IdTable idByPath_;             // path → id (required HashTable role)
    std::size_t edgeCount_ = 0;    // accepted (non-duplicate) edges only
    bool finalized_ = false;
};

} // namespace graph
