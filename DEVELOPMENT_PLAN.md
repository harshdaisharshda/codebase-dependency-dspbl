# DEVELOPMENT_PLAN.md — Codebase Dependency Analyzer

**Status:** v2.1 (planning; grounded in the synopsis `docs/Project_Synopsis_Codebase_Dependency_Analyzer.docx` + instructor addendum: six BFS variations, benchmarking; team decisions approved 2026-11-07).
Effort units below are rough **2–3 hour work sessions** for the 3-person team
(Akshay, Harshda, Arnav); have no calendar dates on purpose.

---

## Guiding rules for this plan

- Each phase ends with the `unit-tests` binary passing **and** a TASK_LOG entry.
- Nothing merges into the dependency graph unless ARCHITECTURE.md already describes it.
- Data structures are built **first and in isolation** (Phase 1) because every
  later phase depends on them; they cannot be "fixed later".
- No code is written until ARCHITECTURE.md v2.0 is approved by the team/teacher.

---

## Phase 0 — Project setup   *(1–2 sessions) — DONE 2026-11-07*

Tasks
- [x] Create the folder skeleton exactly as in ARCHITECTURE.md §7.
- [x] Build tool chosen: **CMake** (installed via Homebrew; a plain `g++` command
      remains the documented fallback). Both `dep-analyzer` and `unit-tests` build
      with `-Wall -Wextra`, Debug (with `-fsanitize=address,undefined`) and Release.
- [x] `git init` + first commit (`18dca71`). `.gitignore` covers build/, results/,
      .DS_Store. (Housekeeping done earlier: synopsis folder
      is already renamed `docs /` → `docs/`, file preserved unchanged.)
- [x] `tests/test_framework.h` written (tiny assert harness — no external deps).
- [x] `tests/fixtures/sample_project/` created per TESTING_STRATEGY.md §5
      (12 files: one 2-cycle, one unresolved include, one duplicate basename,
      one commented include, one angle include).

Exit criteria — all met (2026-11-07):
- `dep-analyzer` and `unit-tests` build from one command in Debug and Release.
- `unit-tests` passes (1 self-check test) in both configs; ASan/UBSan clean.
- Menu tolerates garbage input, exits cleanly on 0 and on EOF (sanitizer clean).

---

## Phase 1 — Custom data structures   *(4–5 sessions) — DONE (close-out approved 2026-11-07)*

Build order follows dependency: objects needed by later phases come earlier.

| # | Structure | Notes / pitfalls to test for |
|---|-----------|------------------------------|
| 1.1 | `LinkedList` (template) | **DONE 2026-11-07.** O(1) append via tail pointer; forward const iterator; copy deleted / move steals; `removeIf` for hash-bucket unlinking; empty & single-element, pop-drain, 10k stress — 11 tests green in Debug (ASan/UBSan) & Release |
| 1.2 | `Stack` (template) | **DONE 2026-11-07.** Thin facade over the custom `LinkedList` (top == list head; zero new/delete logic duplicated): all ops O(1); underflow = assert per project policy; empty/single/multi, LIFO, top-no-remove, drain-reuse, clear-reuse, move-then-reuse, 10k stress — 9 tests green in Debug (ASan/UBSan) & Release |
| 1.3 | `Queue` (template) | **DONE 2026-11-07.** Thin facade over the custom `LinkedList` (per team design decision — no ring buffer this version): `enqueue`=O(1) tail append, `dequeue`=O(1) popFront, `front`/`size`/`isEmpty` O(1), `clear` O(n); underflow = assert per project policy; FIFO, empty/single/multi, front-no-remove, drain-reuse, clear-reuse, move-then-reuse, **interleaved ops**, 10k stress — 10 tests green in Debug (ASan/UBSan) & Release |
| 1.4 | `HashTable` (string→int, separate chaining, Linked List buckets) | **DONE 2026-11-07 (upgraded to template `HashTable<V>` per team instruction).** FNV-1a 64-bit (public `hashFor` for deterministic collision tests), separate chaining on `ds::LinkedList`, buckets in new `ds::DynArray` primitive (no STL), integer-exact rehash at `count*4 > buckets*3` (doubling), duplicate insert rejected w/o overwrite, `find`→`const V*` distinguishes absent vs stored zero, moved-from tables reusable. 18 tests incl. collision chains + 10k stress → **48 tests, 20 293 checks green** Debug (ASan/UBSan) + Release. Fixes found by tests: value-init in DynArray ctor; hand-written HashTable move (defaulted move copied `count_`, breaking moved-from invariant) |
| 1.5 | `Trie` | **DONE 2026-11-07.** Keyed by **basename** (so same-basename files in different dirs share one terminal and BOTH ids return for prefix "api" — user's requirement). Children = own `LinkedList<{char, Node*}>` (documented: no STL container, and a 128-slot array ≈1 KB/node was rejected for memory). Terminal ids deduplicated at insert; `searchPrefix` dedups across names; byte-exact case sensitivity; remove(name,id) drops one id w/o node pruning (clear() on re-scan); moved-from reusable. 13 tests incl. deep chains + 10k shared-prefix stress → **61 tests, 30 375 checks green** Debug (ASan/UBSan) + Release |
| 1.6 | `MaxHeap` (score,id) | **DONE 2026-11-07.** Array-backed via `ds::DynArray<Entry>` + separate logical `count_`; ordering: higher score first, ties → smaller fileId (matches sorted-scan id assignment ⇒ deterministic tie-break at graph level). **Bottom-up heapify `buildFrom` included** — justified: all V in-degrees known at once in F6, O(n) vs O(n·log n). Hand-written move (defaulted move would copy `count_` — known Phase 1.4 trap). 10 tests incl. heap-invariant walk, buildFrom≡insert equivalence (n=4000), 10k stress → **71 tests, 48 441 checks green** Debug (ASan/UBSan) + Release |
| 1.7 | Small dynamic numeric arrays (`IntArray`, `BoolArray`) | used for dist/parent/visited/color tables. **Superseded 2026-11-07 (approved ruling A1):** `DynArray` gained generic growth (`emplaceBack`, amortized doubling, move-only-safe) in Phase 2.0 and serves as the single growable-array primitive; Phase 4 algorithm tables will be plain `DynArray<int>`/`DynArray<bool>` — no separate classes needed. |

Unit tests (per structure, plus cross-structure polymorphic use in graph in Phase 2):
empty, single item, duplicates, resize/rehash/wrap, ties, worst-case collisions,
iterator semantics, copy/move discipline. Leak check with sanitizers.

Exit criteria
- All containers pass their unit tests; no ASan/UBSan findings; no leaks.

---

## Phase 2 — Dependency graph model   *(2–3 sessions) — DONE 2026-11-07*

- [x] Node table: `path`, `name`, `unresolvedIncludes` (`graph::DependencyGraph::Node` — out/in degrees are
      **derived** from the adjacency list sizes instead of stored twice: one source of truth).
- [x] `addNode` (dedup via HashTable → stable id 0..V-1, never renumbered),
      `addEdge` (no duplicate edges; **self-edges recorded** per approved ruling B1
      — a self-include is a real length-1 cycle; out-of-range ids rejected without
      mutation, report-and-skip), `finalize()` (reverse adjacency in O(V+E); idempotent;
      structural mutation invalidates the finalized state, no-ops do not).
- [x] Accessors: `successors`/`predecessors` (predecessors + `inDegree` assert
      `isFinalized()`), `outDegree`, `node`, `findId`, `edgeCount`, `nodeCount`,
      `isFinalized`, `clear` (full reset — ids restart by design for re-scan).
- [x] In-memory graph construction **by hand in tests** (`tests/graph_tests.cpp`,
      added to the `unit-tests` target) — no filesystem involvement.
- [x] Support change (approved ruling A1): `ds::DynArray` gained `emplaceBack`
      with amortized-doubling, move-only-safe growth (see TASK_LOG Phase 2.0);
      DEVELOPMENT_PLAN item 1.7 thereby satisfied by the generalized primitive.

Exit criteria — ALL MET:
- Known 7-node hand-authored graph (chain + diamond + disconnected node)
  round-trips exactly: ids, both degrees per node, Σin = Σout = E, and every
  edge mirrored in the reverse lists, in insertion order.
- Duplicate / out-of-range / self edges behave per spec; counts verified.
- 2000-node / 5000-edge stress: counter == accepted edges, mirror invariant
  holds edge-by-edge (a sanity check, explicitly not a benchmark).
- 86 tests / 51 820 checks green in Debug (ASan/UBSan) + Release, 0 warnings.

---

## Phase 3 — Source scanning & `#include` parsing   *(2–3 sessions) — DONE 2026-11-07*

- [x] Directory walk with `std::filesystem::recursive_directory_iterator`,
      extension filtering (`.h .hpp .hh .c .cc .cpp .cxx` — lowercase exact,
      `HEADER.CPP` skipped by design), skip-list (`.git/`, `build/`, `results/`,
      exact names), **sorted deterministic order** (paths sorted before
      `addNode`; `skip_permission_denied` + increment-error recovery; warnings
      never abort the scan — Phase 3 review ruling #4).
- [x] Line parser (`src/io/include_parser.h`): `#include "..."` extraction only;
      `//` and multi-line `/* */` comments respected (markers INSIDE quoted
      targets are literal, ruling #1); **documents** why `#ifdef` is not
      evaluated; directive must start the line, one directive per line;
      counted separately: quoted / angle / unresolvable (macro-form or
      malformed — NOT node-counter increments, ruling #2).
- [x] Resolution order (per ARCHITECTURE.md §8):
      1. relative to the including file's directory,
      2. relative to the project root,
      3. otherwise → `addUnresolvedInclude` + `ResolutionIssue` + warning
      (warn, no edge; repeats each count — ruling #3). Resolution matches
      scanned nodes only; never invents nodes or edges.
- [x] Builds the graph from Phase 2 (empty-graph precondition asserted,
      ruling #5); self-includes = real edges + `selfIncludes`; Trie
      population deferred until its feature needs it (approved deviation (a)).
- [x] Scan summary stats: files scanned, quoted/angle/unresolvable include
      counts, unresolved count, self-includes.

Exit criteria — ALL MET:
- Scanning `tests/fixtures/sample_project` produces the exact graph assumed by
  algorithm tests: explicit **12 nodes / 9 edges**, full edge-list equality
  (`main→{api/api.h, utils/utils.h}`, `api/api.cpp→api/api.h`,
  `api/api.h→database/database.h`, `graph.cpp→{graph.h, shared/util.h}`,
  `utils/utils.h→shared/util.h`, `cycle_a.h↔cycle_b.h`), 1 unresolved
  (`missing.h`), 1 ignored angle include, 1 ignored commented include, sorted
  id round-trip, Σin = Σout = 9 mirror verified.
- Non-existent folder → returns false, graph untouched and unfinalized
  (clean fatal-error path).
- 100 tests / 51 973 checks green in Debug (ASan/UBSan) + Release, 0 warnings.

---

## Phase 4 — Core graph algorithms & features (synopsis F1–F8)  *(4–5 sessions) — DONE 2026-11-09*

4.1 Cycle detection (**DFS + Stack**): 3-color recursive DFS with a depth guard
    (default 10 000 per review ruling R2). Reports closed **cycle instances**
    (first node repeated last; storage capped at `maxReportedInstances`, default
    16) **without capping detection** (`cyclesFound` counts every back edge —
    review ruling R1: no Tarjan/SCC; cycle instances only).
    ~~node->SCC-id map (Tarjan, iterative)~~ **amended as-built per R1.**
4.2 Topological order (**Queue / multi-source BFS-style Kahn**): seeds = all
    out-degree-0 nodes in id order (dependencies compile first); leftovers
    = `cycleBlocked` (nodes on a cycle **and** nodes transitively depending on
    one — a "blocked" set, not cycle membership; F2 lists instances).
4.3 Impact analysis (**Queue / reverse BFS**): all transitive dependents of a file,
    levels = distance; **level 0 = the changed file itself** (ruling R3), −1 =
    unaffected.
4.4 Deepest dependency chain (**recursive memoized DFS**): longest chain when the
    graph is a DAG; **if cycles exist the result is undefined** (`firstCycle`
    lists one closed instance; never an SCC condensation). A tripped guard
    marks the result **incomplete** — no partially computed chain is published
    (review ruling #3/F5 policy).
4.5 Highly connected modules (**MaxHeap**): `buildFrom` O(V) heapify of all
    in-degrees (bulk justification); extract top-k; ties → smaller fileId.
4.6 Prefix search (**Trie**): basename index built by `buildNameIndex`
    (caller-owned; no graph reference stored — ruling R4 in review #4); prefix
    search + basename existence; empty prefix → empty result (R5).
4.7 Path between two files (**classic forward BFS** now; Phase 5 B3 bidirectional
    variant benchmarks against exactly this fallback).
4.8 Dependency tree/levels **console output formatting** — **DEFERRED to
    Phase 7** (menu/console layer, as planned).

Exit criteria — ALL MET (2026-11-09): one test per feature class on the fixture
project with hand-computed answers, covering: acyclic, 2-cycle, 3-cycle,
self-include, disconnected, diamond, duplicate basenames, report-cap semantics,
depth-guard semantics, and a Layer-4 scanner→algorithms integration test.
**131 tests / 52 275 checks green** in Debug (ASan/UBSan) + Release, 0 warnings.

---

## Phase 5 — Six BFS variations (**instructor addendum**)  *(2–3 sessions) — PENDING*

Implement both directions where relevant and make each variant reachable from the
menu (corresponding synopsis features) and from the benchmark runner.

| # | Variation | Library name | Used by (feature) | Verification test |
|---|-----------|--------------|--------------------|--------------------|
| B1 | Single-source BFS forward | `bfs_forward` | transitive-deps view | equals DFS reachability set on DAGs |
| B2 | Multi-source BFS | `bfs_multi_source` | Kahn's topo (F4) + "several changed files" impact | equals sequential single-source union for level-consistency |
| B3 | **Bidirectional BFS** | `bfs_bidirectional` (two frontiers, meet middle) | F8 path A→B | path length equals classic BFS path length; both agree or neither finds a path |
| B4 | Reverse-graph BFS | `bfs_reverse` | F3 impact engine | equals BFS over manually reversed graph |
| B5 | **Bounded-depth BFS** | `bfs_bounded` | depth-limited impact/deps views | `depth = ∞` result == B1 (and == B4 in reverse mode) |
| B6 | **Direction-optimizing BFS** | `bfs_direction_optimizing` | performance mode for B1/B4 on large graphs | vertex set always equals B1; switch counter (`mode` changes) sanity-checks |

Fairness: each variation measured against its natural fallback (B3 vs classic BFS
pathfind; B6 vs B1 classic push BFS; B2 vs repeated single-source), because B6 vs
B3 etc. aren't comparable in semantics (see §7 in ARCHITECTURE.md).

Exit criteria — all equivalence tests pass; all six are callable from the
benchmark runner with counter output.

---

## Phase 6 — Benchmarking suite (**instructor addendum**)  *(2–3 sessions) — PENDING*

- [ ] Generators (seeded): sparse random / dense / layered DAG / cycle-rich / tree+chords.
- [ ] Runner: warm-up + ≥10 trials, drop min/max, mean/median/stdev; rotated
      algorithm order per trial; single-threaded.
- [ ] Metrics: `time_us`, `visited`, `edges_examined`, `max_frontier`.
- [ ] CSV writer (raw + summary; documented schema); writes to `results/`.
- [ ] Menu item 12 (benchmark) → run on current graph OR on generated suite;
      print environment info (build, flags, CPU) into CSV/report.

Exit criteria
- Sanity: on a known graph, visited counts match passed unit tests exactly;
  timing values are plausible (e.g., >0, scale upward with size on the largest
  dense case) — **smoke check only; no timing assertions**.
- CSV file opens cleanly; one row per (graph × algorithm × trial); environment
  recorded. Results **must come from real runs**; docs/TASK_LOG record machine/setup.

---

## Phase 7 — Menu integration, report output, polish  *(2–3 sessions) — PENDING*

- [ ] Full menu flow (per ARCHITECTURE.md: menu prompt, feature selection,
      results, loop). Garbage input handled → re-prompt.
- [ ] Synthesized outputs: dependency tree (indented like the synopsis example),
      levels "0 h → 1 hop → 2 hops", cycles listing, impact levels, hub ranking
      tables, path display, unresolved includes list.
- [ ] Report saving (`F9`, optional synopsis feature): write menu analysis
      results to a chosen `.txt` file; graceful failure when not writable.
- [ ] UX polish: meaningful prompts; "0" exit; load statistics after scan;
      scan-to-loaded flow; no crash on empty graph/`no files` cases.

Exit criteria — manual CLI checklist (in TESTING_STRATEGY.md) all pass; unit-tests still green; no compiler warnings in Release.

---

## Phase 8 — Testing, documentation, submission prep  *(2–3 sessions) — PENDING*

- [ ] Full unit test suite audit; add anything missing per TESTING_STRATEGY.md checklist.
- [ ] Integration test: one per synopsis feature on the fixture (already in Phase 4)
      + at least one **menu scripted flow** test.
- [ ] Update README with build/run/test/benchmark instructions.
- [ ] Freeze benchmarks: run final suite, record environment in TASK_LOG; results
      committed only from real runs.
- [ ] Final documentation pass: ARCHITECTURE.md reflects **as-built**; sync
      open-questions resolutions; viva demo narrative (which DS answers which feature).
- [ ] viva dry-run: each teammate can explain their modules' DS choices; both team
      members can run both binaries blind.

Exit criteria — "Definition of Done" checklist (below) all ☑.

---

## Definition of Done (project-level)

- All 8 synopsis features (incl. optional F9) work on the fixture project and the
  menu loads them from a real project folder.
- Six BFS variations implemented, equivalence-tested, benchmarked; CSV exists.
- Zero compiler warnings (`-Wall -Wextra`), zero ASan/UBSan findings, green
  `unit-tests` in Debug & Release.
- Every custom data structure is load-bearing (documented role) and unit-tested.
- Docs (ARCHITECTURE/TESTING_STRATEGY/DEVELOPMENT_PLAN/TASK_LOG) truthful and
  as-built; no fabricated benchmark numbers.

---

## Risks & mitigations

| Risk | Mitigation |
|------|------------|
| "Deepest chain" on cyclic graphs ambiguous | policy chosen in ARCHITECTURE.md; get instructor sign-off before Phase 4.4 |
| Instructor's BFS variant list differs from our 6 | confirm before Phase 5; our six were chosen to be definitional variants, not re-implementations |
| Grading machine lacks C++17/filesystem | Phase 0 decision point; fallback documented |
| Benchmark numbers questioned in viva | honest-figures rule, env-logged runs, generated graphs reproducible by seed |
| Team member availability | every phase independently verifiable; DS layer + algorithms delegated to pairwise-reviewed unit tests |
| ~~Folder name `docs ` (trailing space) confusing tooling~~ | RESOLVED 2026-11-07: renamed to `docs/`, synopsis preserved unchanged |
