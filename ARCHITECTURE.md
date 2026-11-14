# ARCHITECTURE.md — Codebase Dependency Analyzer

**Status:** v2.8 — Decisions approved by the team (2026-11-07), Phase 4 review
rulings in §9 (2026-11-09), and Phase 5 planning rulings §9.19–25
(2026-11-14). Phase 0–4 as-built (DS library `src/ds/`, DependencyGraph model
`src/graph/dependency_graph.h`, scanning/parsing `src/io/`, analytics engine
`src/graph/analysis.h`). **Phase 5 complete as-built** (5.1–5.2 committed
`47e2a95`/`e96bf99`; 5.3–5.5 2026-11-15): `src/graph/bfs.h` carries all six
variants — shared core behind B1/B2/B4/B5 (`explore`, `exploreMultiSource`,
`exploreReverse`, `exploreBounded`), **B3** `bidirectionalPath`, and **B6**
`exploreDirectionOptimizing` (simplified per-level push/pull heuristic with
`topDownLevels`/`bottomUpLevels` counters). Benchmark scaffolding shipped:
`src/bench/graph_generators.h` (deterministic SplitMix64 generators, five
shapes, acyclic by construction), `src/bench/benchmark_runner.h` +
`src/bench/bench_main.cpp` (`bench` binary; warm-up + 10 trials, rotated
variant order, drop min/max, counter-drift assertion; raw + summary CSV,
schema v1 — columns below in §6). Menu integration remains Phase 7.

> Note: the synopsis is a **Word document**, not the PDF initially referenced
> (`docs/Project_Synopsis_Codebase_Dependency_Analyzer.docx`). The file is
> **preserved unchanged**, and its folder was renamed from `docs ` (trailing
> space) to `docs/` — all documentation references this real path.

---

## 1. Project overview

A **console-based, menu-driven C++ application** (per the synopsis: "menu that
lets the user choose operations", `cin`/`cout`) that scans a C/C++ project
directory, reads each source/header file, and extracts its `#include`
relationships. The relationships form a **directed graph**:

- **Node = a source/header file**
- **Directed edge `A → B` = "A includes B", i.e., A depends on B**

On this graph the program answers practical questions a developer has about a
codebase: what will break if this file changes, in what order should files be
compiled, are there circular dependencies, which modules areLoad the most
depended-upon, what is the deepest chain of dependencies, and does a dependency
path exist between two given files? Every course data structure is used where it
naturally belongs, so the program doubles as a live demonstration for viva
examination.

### 1.1 Worked example from the synopsis

```
my-project/                          dependency tree produced by the analyzer
 ├── main.cpp                        main
 ├── utils.cpp                        ├── api
 ├── graph.cpp                        │    └── database
 ├── database.cpp                     └── utils
 └── api.cpp
```

The console output must be able to show this indented tree/levels form.

---

## 2. Requirements (consolidated)

### 2.1 Functional features — from the synopsis (authoritative)

| # | Feature | What it must do | Synopsis-mandated approach |
|---|---------|-----------------|---------------------------|
| F1 | Build Dependency Graph | parse scanned files, build directed graph | `#include` extraction, adjacency lists |
| F2 | Detect Circular Dependencies | find cycles that would break the build | DFS; stack tracks "files currently on the recursion stack"; an edge back to an on-stack file is a cycle |
| F3 | Find Files Affected By A Change | from a changed file, find every file that transitively depends on it | BFS expanding **level by level** (this means traversing *dependents*, i.e., reverse edges) |
| F4 | Topological Ordering | compile order: every file after its dependencies | topological sort (Kahn-style queue algorithm) |
| F5 | Deepest Dependency Chain | longest/most deeply nested dependency chain | DFS exploration ("recursion" topic) |
| F6 | Highly Connected Modules | files most depended-upon | rank by **in-degree** using a max heap |
| F7 | Search Files | fast prefix search over filenames (+ existence check) | Trie (autocomplete-friendly) |
| F8 | Dependency Path Between Files | does a path `A → B` exist; display it | graph path finding |
| F9 | Save Analysis Report *(optional per synopsis)* | write analysis output to a file for later reference | `fstream` report writing |

### 2.2 Required data structures & concepts (authoritative synopsis mapping)

| Structure | Synopsis role in our system |
|-----------|------------------------------|
| Directed Graph | the core model; adjacency lists store "files I depend on" |
| DFS | chain exploration, deepest path, reachability |
| Stack | DFS frames; cycle detection via on-stack tracking |
| Queue | BFS frontier for impact analysis |
| Hash Table | file name → its dependencies/dependents record; constant-time lookup building & querying the graph |
| Trie | filename prefix search / existence |
| Heap (max) | rank by in-degree → extract top-k most depended-upon |
| Linked List | adjacency list storage per file |

### 2.3 Topics the synopsis commits us to

Graph algorithms · **Recursion** (DFS, cycle detection, deepest chain) ·
**File handling** (read sources, parse includes, optionally save reports) ·
Standard I/O · **Menu-driven** UI.

> Implementation note (**approved decision**): the synopsis lists *Recursion* as a
> required topic and describes the stack as tracking the *recursion stack*.
> Per the team decision (2026-11-07):
> - DFS (cycle detection, reachability, deepest-chain exploration) is implemented
>   **recursively** — recursion is demonstrated explicitly and matches the synopsis
>   wording ("files currently on the recursion stack").
> - A documented **recursion-depth guard** protects against pathologically deep
>   graphs: the program reports "graph too deep to analyze with recursion" and
>   suggests running on a smaller subtree instead of crashing.
> - Our own **Stack** class (a required data structure) is used meaningfully
>   elsewhere: chiefly **path/cycle reconstruction** — a parent chain is pushed
>   onto the Stack and popped to print a dependency path or a cycle in correct
>   edge order (F2/F8). No STL container ever replaces a required custom
>   data structure.

### 2.4 Instructor addendum (NOT in the synopsis — must not be confused with it)

- **Six meaningful BFS variations/applications** — see §5. These implement the
  instructor's addendum and serve synopsis features (they do not add new user
  features). **Provisional:** kept as proposed (B1–B6) per team decision
  2026-11-07; they are *not* claimed to be the instructor's exact mandated
  algorithm list until the instructor confirms it (see §9).
- **Benchmarking** those six variants: execution times + operational counters,
  exported to CSV, honestly reported.

### 2.5 Documented limitations of include analysis (approved, user-facing)

- Angle-bracket includes (`<...>`) are treated as system dependencies — not project edges.
- `#include` directives inside comments are ignored; conditional-compilation
  directives (`#ifdef`, `#if`, ...) are **not evaluated** — an include under any
  branch of a conditional is treated as a real dependency.
- No preprocessor support: macro-*generated* includes (`#include MACRO_NAME`) are
  not resolved.
- Include search uses only the two approved roots (includer's directory, project
  root) — no additional `-I` paths.
- I/O errors and unresolvable targets surface as scan Warnings, never as
  invented edges.

---

## 3. Architectural goals (for a 3-person, II-semester-scope project)

1. **Correctness first:** every algorithm output can be validated by hand on the
   bundled sample project.
2. **Every required DS is load-bearing** — its use must be justified in the viva
   (mapping table above) — no decorative containers.
3. **Layered but simple:** scanning/parsing, graph, analytics, menu are separate
   layers with one-directional dependencies; a second-year student can read any
   file in isolation.
4. **Memory-safe:** RAII, no leaks, ownership explicit and documented.
5. **Reproducible benchmarks:** seeded generative graphs, static CSV schema.
6. **Docs-as-contract:** ARCHITECTURE/DEVELOPMENT_PLAN/TESTING_STRATEGY evolve
   with code; TASK_LOG records every work session.

---

## 4. Module breakdown (high level; detailed interfaces come later)

```
┌────────────────────────────── Console application ───────────────────────────────┐
│  Menu UI (cin/cout, feature selection, trees/levels output, report saving)       │
├─────────────────────────────────────┬────────────────────────────────────────────┤
│  Scanning & Parsing                 │  Analytics engines                         │
│  walk project tree (filesystem)     │  cycle detection (DFS+Stack, SCC)          │
│  read files, extract #include       │  impact analysis (BFS on reverse edges)    │
│  resolve paths → node ids (Hash)    │  topological order (BFS-queue Kahn)        │
│  build graph (Linked List adjac.)   │  deepest chain (DFS/DP on DAG)             │
│  populate Trie names                │  hub ranking (MaxHeap on in-degree)        │
│                                     │  path search (bidirectional BFS)           │
│                                     │  prefix search (Trie)                      │
├─────────────────────────────────────┴────────────────────────────────────────────┤
│  Graph model: nodes (int ids), forward adjacency lists, reverse adjacency lists, │
│               in-degree/out-degree counts, name↔id Hash Table                    │
├──────────────────────────────────────────────────────────────────────────────────┤
│  Custom DS library (pure, self-contained): LinkedList, Stack, Queue, HashTable,  │
│  Trie, MaxHeap, small dynamic numeric arrays                                                     │
├──────────────────────────────────────────────────────────────────────────────────┤
│  Benchmark suite (separate mode): seeded graph generators, steady_clock timing,  │
│  6-BFS comparison runner, CSV writer                                             │
└──────────────────────────────────────────────────────────────────────────────────┘
```

Dependency direction: Menu → Analytics → Graph model → DS library. Scanning
feeds the Graph model and Trie; anything higher never touches scanning internals.

---

## 5. The six BFS variations (instructor addendum)

Direction convention: edge `A → B` = A depends on B. "Forward" = follow includes;
"Reverse" = follow dependents (needed by F3).

| # | Variation | Serves feature | What is different about the *algorithm* | Expected behaviour |
|---|-----------|----------------|------------------------------------------|--------------------|
| B1 | **Single-source BFS (forward)** | reachability view of F1/F5 support: what A transitively depends on | classic FIFO level-by-level | visits O(V+E) |
| B2 | **Multi-source BFS** | F4 topological sort (Kahn's algorithm seeds the queue with **all** in-degree-0 files); also "several files changed at once" impact on reverse edges | multiple seeds at level 0; waves = compile batches | O(V+E), level ordering |
| B3 | **Bidirectional BFS** | F8 path A→B between two files | two frontiers (one forward from A, one reverse from B), expand one LEVEL of the smaller per round with **strict alternation on size ties** (a constant tie rule starves one side — caught in review), meet at the first second-discovery, **choose the meeting vertex with the smallest levelF+levelB sum** (BFS levels are exact distances, so every candidate's sum is a real path length and the earliest second-discovery round always contains a vertex of a shortest path — this keeps the result provably shortest even when one frontier races ahead) | dramatically fewer vertices on hub-rich graphs: O(b^(d/2)) for path length d vs BFS's O(b^d) |
| B4 | **Reverse-graph BFS (single source)** | F3 impact analysis engine: who transitively depends on changed file | BFS over **predecessor** adjacency | O(V+E), levels = distance from changed file |
| B5 | **Bounded-depth BFS** | "look only N hops ahead" mode of F3/F7-like views (direct deps vs. deep groups) | FIFO with per-level counter stopping at maxDepth | O(V + E_k) where E_k = edges within k levels |
| B6 | **Depth-optimizing BFS** | performance mode for B1/B4 on large graphs (benchmark showcase, no new user feature) | starts top-down (push from frontier); when frontier becomes dense switches bottom-up (unclaimed nodes check their predecessors). Beamer–Asanović–Patterson-style "direction-optimizing BFS", implemented as a **simplified per-level push/pull heuristic** (ruling §9.19, approved 2026-11-14) | same asymptotic bound, but *edges examined per claim* drops on graphs with dense frontiers. Levels/visited set provably equal B1; discovery order may differ (documented) |

Why they are **not** just six implementations of one algorithm:

- B1 vs B4 differ by **graph direction** and answer opposite questions (F1-style view vs F3).
- B2 changes **initialization** (multiple roots — Kahn's compile-wave batching).
- B3 changes the **search strategy** (two frontiers with mid-meeting, not one).
- B5 changes the **termination condition** (frozen depth budget), not just view.
- B6 changes the **frontier execution discipline** (push vs pull switching) and gains
  only in a specific regime — this is exactly what benchmarking demonstrates.

**B6 as-built (5.3):** per level, `pushCost = Σ outDegree(wavefront)` (exact) vs
`pullCost = unclaimed × edgeCount / n` (average-case, multiply-before-divide so
sparse graphs don't collapse to 0); pull runs only when **strictly** cheaper,
ties → top-down (the B1-equivalent default). Levels and the claimed set are
provably equal to B1's; discovery order/parents may differ (documented).
Counters: `topDownLevels`/`bottomUpLevels` = levels expanded per mode;
`maxFrontier` = max **wavefront** size (in pull mode there is no queue, so this
is not directly comparable to B1's queue peak — documented).

Each variant is benchmarked **only against variants/alternatives answering the
same question** (fair protocol in §7). Equivalences (e.g., B5 with depth = ∞
must match B1's vertex set; B4 with one changed file must match B2 on that file)
are asserted in tests so comparisons stay honest.

---

## 6. Benchmarking design (instructor addendum)

- **Timer:** `std::chrono::steady_clock` (monotonic — immune to wall-clock changes).
- **Graph material for benchmarks:** synthesized with a seeded generator so every
  size/shape is reproducible and fair across variants. **Approved shape set
  (2026-11-14 — supersedes the earlier illustrative list):** chain, tree-like,
  sparse, dense, hub-heavy — at **1k / 5k / 10k nodes**. All five shapes are
  acyclic by construction (edges point to earlier ids); cycle behaviour is
  exercised by unit tests, not benchmarks. Edge budgets: chain/tree-like
  ≈ V−1, sparse ≈ 2.5/node, **dense capped at ≈ 8/node** (ruling §9.20),
  hub-heavy ≈ 2/node with ~5% hubs holding ~40% of edge mass.
  Real scanned fixtures are benchmarked too (realism cross-check).
- **Metrics per run:** elapsed time (µs), vertices visited, edges examined,
  maximum queue/frontier size reached. These are computed during the traversal,
  not derived afterwards, so they are unit-testable.
- **Methodology:** ≥10 measured trials per (algorithm × graph) after a warm-up,
  drop best/worst leftover outlier handling, report mean/stdev/min/max; rotate
  algorithm execution order between trials to spread cache effects.
- **Output (as-built, schema v1):** `results/benchmarks_raw.csv` — one row per
  timed trial (`schema_version, timestamp_iso, graph_kind, nodes, edges, seed,
  variant, bound, trial, time_us, vertices_visited, edges_examined,
  max_frontier, mode_switches`; `mode_switches` = B6 `bottomUpLevels`, 0 for
  B1–B5; `bound` non-zero only for B5; the classic B3 baseline predates
  counters, so its counter columns are 0 by definition) — plus
  `results/benchmarks_summary.csv` — one row per (variant × graph) with
  `trials_kept, mean_us, median_us, stdev_us, min_us, max_us` over the 8 kept
  trials and the first trial's counters — and `results/benchmark_env.txt`
  (timestamp, build type, compiler, methodology line). `results/` is
  gitignored; CSVs are regenerated by `./build/Release/bench`.
- **Honesty rule:** results only from actual runs; the report never quotes
  numbers that were not produced by the committed benchmark code. Environment
  (machine, build type, flags) recorded alongside.
- **Noise control:** release build with optimization ON; single-threaded only
  (threading is out of scope anywhere in the project — see decision list); same
  graph object for algorithms being compared; nothing else running concurrently
  where practicable.

---

## 7. Planned folder structure (no code yet)

```
dsproject/
├── AGENTS.md ARCHITECTURE.md DEVELOPMENT_PLAN.md TESTING_STRATEGY.md TASK_LOG.md
├── docs/                       # synopsis DOCX (original file, unchanged)
├── CMakeLists.txt              # or Makefile — decided in Phase 0
├── src/
│   ├── main.cpp                # menu bootstrap
│   ├── app/                    # menu, session, output formatting, report saving
│   ├── io/                     # include_parser.h, project_scanner.h (Phase 3,
│   │                           #   as-built: discovery, parsing, resolution,
│   │                           #   summary/warnings; Trie population deferred)
│   ├── ds/                     # pure custom structures (testable in isolation)
│   ├── graph/                  # dependency_graph.h (model, Phase 2, as-built) +
│   │                           #   algorithms (BFS variants, DFS/SCC, topo, chains)
│   └── benchmark/              # generator, runner, timer, CSV
├── tests/
│   ├── test_framework.h        # tiny assert harness (no external deps)
│   ├── ds_tests.cpp graph_tests.cpp io_tests.cpp analysis_tests.cpp
│   └── fixtures/sample_project/ # ~10 files incl. cycles, unresolved include, dup basename
└── results/                    # CSV outputs (git-ignored; never commit invented numbers)
```

---

## 8. Key technical decisions (and their rationale)

| Decision | Rationale |
|----------|-----------|
| Integer node ids; strings only via Hash Table lookup | traversals touch numbers, not filenames — simpler DS and faster algorithms |
| Keep **forward** and **reverse** adjacency lists | F3 (impact), B3 (bidirectional meeting frontier), B4, and in-degree counting for F6 all need reverse edges; building both once at scan time is cheap |
| DFS implemented **recursively** (recursion-topic demo); **Stack** used for path/cycle reconstruction | approved 2026-11-07: recursion is demonstrated explicitly as the synopsis requires; the recurrence guard caps depth and degrades gracefully; the custom Stack is load-bearing for printing a path/cycle in edge order. No STL container replaces any required structure |
| Kahn's algorithm (B2 multi-source) for F4 | standard, uses our Queue, gives topological ordering with a clean cycle-report leftover |
| "Deepest chain" policy **(approved 2026-11-07)** | compute the longest dependency chain when the graph is a **DAG**. If cycles exist → **report that the longest dependency chain is undefined for the current graph** (list cycle groups; do NOT present SCC condensation as a real file-level chain) |
| Include resolution order **(approved 2026-11-07)** | (1) path relative to the including file, (2) path relative to project root; else **unresolved** (reported, edge-less). Limitations vs. compilers' `-I` systems are documented (§2.5) |
| File identity = **project-relative path** (approved 2026-11-07) | **Duplicate basenames remain separate graph nodes**; Trie search returns every basename match with its path |
| **Angle-bracket includes ignored** (`<...>` = system headers) | approved: they are external, not project dependencies |
| Conditional-compilation directives not evaluated | approved: `#include` inside `//`/`/* */` (single-line block) is ignored; `#ifdef` branches are recorded regardless of condition — documented limitation (§2.5) |
| Core scope locked **(approved 2026-11-07)** | the 8 synopsis features + 8 required data structures are the scope; the addendum (6 BFS + benchmark) applies to algorithm depth only; no silent feature additions — new ideas go to TASK_LOG "Proposals" |
| Single binary + unit-test binary, **CMake** (installed via Homebrew) | smallest tooling footprint; a plain `g++` command remains documented as fallback |
| No threading, no design patterns for pattern's sake | benchmark fairness, code simplicity, semester scope |

---

## 9. Decisions approved (locked 2026-11-07) and remaining confirmations

**Approved & locked** (details in §8):
1. Deepest dependency chain: longest chain **only for a DAG**; if cycles exist the
   result is reported **undefined** (cycle groups listed; never an SCC-condensed
   file-level chain).
2. DFS is implemented **recursively**; the custom **Stack** is used meaningfully
   (path/cycle reconstruction); no STL replacement of required structures.
3. Include resolution: includer's directory → project root; `<...>` ignored;
   conditional directives not evaluated — limitations documented (§2.5).
4. Files identified by **project-relative path**; duplicate basenames stay
   separate nodes.
5. The synopsis is a **DOCX**, preserved unchanged at `docs/Project_Synopsis_
   Codebase_Dependency_Analyzer.docx` (folder renamed from `docs ` to `docs/`).
6. Scope locked to the 8 synopsis features + 8 data structures (+ instructor
   addendum); ideas go to TASK_LOG "Proposals" before any addendum becomes a feature.

**Phase 2 rulings (approved 2026-11-07, implemented in `dependency_graph.h`):**
7. `addEdge(a, a)` **records** the self-edge as a real graph edge (ruling B1):
   a self-include is a length-1 dependency cycle that cycle detection (F2) and
   topological ordering (F4) must see; parse-level counts in Phase 3 stay
   separate from edge semantics. Synopsis never specifies self-edge handling —
   this is the documented team choice.
8. `DynArray` gained `emplaceBack` growth (ruling A1, satisfying plan item
   1.7): one generic growable array serves the node table and future algorithm
   tables; no separate `IntArray`/`BoolArray` classes.
9. Out/in degrees are **derived** from adjacency list sizes rather than stored
   as separate fields (single source of truth — nothing to drift).
10. macOS dev-machine limitation: ASan leak detection (LSan) is unsupported
    (`detect_leaks is not supported`); sanitizer runs stay mandatory but
    LSan-based leak checking is impossible locally.
11. Scanner rulings (Phase 3, 2026-11-07): empty-graph precondition asserted
    inside `scanProject` (ruling #5); sorted-path node ids (ruling #6);
    Unresolvable (macro/malformed) ≠ unresolved-quoted (ruling #2); every
    unresolved directive counts (ruling #3); extension/skip matching exact
    strings; comment markers inside targets are literal (ruling #1). Trie
    population deferred until Phase 4/7 (approved deviation); `main.cpp`
    menu untouched until Phase 7 (approved deviation (b)).

**Phase 5 rulings (approved 2026-11-14, planning locked before implementation):**
19. **B6 direction-optimizing BFS** uses a **simplified per-level top-down /
    bottom-up heuristic** (documented divergence from the full Beamer cost
    model). The heuristic alone decides switching — tests verify correctness
    (level array ≡ B1) and counter consistency, never a forced direction.
    B6's determinism guarantee is therefore levels/visited-set equality with
    B1; discovery order may differ (documented per variant).
20. **Benchmark shape set locked:** chain, tree-like, sparse, dense,
    hub-heavy at 1k/5k/10k nodes (supersedes §6's earlier illustrative list;
    all acyclic by construction); dense capped at ≈ 8 edges/node.
21. **Phase 4 stays untouched:** `analysis.h` is not refactored to delegate to
    the new BFS core; its functions serve as **test oracles** (B4 ≡
    `impactOf` levels; B3 length ≡ `dependencyPath`). Phase 5 code reuse is
    internal to bfs.h: one shared core backs B1/B2/B4/B5; B3 and B6 are
    separate engines.
22. **B3 is unbounded only** — no hop-budget knob in API, counters, tests or
    docs (revised from the original plan proposal).
23. **Equivalence roles:** B2 with one seed ≡ B1 (forward view);
    B4 ≡ `analysis::impactOf` (reverse view). These replace the earlier
    muddled pairing in TESTING_STRATEGY §7.
24. **Benchmark scaffolding moved into Phase 5** (user request 2026-11-14):
    generators + runner + CSV ship with the variants; Phase 6 shrinks to the
    final frozen run, environment recording, and reporting polish (menu wiring
    remains Phase 7).
25. **Final Phase 5 planning corrections (2026-11-14, same session):**
    (a) TraversalReport's level/parent result tables are **`ds::DynArray<int>`**,
    not `std::vector` — consistent with Phase 4's internal tables and the
    custom-DS requirement; reports are move-only; −1 initialisation via
    explicit loops (DynArray has no `assign` method; API verified sufficient:
    value-init size ctor, `operator[]`, `size()`, noexcept move).
    (b) Benchmark equivalence wording corrected: **B2 with one seed ≡ B1**;
    **B4 ≡ `analysis::impactOf` levels ≡ B1 over a manually reversed graph**;
    the earlier "B4 ≡ B2-restricted" phrasing is wrong and removed.
    (c) Chain generator orientation fixed: **node i+1 depends on node i** so
    edges point to earlier ids (acyclic-by-construction rule).

**Phase 4 rulings (approved 2026-11-09, implemented in `analysis.h`):**
12. **No Tarjan/SCC (R1):** F2 reports *cycle instances only* — closed paths
    (first node repeated last) reconstructed via the DFS-tree parent chain on
    the custom Stack. The report cap (default 16) bounds **stored instances,
    not detection**: `cyclesFound` counts every back edge and the DFS keeps
    walking. Cycle-blocked/topo-leftover wording (F4) is a *blocked* set: it
    includes nodes transitively DEPENDING on a cycle member, not just members.
13. **DFS recursion guard (R2):** recursive DFS with `maxDepth`, default
    10 000 (parameterized so tests can force a trip). A tripped guard means
    "results partial — no full verdict"; it never crashes and never publishes
    partially computed results. For F5 the answer becomes undefined +
    `depthGuardTripped` (review #3); in the standard configuration the step-2
    guard branch is defensively unreachable because step 1 runs the same
    `findCycles` check first (documented behavior).
14. **Impact level 0 = the changed file itself (R3):** dependents start at
    level 1; −1 = unaffected; `affectedCount` includes the changed file.
    Impact flows over **predecessors** (who includes me) only.
15. **F8 `from == to` (R4):** found, path `[from]` (single entry).
16. **F7 empty prefix → empty result (R5);** the Trie returned by
    `buildNameIndex` is **caller-owned** and stores no graph reference
    (no coupling — review ruling #4). Prefix search is byte-exact case
    sensitive, keyed on basenames.
17. **F6 hub ranking deterministic ties:** in-degree desc, then ascending
    fileId (⇒ lexicographic path order via sorted id assignment); heap built
    with O(V) `buildFrom` heapification, not repeated inserts.
18. **Determinism rule for all analytics:** traversals seed/visit in node-id
    order, adjacency lists keep insertion order, and every result vector is
    caller-owned (AGENTS §5) — all outputs reproducible.

**Remaining confirmations (not implementation blockers):**
1. **B1–B6 remain provisional.** They implement the instructor's "six meaningful
   BFS variations/applications" addendum; if the instructor's exact list differs,
   the menu labels and benchmark naming must be adjusted (before Phase 5 sign-off).
   We do **not** claim these six are the teacher's mandated exact algorithms.
2. Benchmark presentation format for viva (CSV schema details / slides) — decided
   at demo prep time.
3. Lab-machine C++17 support for `std::filesystem` — assumed; verify at first
   in-lab run.

---

## 10. What is deliberately NOT designed in this document

Class Catalogue (class names, member functions, signatures), ownership diagrams,
memory-model prose, precise CSV schemas, per-module headers. Those follow **after
this document is approved** (next planning step), so that implementation never
pre-empts a design conversation.
