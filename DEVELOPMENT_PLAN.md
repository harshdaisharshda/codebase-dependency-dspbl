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

## Phase 5 — Six BFS variations (**instructor addendum**) + benchmark scaffolding  *(3–4 sessions) — planning locked 2026-11-14 (rulings §9.19–25); 5.1 DONE 2026-11-14 (`47e2a95`); 5.2 DONE 2026-11-15 (`e96bf99`); 5.3–5.5 DONE 2026-11-15*

One shared core (`runCore`) backs B1/B2/B4/B5 — **5.1 shipped**: `explore`,
`exploreMultiSource`, `exploreReverse`, `exploreBounded` over one engine
(ds::Queue frontier, DynArray level/parent tables, counters sampled during
traversal). Results follow AGENTS §5's caller-owned-arrays rule:
`ds::DynArray<int>` level/parent tables (−1 fills via explicit loops;
reports are move-only). **5.2 shipped**: `bidirectionalPath` — unbounded
level-synchronized two-frontier search (smaller side per round, strict
alternation on size ties), first-second-discovery meeting with smallest-sum
candidate selection (provably shortest without any balance assumption),
Stack-based edge-order reconstruction, combined `|F|+|B|` maxFrontier.
B6 shipped as its own engine (5.3): `exploreDirectionOptimizing` — per-level
push/pull decision, `pushCost = Σ outDegree(wavefront)` vs
`pullCost = unclaimed × edgeCount / n` (multiply-before-divide), pull only
when strictly cheaper, ties → top-down; levels/claimed set ≡ B1 (asserted on
hand-built and all five generated shapes); wavefront held in `DynArray` +
membership bitmap (no Queue — the structure is deliberately different from
B1's); wavefront-based `maxFrontier` (not B1's queue peak).
**5.4 shipped**: generators (`src/bench/graph_generators.h` — SplitMix64 RNG,
exact edge budgets with retry-on-duplicate, acyclic by construction, asserted
non-empty output graph) and the runner (`src/bench/benchmark_runner.h` +
`src/bench/bench_main.cpp`, `bench` CMake target): 15 graphs × 7 variants,
warm-up 1 + 10 trials, drop min/max → stats over 8, rotated variant order per
trial, untimed graph construction/validation (`findCycles` acyclicity + B3 vs
classic reachability agreement), counter drift across trials asserted in the
runner (determinism, not noise), steady_clock, Release-only runs. Output:
`results/benchmarks_raw.csv` (1050 trial rows), `results/benchmarks_summary.csv`
(105 rows), `results/benchmark_env.txt` — **schema v1**, columns per
ARCHITECTURE §6. First Release run (2026-11-15, Apple LLVM 21.0.0): recorded
in TASK_LOG; `b3_classic_baseline` counter columns are zeros by definition
(Phase 4 API predates counters).
`analysis.h` stays untouched and serves as the test oracle (ruling §9.21).

| # | Variation | Library name | Used by (feature) | Verification test |
|---|-----------|--------------|--------------------|--------------------|
| B1 | Single-source BFS forward | `bfs_forward` (`explore`) | transitive-deps view | reachability set ≡ per-node `dependencyPath(seed, v)` on small graphs |
| B2 | Multi-source BFS | `bfs_multi_source` (`exploreMultiSource`) | Kahn's topo (F4) + "several changed files" impact | ≡ sequential single-source union (level = nearest seed); **one seed ≡ B1** (ruling §9.23) |
| B3 | **Bidirectional BFS** | `bfs_bidirectional` (`bidirectionalPath`) | F8 path A→B — **unbounded only, no hop budget** (ruling §9.22) | path length equals classic-BFS (`analysis::dependencyPath`) length; both agree or neither finds a path |
| B4 | Reverse-graph BFS | `bfs_reverse` (`exploreReverse`) | F3 impact engine | levels ≡ `analysis::impactOf` (oracle) and ≡ B1 over a manually reversed graph |
| B5 | **Bounded-depth BFS** | `bfs_bounded` (`exploreBounded`) | depth-limited impact/deps views | `depth = ∞` result ≡ B1; bounded cutoff + `boundHit` exact |
| B6 | **Direction-optimizing BFS** | `bfs_direction_optimizing` (`exploreDirectionOptimizing`) | performance mode for B1/B4 on large graphs | level array ≡ B1 always; mode counters **consistent** (Σ levels == levels expanded); the simplified per-level heuristic alone decides switching — tests never force a direction (ruling §9.19) |

Counters (computed during traversal, never derived afterwards): vertices
visited, edges examined, max frontier size; asserted numerically on known
graphs (chain-of-5, star, diamond, disconnected, self-loop seed). B3 counters
cover both frontiers; B6 adds top-down/bottom-up level counts.

Benchmark scaffolding (absorbed from Phase 6, ruling §9.24):
- `src/bench/graph_generators.h` — seeded, deterministic, unit-testable;
  shapes chain / tree-like / sparse / dense (≈8 edges/node cap) / hub-heavy
  at 1k, 5k, 10k nodes; acyclic by construction.
- `src/bench/benchmark_runner.h` + `bench` binary — warm-up + 10 trials per
  (variant × graph), drop min/max, mean/median/stdev of 8; rotated variant
  order; single graph object; steady_clock; Release only.
- Pairings per fairness rule: **B1 vs B6** (full traversal cost on the same
  graph/seed); **B3 vs `analysis::dependencyPath`** — the existing Phase 4
  implementation is the shortest-path baseline; **B5 is benchmarked only at
  meaningful bounded depths** (unlimited B5 ≡ B1 is an equivalence asserted
  in tests, so no separate unlimited-depth timing comparison). Other
  equivalences are asserted in tests, not benchmarked: **B2 with one seed
  ≡ B1**, and **B4 ≡ `analysis::impactOf` levels ≡ B1 over a manually
  reversed graph** (ruling §9.23; the earlier "B4 ≡ B2-restricted" phrasing
  was wrong and is removed).
- CSV output to `results/` (raw + summary, documented versioned schema).
- Generator orientation rule: chain = **node i+1 depends on node i** (edges
  point to earlier ids, per the acyclic-by-construction rule).

Exit criteria — all equivalence tests pass; counters exact on known graphs;
all six callable from the benchmark runner with counter output; CSV opens
cleanly; benchmark numbers recorded from real Release runs only.
**All met 2026-11-15** (171 tests green Debug ASan/UBSan + Release, 0
warnings; 1050 timed executions from the real Release run; CSV row counts
exact).

---

## Phase 6 — Benchmarking suite (**instructor addendum**)  *(1 session) — RESCOPED 2026-11-14; DONE 2026-11-15*

Generators, runner discipline, counters and CSV now ship with Phase 5
(ruling §9.24). Phase 6 keeps only the final deliverables:

- [x] Freeze benchmarks: final frozen run on all 15 synthetic graphs
      (5 shapes × 1k/5k/10k) + the scanned fixture; record environment
      (machine, build type, flags) into the CSV meta/report.
      **Done 2026-11-15**: `results/frozen/` (raw 1120 rows, summary 112
      rows, env record) — dedicated directory, Phase 5's `results/` files
      untouched. The fixture runs as its own case (`fixture_sample_project`,
      seed column 0), same deterministic endpoint rules as the synthetic
      cases. Frozen-run means: dense/10000 B1 144.8 µs vs B6 94.5 µs;
      chain/10000 B3 bidirectional 452.5 µs vs classic baseline 546.5 µs;
      B6 `bottomUpLevels = 0` on all 16 instances (pull never fired — honest
      limitation, see ARCHITECTURE §6).
- [x] Confirm results/ CSV schema docs match the as-built writer.
      Schema v1 verified field-by-field: raw 14 columns × 1121 lines,
      summary 18 columns × 113 lines, no malformed rows, mean ∈ [min, max]
      on all 112 summary rows.
- [x] Results committed only from real runs; TASK_LOG records machine/setup
      (Apple M5, arm64, 10 cores, macOS 26.6.2, Apple LLVM 21.0.0).

Reproducibility verification (2026-11-15): a second independent Release run
(`results/frozen_verify`, since deleted) produced **identical values in every
non-timing column** of both CSVs — schema, graph metadata, seeds, variant,
bound, trial and all four counters (fixture counters included). Timings are
the only columns that vary, as expected.

Runner corrections made in Phase 6 (genuine issues, documented):
1. **Asserts active in the bench binary** — `-UNDEBUG` for the `bench`
   target only: the runner's correctness checks (acyclicity of synthetic
   shapes, B3-reachability agreement, counter drift across trials) previously
   compiled out under `NDEBUG`, i.e. they did NOT run where the results are
   produced. All checks sit outside the timed region.
2. **Acyclicity validation scoped to synthetic shapes** — the Debug run
   caught that the bundled fixture deliberately contains the
   `cycle_a.h ↔ cycle_b.h` pair; scanned projects are NOT assumed acyclic.
   BFS handles cycles via discovery guards (asserted since Phase 5.1), so
   the cyclic fixture is valid benchmark input; the B3 reachability-agreement
   check applies to every case.
3. **Build type recorded from CMake** — after (1), `#ifdef NDEBUG` could no
   longer serve as a Release/Debug proxy inside bench sources; the env record
   now gets `BENCH_BUILD_TYPE`/`BENCH_OPT_FLAGS` from CMake verbatim.

Exit criteria — **all met**: fixture counters match the unit-tested exact
values (B1 3 visited / 2 edges / frontier 2; B2 5/3/3; B3 4/3/3); timings
plausible (>0, scale with size on the largest dense case); environment
recorded; no invented numbers anywhere.

---

## Phase 7 — Menu integration, report output, polish  *(2–3 sessions) — DONE 2026-11-15 (as-built per the approved Phase 7 instruction list)*

- [x] Full menu flow: numbered 1–9 + 0 menu exactly per the approved layout;
      banner once, menu per iteration; garbage → "Please enter a number.";
      EOF exits cleanly; unknown items reported. **Not in the approved
      instruction list and therefore NOT implemented: `F9` report-to-file
      (synopsis optional feature) — deferred to a future approval.**
- [x] Option 1 Scan: directory prompt (empty line = bundled fixture via
      `bench::locateFixtureRoot`); scan into fresh locals, live state
      replaced only on success; summary incl. unresolved list + warnings.
- [x] Option 2 View: numbered `id: path` list + directed edges
      `A -> B (A depends on B)`.
- [x] Option 3 Explore: file picker → `bfs::explore` (B1); levels grouped,
      unreachable count, traversal counters.
- [x] Option 4 Impact: `analysis::impactOf` (F3); level 0 = changed file,
      affected count.
- [x] Option 5 Path: `bfs::bidirectionalPath` (B3); hop count, edge-order
      route, from==to and no-path cases.
- [x] Option 6 Prefix: production F7 path (`buildNameIndex` + `filesWithPrefix`
      + `fileExistsByBasename`).
- [x] Option 7 Statistics: counts (files/edges/unresolved/unresolvable/self/
      warnings), F2 cycles (+instances), F6 top-10 in-degree ranking, F5
      deepest chain, F4 compile order (first 10 when longer).
- [x] Option 8 DS demos: all seven structures demonstrated LIVE on the real
      `src/ds` objects — LinkedList arrow diagrams; Stack/Queue discipline
      traces; HashTable size/buckets/load factor + real placement via the
      published `hashFor` (shows actual collision chains); DynArray
      size/capacity checkpoints (16→32); Trie real branch tree via the one
      Phase 7 API addition (`Trie::visitNodes`, read-only DFS diagnostic view,
      same precedent as `MaxHeap::rawEntryAt`); MaxHeap live tree levels via
      `rawEntryAt` + extraction order.
- [x] Option 9 Benchmarks: the Phase 5/6 runner untouched, writes to
      `results/menu_run/` (NOT `results/frozen/`); Debug builds print a
      "timings not citable" warning.
- File picker: exact path via `findId`, else basename prefix via the Trie
  index; multi-match numbered sub-selection; empty line cancels; garbage
  re-prompts.

Corrections made in Phase 7 (genuine issues, documented):
1. **`parseMenuChoice` out-of-bounds read** (Phase 0 latent bug, exposed by
   the new scripted tests under ASan): `line[digitStart - 1]` underflows when
   digits start at position 0 → sign is now tracked explicitly while
   scanning.
2. Menu layers unit-tested for the first time: `tests/app_tests.cpp` (9
   scripted tests over `app::run(istream, ostream)`).

Exit criteria — **all met**: menu checklist verified against the fixture
(options 1–8 scripted, 9 run from the Release binary; `results/frozen/`
checksum-verified untouched); 181 tests green in Debug (ASan/UBSan) +
Release; 0 warnings at `-Wall -Wextra`.

---

## Phase 8 — Testing, documentation, submission prep  *(2–3 sessions) — DONE (2026-11-15)*

- [x] Full unit test suite audit — 181 tests / 54 334 checks green in Debug
      (ASan/UBSan) and Release after `--clean-first` rebuilds; 0 warnings in
      both configs; clean-directory CMake configure + build verified.
- [x] Integration tests per synopsis feature + menu scripted flows — already in
      place from Phases 4/7 (`tests/app_tests.cpp`); re-verified by a scripted
      walkthrough of all nine menu options against the fixture (see TASK_LOG).
- [x] README.md created (build/run, all nine options, DS + algorithm + BFS
      tables, benchmark methodology, testing, limitations).
- [x] Benchmarks frozen in Phase 6 (`results/frozen/`); Phase 8 re-verified
      integrity by SHA-256 before/after a menu option-9 run — byte-identical.
- [x] Final documentation pass: ARCHITECTURE.md §7 rewritten as-built,
      F9 wording corrected to "deferred, not implemented" (§2.3/§4),
      status bumped v2.11; DEVELOPMENT_PLAN Definition of Done no longer
      claims optional F9; SUBMISSION_NOTES.md (sections A–F) and
      explanation.md (personal study guide) created.
- [x] Viva/demo prep: SUBMISSION_NOTES §E (menu demo sequence) and §F
      (Q&A); explanation.md §9 (30-second explanation).

Exit criteria — "Definition of Done" checklist (below) all ☑.

---

## Definition of Done (project-level)

- All 8 synopsis features work on the fixture project and the menu loads them
  from a real project folder. (Optional F9 report saving is **deferred** —
  not part of the delivered scope; documented in ARCHITECTURE §9/§1.)
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
