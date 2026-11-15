# SUBMISSION_NOTES.md — revision & viva guide

Internal revision document. Not a user manual (see README.md). Everything
here reflects the code as committed (`cef44d6`); no feature is claimed that
does not exist.

---

## A. Custom data structures (all in `src/ds/`, all hand-written)

| Structure | What it does | Why implemented | Where used | Complexity |
|---|---|---|---|---|
| `LinkedList<T>` | singly linked list, O(1) tail append, iterator | required; the backbone of adjacency and chaining | graph adjacency lists (successors/predecessors), HashTable chains, Stack/Queue backbone | append O(1), search O(n), removeFirst O(n) |
| `Stack<T>` | LIFO facade over LinkedList | required; demonstrates stack discipline | cycle-path reconstruction (F2), shortest-path reconstruction (B3) | push/pop/top O(1) |
| `Queue<T>` | FIFO facade over LinkedList | required; BFS needs FIFO | BFS frontiers: impact (F3), Kahn's algorithm (F4), path baseline (F8), B1/B2/B4/B5 | enqueue/dequeue O(1) |
| `HashTable<V>` | string→value map, separate chaining on LinkedList, FNV-1a 64-bit | required; O(1) name lookup | file path → node id during scan and all queries | avg O(1) at load ≤ 0.75, doubling rehash, worst O(chain) |
| `DynArray<T>` | growable array, amortized doubling (16→32→…), move-friendly slots | required; move-only elements must survive growth | level/parent tables, heap storage, bucket array, wavefronts | append amortized O(1), index O(1) |
| `Trie` | prefix tree over file basenames; one terminal may hold several node ids (duplicate basenames) | required; prefix search + existence | F7 filename search; the menu file picker | insert/contains O(L·F); prefix search O(prefix walk + subtree + K² dedup) |
| `MaxHeap` | array binary max-heap over (score, fileId); ties → smaller fileId | required; deterministic hub ranking | F6 most-depended-upon (buildFrom O(V) heapify) | insert/extract O(log n), buildFrom O(n) |

Design constants worth remembering: hash table rehashes when
`count*4 > buckets*3` (load 0.75, integer-exact); DynArray initial capacity
16; heap comparison is "higher score first, then smaller id" so results match
the lexicographic id assignment of the scanner.

## B. Graph algorithms (`src/graph/analysis.h`, all over a finalized graph)

Graph direction everywhere: **A → B means A depends on B**.

- **F2 Cycle detection** — recursive 3-color DFS (white/gray/black). A back
  edge to a gray vertex = cycle; the closed cycle path is rebuilt by walking
  the DFS-tree parent chain onto a `ds::Stack`. Self-include = length-1
  cycle. Detection is uncapped; reported instances capped (default 16) so
  hostile graphs can't flood output; a depth guard (default 10 000) marks
  `depthGuardTripped` instead of recursing into a stack overflow.
  Complexity O(V + E).
- **F3 Change impact** — BFS over *predecessors* (who includes me),
  transitively. Level 0 is the changed file itself. O(V + E).
- **F4 Topological order** — Kahn's algorithm: seed the queue with all
  out-degree-0 files (no unresolved dependencies) in id order; peel level by
  level. Leftovers when a cycle exists are reported as `cycleBlocked`
  (cycle members + their transitive dependents), never silently dropped.
  O(V + E).
- **F5 Longest dependency chain** — two steps: (1) if any cycle exists the
  answer is *undefined* (longest path is ill-defined with positive cycles);
  (2) otherwise memoized DFS (`memo[u]` = longest chain of nodes from u;
  `next[u]` = chosen first hop) reconstructs the chain. Ties → smaller id.
  O(V + E) after memoization.
- **F6 Most depended-upon** — every file's in-degree goes into the MaxHeap
  via `buildFrom` (O(V) bottom-up heapify — all scores are known at once, so
  O(V) beats V·O(log V) inserts), then extract top-k. Ties → smaller id.
- **F7 Filename search** — `buildNameIndex` builds a caller-owned Trie of
  basenames; `filesWithPrefix` returns ids ascending; `fileExistsByBasename`
  is the "does this file exist" check. Duplicate basenames (api/api.h vs
  network/api.h) share a terminal and both ids are returned.
- **F8 Shortest path** — classic forward BFS with a parent table; the path is
  rebuilt through a Stack so it prints in edge order. O(V + E).

## C. The six BFS variations (`src/graph/bfs.h`)

| # | Name | What differs | Intended use | Notes/limits |
|---|---|---|---|---|
| B1 | `explore` | classic FIFO forward BFS | transitive dependency view | — |
| B2 | `exploreMultiSource` | several seeds at level 0; level = distance to NEAREST seed | compile waves; "several files changed" | one seed ≡ B1 (asserted) |
| B3 | `bidirectionalPath` | two level-synchronized frontiers (forward from A, reverse from B); expand one level of the smaller side, **strict alternation on size ties**; meeting = first second-discovery, **smallest levelF+levelB sum wins** | shortest path A→B | ~b^(d/2) work vs b^d; provably shortest without a balance assumption; unbounded only (no hop budget) |
| B4 | `exploreReverse` | BFS over predecessor adjacency | impact analysis | ≡ `analysis::impactOf` (asserted) |
| B5 | `exploreBounded` | FIFO + level budget; horizon nodes visited but not expanded; `boundHit` = the bound cut off ≥1 edge scan | "look N hops ahead" | bound = ∞ ≡ B1 (asserted) |
| B6 | `exploreDirectionOptimizing` | per-level push/pull heuristic: `pushCost = Σ outDegree(wave)` vs `pullCost = unclaimed × E / n` (multiply-before-divide); pull only when strictly cheaper; ties → top-down | faster full traversal on large graphs | **simplified** Beamer-style heuristic, not the full cost model; levels/claimed set provably ≡ B1, discovery order may differ; `maxFrontier` = max wavefront size (not B1's queue peak); the pull branch never fired on the recorded matrix |

B1/B2/B4/B5 share one engine (`runCore`); B3 and B6 are separate engines.
Phase 4 functions stay untouched and serve as test oracles.

## D. Benchmarking (`src/bench/`, `bench` target, Phase 6 frozen record)

- **Shapes** (all acyclic by construction — every edge points to a smaller
  id): chain (n−1 edges), tree-like (n−1, random earlier parent), sparse
  (2.5n), dense (8n — approved cap), hub-heavy (2n, 40% of mass targets the
  5% hubs with `id % 20 == 19`). Sizes 1k/5k/10k. Seeds are fixed per
  (shape, size): `20261115 + nodes·1000 + shapeIndex` → fully reproducible
  (SplitMix64, independent of the standard library).
- **Variants timed:** b1, b2 (two seeds), b4, b5 (bound 3), b6, b3
  bidirectional, and the Phase 4 classic path as the B3 baseline (its counter
  columns are zeros by definition — the API predates counters).
- **Methodology:** 1 warm-up + 10 trials per (variant × graph); drop min/max;
  mean/median/stdev over the 8 kept; variant order rotated each trial;
  `steady_clock`; single graph object per case; graph construction,
  acyclicity validation and B3-reachability agreement happen untimed; counter
  drift across trials is asserted (they are deterministic).
- **Why Release:** `-O0` sanitizer builds measure the sanitizer, not the
  code; the bench binary keeps asserts *active* (`-UNDEBUG`) so the
  correctness checks still run, outside the timed region.
- **Metrics:** wall time (µs), vertices visited, edges examined, max
  frontier, B6 mode switches.
- **What the results establish** (frozen record `results/frozen/`, 16 cases ×
  7 variants × 10 trials = 1120 timed executions, Apple M5, clang 21, -O3):
  B6 beats B1 on dense/10000 (≈94 µs vs ≈145 µs) **without ever switching
  direction** — the win is wavefront discipline + memory locality, not
  pull-mode switching; B3 edges out the classic baseline on long chains;
  B2/B4/B5 behave as the equivalence tests predict.
- **Why B6 never went bottom-up:** the heuristic compares an exact push cost
  against an average-case pull cost. On these acyclic-by-construction shapes
  the wavefront's out-degree stayed ≤ the pull estimate `unclaimed × E / n`
  on every level of every instance, and the multiply-before-divide estimate
  never collapses to zero — so the top-down branch always won or tied. This
  is recorded honestly as a limitation; Beamer-style switching pays off
  mainly when a frontier covers a large, high-degree share of the graph,
  which these generators do not produce.
- **Reproduce:** `./build/Release/bench [outputDir]` (default `results`;
  the frozen record lives in `results/frozen/` and is not overwritten).

## E. Demonstration guide (menu sequence)

1. **1** (empty line) — scan the bundled fixture: summary shows 12 files,
   9 edges, 1 unresolved include.
2. **8** — data-structure showcase: live growth (16→32), a real hash
   collision chain, a real trie branch tree, heap levels.
3. **3** → `main.cpp` — BFS levels of dependencies.
4. **4** → `api/api.h` — impact: 3 files affected (dependents only!).
5. **5** → `main.cpp`, then `shared/util.h` — shortest path, 2 hops.
6. **6** → `api` — trie finds 3 files, duplicate basenames shown.
7. **7** — statistics: 1 back edge (`cycle_a.h ↔ cycle_b.h`), hub ranking,
   deepest chain undefined (cycle), compile order blocked for 2 files —
   **this is the strongest slide: every feature reacts correctly to the
   planted cycle.**
8. **9** (Release build only) — benchmark run, results in
   `results/menu_run/`; cite `results/frozen/` as the recorded record.

## F. Viva preparation — likely questions

**Q: Why a directed graph? Why is direction important?**
A dependency is not symmetric: `main.cpp` depending on `util.h` does not mean
`util.h` depends on `main.cpp`. Direction decides which algorithm runs over
which adjacency list: "what do I depend on" walks successors (B1), "who
depends on me" walks predecessors (F3/B4).

**Q: Why hand-write the data structures?**
Course requirement — but also honest engineering: adjacency lists are
appended far more than randomly accessed (LinkedList tail pointer), the
name→id map needs stable references during growth (separate chaining), and
the heap needs a deterministic tie-break that mirrors the scanner's
lexicographic id order.

**Q: Why does the heap tie-break prefer the smaller id?**
The scanner assigns ids in sorted directory-walk order, so "smaller id"
reproduces "lexicographically earlier path" — deterministic, testable
output.

**Q: How does cycle detection avoid infinite recursion?**
3-color DFS: gray = on the recursion stack. A back edge to a gray vertex is
a cycle; black vertices are fully processed and never re-entered. A depth
guard bounds recursion on deep graphs and flags incomplete detection instead
of crashing.

**Q: Why is the deepest chain undefined when a cycle exists?**
With a reachable cycle you can loop arbitrarily often, so no longest path
exists. The tool reports the first cycle found instead of inventing an
answer.

**Q: Why is bidirectional BFS faster, and why is it still shortest?**
Two frontiers grow roughly b^(d/2) instead of one growing b^d. It stays
shortest because BFS levels are exact distances: the first meeting round
always contains a vertex on some shortest path, and among all candidates the
one with the smallest `levelF + levelB` is selected — no "frontiers are
balanced" assumption needed.

**Q: What was the tie-rule bug in B3?**
Expanding the smaller frontier per round starves a side if ties always pick
the same side; the fix is strict alternation on equal sizes. A test caught
it before any commit.

**Q: Why can't B5 just use a visited flag and stop early?**
It can — but `boundHit` needs precision: the bound only counts as "hit" when
it actually prevented an edge scan (a horizon node that is a leaf did not
miss anything).

**Q: Is B6 the full Beamer algorithm?**
No — a documented simplification: per-level cost estimates, exact push cost
vs average-case pull cost, ties go top-down. The correctness invariant
(levels ≡ B1) is asserted by tests on hand-built and generated graphs.

**Q: Why do benchmark timings vary between runs but counters never do?**
All six variants are deterministic, so visited/edges/frontier are asserted
identical across trials; wall time depends on machine state, so it is
reported with min/max/median and never asserted.

**Q: What does `results/frozen/` prove?**
That the recorded numbers came from real runs of the committed code: 1120
timed executions on 16 graphs, environment recorded, and a second
independent run reproduced every non-timing column identically.

**Q: What is deliberately out of scope?**
Preprocessor evaluation (`#ifdef`), incremental re-scans, GUI, threading,
external libraries, report export (F9, deferred).
