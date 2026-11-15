# Codebase Dependency Analyzer

A C++17 console application that scans a C/C++ project, extracts its
`#include` relationships, models them as a directed dependency graph, and
answers practical questions about that graph through a numbered text menu:
*What does this file depend on? What breaks if I change it? In what order
should these files be compiled? Is there a hidden include cycle?*

Built as a data-structures course project: every analysis is driven by
hand-written data structures (no `std::vector`/`std::stack`/`std::map` in the
core), and the instructor's addendum — six meaningful BFS variations plus a
benchmark suite comparing them — is implemented and measured.

---

## Problem statement

Large C/C++ codebases manage dependencies implicitly through `#include`
directives. This tool makes them explicit:

- **File = node**, `#include "target"` = **directed edge** (A → B means
  *A depends on B*).
- On that graph it performs: circular-dependency detection, change-impact
  analysis, topological compilation ordering, deepest dependency chain,
  most-depended-upon (hub) ranking, filename prefix search, and
  shortest-path queries between two files.
- On top of the required features it demonstrates six BFS variants
  (single-source, multi-source, bidirectional, reverse, bounded-depth,
  direction-optimizing) and benchmarks them honestly on synthetic graphs.

## Requirements

- **C++17** (uses `std::filesystem`), **CMake ≥ 3.16**.
- Developed and verified on macOS (Apple clang); no external libraries.

## Directory structure

```
src/
  main.cpp                 entry point (delegates to app::run)
  app/                     terminal interface (menu loop, option handlers, state)
  ds/                      the seven hand-written data structures
  graph/                   DependencyGraph model, analytics (F2–F8), six BFS variants
  io/                      include parser + project scanner
  bench/                   seeded graph generators + benchmark runner + bench binary
tests/                     self-registering unit-test binary + bundled fixture project
results/                   (gitignored) benchmark CSV output; results/frozen = frozen record
docs/                      approved synopsis (preserved unchanged)
```

## Building (macOS)

```sh
cmake -S . -B build/Debug                    # Debug: ASan + UBSan enabled
cmake --build build/Debug

cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release
cmake --build build/Release
```

Three binaries are produced:

| Binary | Purpose |
|---|---|
| `build/<config>/dep-analyzer` | the menu-driven application |
| `build/<config>/unit-tests`   | self-written test harness (no external framework) |
| `build/<config>/bench`        | the benchmark suite (Release strongly recommended) |

## Running the application

```sh
./build/Debug/dep-analyzer      # or build/Release/dep-analyzer
```

### Menu options

```text
==================================================
          CODEBASE DEPENDENCY ANALYZER
==================================================

  1. Scan a Project        directory prompt; empty line scans the bundled sample project
  2. View Dependency Graph files with ids + directed edges (A -> B: A depends on B)
  3. Explore Dependencies  pick a file, see everything it transitively depends on (BFS)
  4. Analyze Change Impact pick a changed file, see who is affected at which distance
  5. Find Dependency Path  pick two files, get the shortest dependency route (bidirectional BFS)
  6. Search Files by Prefix  filename autocomplete via the trie
  7. View Graph Statistics file/edge counts, cycles, hub ranking, deepest chain, compile order
  8. Demonstrate Custom Data Structures  live operations on all seven structures
  9. Run BFS Benchmarks    runs the benchmark matrix, writes results/menu_run/

  0. Exit
```

Files are selected by exact project-relative path or by basename prefix;
ambiguous prefixes show a numbered list; an empty line always cancels.
Invalid input is re-prompted, never crashes.

### Sample output (option 4, on the bundled fixture)

```text
  Change impact of database/database.h (who transitively depends on it; level 0 = the file):
  level 0 (the changed file): database/database.h
  files at level 1: api/api.h
  files at level 2: api/api.cpp, main.cpp
  affected files total: 4
```

## Custom data structures (all hand-written in `src/ds/`)

| Structure | Role in this project |
|---|---|
| `LinkedList` | adjacency lists of the graph; hash-table chains; stack/queue backbone |
| `Stack` | DFS cycle reconstruction; shortest-path reconstruction |
| `Queue` | BFS frontiers (impact, path, Kahn's algorithm) |
| `HashTable` | file path → node id (O(1) during scan and queries) |
| `DynArray` | node tables, level/parent arrays, heap storage (amortized-doubling, move-friendly) |
| `Trie` | basename prefix search and existence checks |
| `MaxHeap` | ranking modules by in-degree ("most depended-upon") |

Each owns its nodes (RAII), supports moves, forbids shallow copies, and is
unit-tested including edge cases (empty container, duplicates, collisions,
full/resize, heap ties).

## Graph algorithms (`src/graph/analysis.h`)

- **Cycle detection** — recursive 3-color DFS; closed cycle instances are
  reconstructed through the DFS-tree parent chain; detection is uncapped,
  reported instances capped (default 16).
- **Change impact** — BFS over the *reverse* adjacency; level 0 is the
  changed file itself.
- **Topological order** — Kahn's algorithm seeded with all in-degree-0 nodes
  in id order; cycle-blocked leftovers are reported, never dropped silently.
- **Longest dependency chain** — memoized DFS; defined only for DAGs (a
  cycle makes the answer undefined by construction).
- **Most depended-upon** — all in-degrees heapified in O(V), top-k extracted.
- **Filename search** — trie over basenames; duplicate basenames (files with
  the same name in different directories) resolve to separate graph nodes.
- **Shortest path** — classic BFS; the menu additionally offers the
  bidirectional BFS variant (see below).

## The six BFS variations (`src/graph/bfs.h`)

| Variant | Difference | Use |
|---|---|---|
| B1 single-source | classic FIFO | what does this file depend on |
| B2 multi-source | several level-0 seeds | compile waves; several changed files |
| B3 bidirectional | two frontiers meeting mid-path, provably shortest | path A→B in about b^(d/2) instead of b^d |
| B4 reverse | BFS over predecessors | who depends on me (impact) |
| B5 bounded-depth | stops after N levels | "look N hops ahead" |
| B6 direction-optimizing | per-level push/pull cost heuristic (simplified from Beamer et al.) | faster full traversals on large dense graphs |

Honesty note: B6 is a **simplified per-level heuristic**, not the full
Beamer–Asanović–Patterson algorithm, and in the recorded experiments its
bottom-up branch never fired — its measured win on dense graphs comes from
wavefront discipline and memory locality, not from direction switching.

## Benchmarks

- **Shapes:** chain, tree-like, sparse (2.5 edges/node), dense (8 edges/node),
  hub-heavy (5% hubs holding 40% of edge mass) at 1k / 5k / 10k nodes, all
  acyclic by construction, plus the real scanned sample project.
- **Methodology:** per (graph × variant): 1 warm-up + 10 timed trials, drop
  min/max, mean/median/stdev over 8; variant order rotated per trial;
  deterministic counters asserted per trial; graph construction and
  validation are untimed. Wall-clock timings are never asserted.
- **Output:** `results/` (`benchmarks_raw.csv` one row per trial,
  `benchmarks_summary.csv` per variant×graph, `benchmark_env.txt`).
- **Frozen record:** `results/frozen/` — a full Release run on all 16 cases
  (1120 timed executions) with environment metadata; a second run reproduced
  every non-timing column identically.
- **Reproduce:** `./build/Release/bench` (or
  `./build/Release/dep-analyzer`, option 9). Run from the repository root.

## Testing

```sh
./build/Debug/unit-tests       # 188 tests, 54 364 checks, ASan/UBSan active
./build/Release/unit-tests     # same suite, optimized build
```

Covers every data structure (including edge cases), the scanner/parser, all
analyses (acyclic, 2-cycles, 3-cycles, self-include, disconnected, diamond,
duplicate basenames), all six BFS variants against Phase-4 oracles, the
generators, the benchmark runner invariants, and the terminal menu
(scripted-input tests).

## Known limitations

- Preprocessor directives are not evaluated (`#ifdef` code is scanned as
  written); line splicing and macro-produced `include`s are not supported.
- Angle-bracket includes (`<...>`) are counted but never resolved.
- The B6 pull branch never fired on the recorded matrix (see above).
- macOS LeakSanitizer is unsupported (`detect_leaks` not available); Debug
  sanitizers still cover heap errors and undefined behavior.
- Report export to file (optional synopsis feature F9) is not implemented.
