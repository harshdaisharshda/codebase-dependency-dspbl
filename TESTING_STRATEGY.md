# TESTING_STRATEGY.md — Codebase Dependency Analyzer

**Status:** v2.0 (planning; grounded in the synopsis + instructor addendum). We
write tests as we develop (Phase 1 onward), not at the end.

---

## 1. Testing philosophy

1. **Correctness over coverage numbers.** The viva will probe edge cases; so do
   our tests. "Green tests" means hand-computed expected outputs match.
2. **Data structures are tested in isolation first.** The 8 custom structures are
   the biggest source of bugs; they break in edges cases (empty, single,
   duplicate, full, rehash, tie). Each gets its own suite *before* any other
   phase uses it.
3. **Features are tested end-to-end on a known fixture project**, so every
   synopsis feature has a *human checkable* expected answer.
4. **No external test framework.** `tests/test_framework.h` : a ~60-line assert
   harness with `TEST`, `CHECK`, `CHECK_EQ`, `REQUIRE_THROWS_NOTHING`, a test
   registry, and a summary (passed/failed counts + failing line numbers).
   Reason: zero dependencies, we control the output, small enough to understand.
5. **Two binaries, both always green:** `unit-tests` (this harness) and the
   `dep-analyzer` app. Unit tests must pass in Debug **and** Release.
6. **Sanitizer runs are part of "passing"**: Debug builds run with
   `-fsanitize=address,undefined`; leaks count as failures.
7. **Timing is never asserted.** Benchmarks measure; tests verify counts and
   equivalence. A timing assert would produce flaky failures on a busy machine.

---

## 2. Test layers

```
Layer 1  Unit (per DS)               — fast, no filesystem
Layer 2  Algorithm (on hand-built graphs, in code) — fast, no filesystem
Layer 3  Parser/IO (fixture files)   — reads tests/fixtures/sample_project
Layer 4  Integration (full pipeline) — scan → graph → algorithm → formatted output
Layer 5  Bench validation            — counters verified, timings only smoke-checked
Layer 6  Manual CLI checklist        — viva-style, scripted menu input (location: docs/)
```

---

## 3. Layer 1 — Custom data structure unit tests

### 3.1 LinkedList
empty; append grows size; pushFront order; `at()` out-of-range asserts (not UB);
iteration order matches insertion; clear() empties; copy discipline (deep copy or
deleted: compile-time check); stress: 10k appends + iterate + clear.

### 3.2 Stack (template)
LIFO order; `top()`/`pop()` on empty is a programming error → assert path (test
*asserts*, do not test crash); peak/size consistency; no leaks after 1M push/pop cycles.

### 3.3 Queue (template, ring buffer)
FIFO order; **wrap-around** correctness (enqueue/dequeue past capacity boundary);
growth keeps FIFO; empty-dequeue is an assert; size/capacity bookkeeping.

### 3.4 HashTable (string→int, chaining)
insert + lookup round-trip; **collisions** — force with known colliding keys
(hand-built ordering or linear probes of the chosen hash); duplicate insert
rejected (returns false, old value kept); rehash correctness (put 1000 keys,
lookup all 1000, size stays 1000); remove removes and lookup fails;
contains/lookup on absent key; empty-string key accepted; load-factor triggers rehash
(benchmark-friendly: expose bucketCount for tests).

### 3.5 Trie
insert then `containsExact`; prefix search returns all ids sharing the prefix;
**multiple files with the same basename** (two ids under one leaf — both found);
exact match must not match longer names; **empty prefix** – documents behavior
(returns every file or is rejected — must be one of these and tested); digits &
underscores ('.', '_', '-') are valid chars in filenames.

### 3.6 MaxHeap (score, id)
build-heap from a list (O(n)) equals incremental insertion; extractMax pops in
descending order; **ties**: deterministic order rule (second key lexicographic);
heap invariant holds after every op (checked by walking internal array in tests);
"k=0" and "k > size" extract loops terminate properly.

### 3.7 IntArray / BoolArray
default values on resize; bounds behavior; fill; used as dist/parent/visited in
later tests.

---

## 4. Layer 2 — Graph/algorithm tests (graphs built directly in code)

Fixture graph builders: `make_chain(n)`, `make_cycle(n)`, `make_diamond()`,
`make_disconnected()`, plus edge-list builder with named nodes, using the graph
model from Phase 2 directly (no filesystem dependency).

### 4.1 Graph model tests
node id stability after more inserts; duplicate addEdge is a no-op; self-edge
handling acknowledged; degrees after finalize() match adjacency lists; reverse
adjacency is truly the edge-set mirror (V→{u: every edge u→v has the mirrored edge v→u in radj[u]}).

### 4.2 DFS cycle detection tests
acyclic → no cycle; 2-cycle, 3-cycle, self-include; cycle + tail (diamond-with-cycle);
deep chain — recursion-depth guard triggers graceful handling (never a crash);
cycle path reconstruction via the Stack prints the cycle in edge order.

### 4.3 Topological order tests
cycle-free chain/diamond/topo validator (verify validity rather than exact order: each edge source appears before its target; report covers exactly all non-cycle-nodes); graph with a cycle → correct *unorderable* detection.

### 4.4 Impact analysis tests
reverse reachability on diamond (both parents' parents reachable); unrelated
component untouched (count = 0); level values correct on a layered chain.

### 4.5 Deepest chain tests
chain length on a linear DAG matches n nodes exactly; diamond → picks the longer leg;
cycle case → **"longest chain undefined"** is reported, with the cycle groups
listed (never a condensed chain); ties broken deterministically by node order.

### 4.6 Hub ranking tests
in-degree counts match list lengths; top-1 with tie broken lexicographically;
k > V returns everything.

### 4.7 Prefix search tests
single match, multiple matches, no-match (empty list), prefix shorter than a added name,
existence check consistent with ids.

### 4.8 Path finding tests
existing path found and its hop count minimal; no path → empty output; direct edge
path of length 1; endpoints in disconnected components → no path.

---

## 5. Layer 3 — Scan & `#include` parser tests (fixture-based)

Fixture `tests/fixtures/sample_project/` (12 files):

```
main.cpp          #include "api/api.h",  #include "utils/utils.h"
api/api.h         #include "database/database.h"      (project-root include path)
api/api.cpp       #include "api.h",  // #include "comments_only.h"  (comment line -> NOT an edge)
database/database.h   #include "missing.h"            (unresolved include)
database/database.cpp  #include <string>              (angle bracket -> NOT a project edge)
graph.cpp         #include "graph.h",  #include "shared/util.h"
graph.h           (no includes)
utils/utils.h     #include "shared/util.h"
shared/util.h     (no includes)
cycle_a.h         #include "cycle_b.h"
cycle_b.h         #include "cycle_a.h"                 (2-cycle for F2)
network/api.h     (no includes)                        (duplicate basename with api/api.h -> separate node)
```

Parser tests:
- quoted includes found (incl. relative paths with subdirectories and
  project-root-relative paths, per approved rule: includer's dir first, then root),
- commented include ignored; angle include ignored,
- include resolution order (dir-of-includer, then project root) produces these ids,
- unresolved include counted and **not** creating an edge,
- duplicate basename case: `api/api.h` vs `network/api.h` — two distinct node
  paths sharing one basename (approved decision #5),
- self-include detection counted (unit-level scenario; fixture stays clean).

---

## 6. Layer 4 — Integration tests

1 per synopsis feature on the fixture project, asserting *hand-derived* outputs:

| Feature | Expected on fixture (abridged) |
|---------|-------------------------------|
| Scan summary | 12 nodes, 9 resolved edges, 1 unresolved (`missing.h`), 1 ignored commented include, 1 ignored angle include, no self-edges |
| Cycles | exactly one cycle group {cycle_a.h, cycle_b.h}; concrete instance reported |
| Impact of `shared/util.h` | transitive dependents: `graph.cpp` and `utils/utils.h` (level 1), `main.cpp` (level 2); cycle group untouched |
| Topo order | valid order over the 10 non-cycle nodes; `cycle_a.h`/`cycle_b.h` appear in the leftover (cycle-blocked) list |
| Deepest chain | **"undefined"** on this fixture (cycle exists per approved policy); DAG case verified at Layer 2 with a hand-built acyclic graph |
| Hubs top | `shared/util.h` and `api/api.h` tie at in-degree 2; deterministic path-lexicographic ordering freezes the list |
| Prefix search "api" | returns BOTH `api/api.h` and `network/api.h` (duplicate basename decision) |
| Path `main.cpp` → `shared/util.h` | exists via `main→utils→shared`, min hops = 2; reverse direction (`shared/util.h→main.cpp`) does not exist as forward and must not be found |

Plus 1 **menu scripted flow** test: pipe a documented input sequence into the app
(build graph → cycles → exit) and check exit code + key strings in output.
Format checks decompose outputs into structural asserts, not byte-diff.

---

## 7. Layer 5 — Benchmark validation (addendum)

Correctness first, always:
- On a known graph, every BFS variant's visited/edges/frontier counters are
  asserted numerically (exact values) — counters are features, not byproducts.
- **Equivalence tests** (invariant across sizes):
  B5 with very large depth ≡ B1's visited set;
  B4 single-source == B2 restricted to that source's reverse reachability;
  B6 visited set == B1 visited set always;
  B3 path length == plain-BFS path length when a path exists.

Timing methodology (never gates correctness):
- warm-up runs discarded; ≥10 measured trials per (algorithm × graph);
  drop min and max; mean + spread reported;
- environment recorded (build type, flags, machine),
- smoke checks only: times > 0; largest/densest cases are generally the slowest;
  a > 100× regression regresses from a recorded baseline → investigate, not fail CI.

Generated graph support: same seeded generator functions exercised in tests
(small sizes) so the exact same graph files are compared times over times.

---

## 8. How to run

```
# Configure & build (Phase 0 keeps the exact commands updated here)
cmake --build build/Release && ./build/Release/unit-tests   # all suites, summary
./build/Release/dep-analyzer                                 # manual, menu UI

# Sanitizer run (debug):
cmake --build build/Debug && ASAN_OPTIONS=detect_leaks=1 ./build/Debug/unit-tests

# Benchmarks (release only):
./build/Release/dep-analyzer     # menu → 12 (benchmarks); CSV → results/
```

(Exact paths will be finalized in Phase 0; this section stays accurate.)

---

## 9. Defect workflow

1. Failing test → TASK_LOG entry (description + repro + assigne).
2. Fix must ship with the test that would have caught it (or an amended test).
3. No fix is "done" until unit-tests + CLI integration check pass in Debug & Release.
4. Benchmark surprises (unexpected slowness) are investigated on their own task
   line; timing regressions are documented, not hidden.
