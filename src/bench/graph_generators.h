#pragma once
// src/bench/graph_generators.h
//
// Phase 5.4 — seeded, deterministic synthetic dependency graphs for the
// benchmark suite (approved shape set, ARCHITECTURE §6 + ruling §9.20):
//
//   Chain     : node i depends on node i−1 (edges to EARLIER ids) — n−1 edges
//   Tree      : node i has ONE uniformly random earlier parent — n−1 edges
//   Sparse    : random earlier-target pairs, 2.5 edges/node
//   Dense     : random earlier-target pairs, 8 edges/node (approved cap)
//   HubHeavy  : 2 edges/node; 40% of that edge mass targets the hub set
//               (node id % 20 == 19 → 5% of nodes), rest random
//
// All shapes are acyclic BY CONSTRUCTION: every edge points from a node to a
// strictly smaller id (chain/tree explicitly; the random shapes draw u < v
// with retries until the exact edge budget is met — duplicate edges are
// rejected by DependencyGraph and retried, so budgets are EXACT).
//
// Determinism: a self-contained SplitMix64 stream (no <random> — immune to
// standard-library implementation differences). The same (shape, nodes, seed)
// triple always produces the identical edge list.
//
// Node identity: paths "gen/<kind>/<i>.cpp", basenames "<i>.cpp" — no
// duplicate basenames (irrelevant to BFS but keeps the Trie layer meaningful
// if a benchmark ever needs it). The graph is finalized at the end, ready for
// every BFS variant.

#include "graph/dependency_graph.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bench
{

enum class Shape
{
    Chain,
    Tree,
    Sparse,
    Dense,
    HubHeavy
};

inline const char* shapeName(const Shape shape)
{
    switch (shape)
    {
    case Shape::Chain:
        return "chain";
    case Shape::Tree:
        return "tree";
    case Shape::Sparse:
        return "sparse";
    case Shape::Dense:
        return "dense";
    case Shape::HubHeavy:
        return "hub_heavy";
    }
    return "unknown";
}

// SplitMix64: tiny, fast, fully specified — same bits on every machine.
class Rng
{
public:
    explicit Rng(const std::uint64_t seed)
        : state_(seed + 0x9E3779B97F4A7C15ULL)
    {
    }

    std::uint64_t next()
    {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // Uniform in [0, bound). Modulo bias is irrelevant at benchmark scales.
    int below(const int bound)
    {
        assert(bound > 0 && "Rng::below: empty range");
        return static_cast<int>(next() % static_cast<std::uint64_t>(bound));
    }

private:
    std::uint64_t state_;
};

// A node is a hub iff (id % 20) == 19 — exactly 5% of nodes, ids 19, 39, …
inline bool isHubId(const int id) { return id % 20 == 19; }

// Builds the requested graph into `out` (must be empty) and finalizes it.
// Deterministic for (shape, nodes, seed); never throws.
inline void generateGraph(const Shape shape, const int nodes,
                          const std::uint64_t seed,
                          graph::DependencyGraph& out)
{
    assert(nodes >= 1 && "generateGraph: empty benchmark graph");
    assert(out.nodeCount() == 0 &&
           "generateGraph: output graph must be empty (scanProject-style "
           "precondition)");

    const std::string kind = shapeName(shape);
    for (int i = 0; i < nodes; ++i)
    {
        const std::string id = std::to_string(i);
        out.addNode("gen/" + kind + "/" + id + ".cpp", id + ".cpp");
    }

    // Retry budget: duplicates make some draws no-ops; 20× budget is far
    // beyond any realistic collision rate — exceeding it is a logic error.
    std::size_t attempts = 0;
    const auto addEdgeBudgeted = [&out, &attempts](const int v, const int u) {
        ++attempts;
        return out.addEdge(v, u);
    };

    Rng rng(seed);
    switch (shape)
    {
    case Shape::Chain:
    {
        // node i depends on node i−1 (ruling §9.25c: edges to earlier ids)
        for (int i = 1; i < nodes; ++i)
            addEdgeBudgeted(i, i - 1);
        break;
    }
    case Shape::Tree:
    {
        // arborescence: every node i ≥ 1 has exactly one earlier parent
        for (int i = 1; i < nodes; ++i)
            addEdgeBudgeted(i, rng.below(i));
        break;
    }
    case Shape::Sparse:
    case Shape::Dense:
    {
        const std::size_t target = shape == Shape::Sparse
                                       ? static_cast<std::size_t>(5) * nodes / 2
                                       : static_cast<std::size_t>(8) * nodes;
        while (out.edgeCount() < target)
        {
            const int v = 1 + rng.below(nodes - 1); // v ∈ [1, n−1]
            addEdgeBudgeted(v, rng.below(v));       // u ∈ [0, v−1] → acyclic
            assert(attempts <= 20 * target + 16 &&
                   "generateGraph: retry budget exhausted");
        }
        break;
    }
    case Shape::HubHeavy:
    {
        const std::size_t target = static_cast<std::size_t>(2) * nodes;
        const std::size_t hubTarget = target * 2 / 5; // 40% of the mass
        std::size_t hubEdges = 0;
        while (hubEdges < hubTarget)
        {
            const int v = 20 + rng.below(nodes - 20); // v ∈ [20, n−1]
            const int u = 19 + 20 * rng.below(nodes / 20); // a hub id
            if (u < v && addEdgeBudgeted(v, u))
                ++hubEdges;
            assert(attempts <= 20 * target + 16 &&
                   "generateGraph: retry budget exhausted");
        }
        while (out.edgeCount() < target)
        {
            const int v = 1 + rng.below(nodes - 1);
            addEdgeBudgeted(v, rng.below(v));
            assert(attempts <= 20 * target + 16 &&
                   "generateGraph: retry budget exhausted");
        }
        break;
    }
    }

    out.finalize(); // ready for every BFS variant immediately
}

} // namespace bench
