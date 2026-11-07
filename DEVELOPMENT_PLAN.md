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

## Phase 1 — Custom data structures   *(4–5 sessions) — PENDING*

Build order follows dependency: objects needed by later phases come earlier.

| # | Structure | Notes / pitfalls to test for |
|---|-----------|------------------------------|
| 1.1 | `LinkedList` (template) | O(1) append via tail pointer; iterator; deep copy or deleted copy; empty list edge |
| 1.2 | `Stack` (template) | push/pop/top; asserts on empty pop — **never UB**; linked nodes |
| 1.3 | `Queue` (template) | ring buffer; wrap-around correctness; growth doubling; deque-from-empty is a programming error |
| 1.4 | `HashTable` (string→int, separate chaining, Linked List buckets) | collision handling; rehash at load factor ~0.75; FNV-1a-like hash; remove; contains; empty-string key |
| 1.5 | `Trie` | insert; exact-match; prefix search returning all ids; multiple files sharing a basename; empty prefix behavior (document + guard) |
| 1.6 | `MaxHeap` (score,id) | heapify build from a list in O(n); extractMax k×; **ties** broken deterministically (lexicographic name second key, applied by caller or stored in the payload) |
| 1.7 | Small dynamic numeric arrays (`IntArray`, `BoolArray`) | used for dist/parent/visited/color tables; no `std::vector` inside DS/algorithm internals |

Unit tests (per structure, plus cross-structure polymorphic use in graph in Phase 2):
empty, single item, duplicates, resize/rehash/wrap, ties, worst-case collisions,
iterator semantics, copy/move discipline. Leak check with sanitizers.

Exit criteria
- All containers pass their unit tests; no ASan/UBSan findings; no leaks.

---

## Phase 2 — Dependency graph model   *(2–3 sessions) — PENDING*

- [ ] Node table: `path`, `name`, `outDegree`, `inDegree`, `unresolvedIncludes`.
- [ ] `addNode` (dedup via HashTable → stable id), `addEdge` (no duplicate edges,
      no self-edges without explicit record), `finalize()` (build **reverse** adjacency
      lists; fill in-degrees).
- [ ] Accessors: successors / predecessors / counts / printing.
- [ ] In-memory graph construction **by hand in tests** (fixture edge lists), used
      for all algorithm tests in Phases 4–5 before file scanning exists.

Exit criteria
- Known 7-node hand-authored graph round-trips exactly (counts, degrees, ids).
- Duplicate/node/self edges behave per spec; counts verified.

---

## Phase 3 — Source scanning & `#include` parsing   *(2–3 sessions) — PENDING*

- [ ] Directory walk with `std::filesystem::recursive_directory_iterator`,
      extension filtering (`.h .hpp .hh .c .cc .cpp .cxx`), skip-list (`build/`,
      `.git/`, `tests/fixtures`, ...), sorted deterministic order.
- [ ] Line parser: `#include "..."` extraction only; strips `//` comments and
      single-line block comments around include lines; **documents** why `#ifdef` is not evaluated.
- [ ] Resolution order (per ARCHITECTURE.md §8):
      1. relative to the including file's directory,
      2. relative to the project root,
      3. otherwise → `unresolvedIncludes` (warn, no edge).
- [ ] Builds the graph from Phase 2 via HashTable lookups; populates Trie.
- [ ] Scan summary stats: files scanned, includes found, unresolved count, self-includes.

Exit criteria
- Scanning `tests/fixtures/sample_project` produces the exact graph assumed by
  algorithm tests (verified output: same vertices/edges/counts); warnings list matches.
- Non-existent folder → clean fatal error handling (menu re-prompt or exit code path).

---

## Phase 4 — Core graph algorithms & features (synopsis F1–F8)  *(4–5 sessions) — PENDING*

4.1 Cycle detection (**DFS + Stack**): 3-color iterative DFS with explicit
    `Stack<DFSFrame>`, frames track the cursor position in the Linked List of edges.
    Reports: simple cycle instances (capped) + node->SCC-id map (Tarjan, iterative,
    our Stack) for groups.
4.2 Topological order (**Queue / multi-source BFS**): Kahn's algorithm; leftovers
    (inside/after cycles) reported via the SCC map of 4.1.
4.3 Impact analysis (**Queue / reverse BFS**): all transitive dependents of a file,
    levels = distance.
4.4 Deepest dependency chain (**recursive DFS**): compute the longest dependency
    chain when the graph is a DAG; **if cycles exist, report that the longest
    dependency chain is undefined for this graph** (list the cycle groups instead;
    do NOT present an SCC condensation as a real file-level chain). Policy
    approved by the team 2026-11-07 (ARCHITECTURE.md §9).
4.5 Highly connected modules (**MaxHeap**): build-heap on in-degree; extract top-k
    (k = user input); tie-break deterministic.
4.6 Prefix search (**Trie**): search + existence; handle multiple basenames / prefix hits.
4.7 Path between two files (**bidirectional BFS**, see Phase 5 B3 — implement
    together with 4.7 test cases).
4.8 Dependency tree/levels **console output formatting** (synopsis sample output style).

Exit criteria — one test per feature against the fixture project with
hand-computed answers, including: 2-cycle, 3-cycle, self-include, disconnected,
diamond, duplicate basenames, unresolved-include scenario.

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
