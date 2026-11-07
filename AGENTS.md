# AGENTS.md — Project Instructions (Codebase Dependency Analyzer)

> This file is the rulebook for every future coding session (human or AI-assisted).
> The authoritative source for WHAT the product must do is the approved synopsis:
> `docs/Project_Synopsis_Codebase_Dependency_Analyzer.docx` — it is a **Word**
> document (not the PDF referenced in retrieval instructions). The file is
> preserved **unchanged**; its folder was renamed from `docs ` (trailing space)
> to `docs/` on 2026-11-07.

## 1. Project objective and scope

Build a **C++ console (menu-driven) application** that:

1. Scans a C/C++ project folder and reads its `.cpp`/`.h` source files.
2. Extracts `#include` statements to find which files depend on which.
3. Models the project as a **directed graph** (file = node, `#include` = edge).
4. Provides these analyses on the graph (approved synopsis features):
   - **Circular dependency detection** (DFS + stack: "is an edge going back to a file already on the stack?")
   - **Change impact analysis** (all files that *transitively depend on* a changed file — BFS expanding level by level)
   - **Topological compilation order** (a build order where every file compiles after its dependencies)
   - **Deepest dependency chain** (longest dependency chain in the project)
   - **Highly connected modules** (rank files by in-degree = how many files depend on them)
   - **Prefix file search** (Trie over filenames, incl. "does this file exist")
   - **Dependency path between two files** (does a dependency path exist; print it)
   - *(Optional in synopsis)*: **save analysis reports to a file** for later reference
5. Demonstrates course data structures with a justifiable role in each feature — never as decoration.
6. Runs from a numbered **text menu** using `cin`/`cout`.

**Instructor addendum (NOT in the synopsis, required consciously):**
implement **six meaningful BFS variations/applications** and **benchmark their
execution times** and document the comparison honestly (CSV output, no invented numbers).

## 2. Language and standards

- **Language:** C++ (C++17). Reason: `std::filesystem` for directory scanning.
  If a grading machine cannot provide C++17, fall back to C++14 and platform
  directory walking — decide early, document in DEVELOPMENT_PLAN.md.
- **Allowed standard-library use:** I/O (`iostream`, `fstream`), `string`,
  `filesystem`, `chrono`, and simple typed arrays (`pair`, `initializer_list`).
- **Forbidden replacement rule:** the eight required data structures must be
  **hand-written**. Do NOT use `std::vector`, `std::list`, `std::stack`,
  `std::queue`, `std::unordered_map`, or `std::priority_queue` where a required
  custom structure is called for. Using `std::string` for text is fine — we are
  not writing our own string class.

## 3. Required custom data structures and where each is used

| Data structure | Role in this project (must match synopsis) |
|---|---|
| Directed Graph | The dependency graph itself (node = file, edge = include) — adjacency-list based |
| DFS | Cycle detection, deepest-chain exploration, reachability |
| Stack | DFS frames / "recursion stack" tracking for cycle detection |
| Queue | BFS working queue (impact analysis); multi-source variant for topological sort |
| Hash Table | File name/path → node id mapping, O(1) lookup while building & querying |
| Trie | Prefix search over filenames |
| Max Heap | Rank modules by in-degree for "most depended-upon" |
| Linked List | Adjacency lists (each node's dependencies / dependents); hash-table chaining |

## 4. Coding conventions

- File names: `snake_case.h/.cpp`. Class names: `UpperCamel`. Functions &
  variables: `lowerCamel`. Constants: `UPPER_SNAKE`. Member suffix `_` optional —
  pick one style and stay consistent within a file.
- 4-space indentation; braces on their own line; one class per header.
- Every header has an include guard (`#ifndef … #define … #endif` or `#pragma once`).
- Prefer building things from smaller functions over deep inheritance.
  No design patterns unless the problem demands one.
- Comments explain **why**, not what. Every non-obvious algorithm decision
  (e.g., how "deepest chain" behaves with cycles) gets a short comment and a
  matching explanation in documentation.
- Compiler: **no warnings** at `-Wall -Wextra` in Debug and Release.
- Every source file compiles into the same single binary `dep-analyzer` plus the
  unit-test binary; no stray `main()` files.

## 5. Memory management expectations

- **RAII everywhere:** every custom class owns its nodes and frees them in its
  destructor. Copy construction / assignment is either implemented properly
  (deep copy) or deleted — never shallow-copied.
- Raw `new`/`delete` appear **only inside** custom data-structure classes, never
  in algorithms or application code.
- Graph ownership model: the graph owns all adjacency lists; algorithms only
  **read** the graph; results (paths, levels, orders) are returned as plain
  arrays of node ids owned by the caller.
- Node identity: stable integer ids 0..V-1; strings only at the boundary
  (name ↔ id via Hash Table).
- No global mutable state, no singleton objects.
- Use VS Xcode/AddressSanitizer or `valgrind`-equivalent **leak check once per
  phase** (Uber Mac: run with `-fsanitize=address,undefined` in Debug).

## 6. Error handling expectations

- **Never throw** exceptions from data structures or algorithms; document
  preconditions as asserts instead.
- Filesystem errors are **reported, then skipped** (`Warning: cannot read …`).
  Folder-not-found is the only fatal error on project load.
- Unresolvable `#include` targets (not any project file) are listed as
  "unresolved" — never crash, never invent edges.
- Menu input must tolerate garbage — re-prompt, never crash.

## 7. Testing requirements (see TESTING_STRATEGY.md for detail)

- Every custom data structure has unit tests, including edge cases (empty
  container, single item, duplicates, collisions, full/resize, ties in heap).
- Every synopsis feature has at least one **end-to-end test on the bundled
  fixture project** (`tests/fixtures/sample_project`) with hand-computed
  expected answers.
- Algorithm tests must cover: acyclic graph, cycle of 2, cycle of 3, self-include,
  disconnected graph, diamond dependency, duplicate file names.
- Benchmark counters (visited vertices / edges examined / max frontier size) are
  verified numerically on known graphs; wall-clock timings are reported, **never
  asserted** (timing asserts would make tests flaky).
- `unit-tests` binary must pass in Debug and Release before any phase is marked
  done.

## 8. Scope discipline (anti-creep rules)

- Allowed scope = synopsis features + instructor addendum + report saving
  (optional synopsis item). Everything else is out unless approved in writing.
- Explicitly NOT part of this project: GUI, threading/parallelism, building a
  compiler/preprocessor, `#ifdef`-aware parsing, incremental re-scans, external
  libraries (Boost/Catch2/networking), visualization beyond console text/tree.
- If an idea seems useful: write it in TASK_LOG.md under "Proposals", do not implement.
- Any discrepancy between synopsis wording and our implementation choice is
  documented in ARCHITECTURE.md "Open questions", never silently decided.

## 9. Workflow for every coding session

1. Read TASK_LOG.md "Current" row; keep architecture docs in sync with reality.
2. Implement small; run unit tests after every data structure/algorithm, not only at the end.
3. Update TASK_LOG.md at the end of the session (what was done, what is next, blockers).
4. Benchmarks: run only on release builds, record environment in TASK_LOG, and
   never write results that were not produced by actually running the suite.
