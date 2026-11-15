# explanation.md — how this project actually works (personal study notes)

> Not for submission. Plain language, everything covered. After reading this
> you should be able to open any file in the repo and explain it to someone.

---

## 1. The one-paragraph version

The program scans a folder of C/C++ files, reads every `#include "..."` line,
turns that into a directed graph (file = node, include = edge, A → B = "A
depends on B"), and then a text menu lets you ask questions about that graph:
what does this file use, who uses it, what order to compile in, are there
include loops, which files are the biggest hubs, is there a path from file A
to file B. All the "data-structure magic" (linked list, stack, queue, hash
table, dynamic array, trie, max heap) is written by hand, because that's the
point of the course project — and there are six flavors of BFS plus a small
benchmark suite comparing them.

## 2. How data flows through the program (the workflow)

```
folder on disk
   │  io::scanProject          (walks folders, respects skip rules)
   ▼
io::IncludeParser             (state machine per file: finds #include "x.h")
   │  resolution: includer's dir → project root → unresolved
   ▼
graph::DependencyGraph        (addNode path→id, addEdge, finalize() builds
   │                           the reverse adjacency)
   ▼
everything else just READS the graph:
   analysis.h   → F2 cycles, F3 impact, F4 topo, F5 chain, F6 hubs,
                  F7 trie search, F8 path
   bfs.h        → B1/B2/B4/B5 (one shared core), B3, B6
   menu (src/app) → wires the above to cin/cout
   bench (src/bench) → generates synthetic graphs, times the BFS variants
```

Key rule: **algorithms never modify the graph**. The graph is finalized once,
then everything else is read-only and returns plain arrays/results the caller
owns.

## 3. Tour of every source file

### `src/main.cpp`
Five lines. Calls `app::run(std::cin, std::cout)`. That's it — the whole
interface lives in `src/app/`.

### `src/app/app_context.h`
- `AppState` — one struct holding: the graph, the scan summary (counts of
  quoted/angle/unresolvable/unresolved/self includes), the list of
  unresolved-include issues, scanner warnings, and a `hasGraph` flag.
  `graphReady()` = scanned AND at least one file.
- `parseMenuChoice(line, choice)` — reads a string like "  12abc" and says
  whether it's a clean integer. Manual digit-by-digit conversion so huge
  input can't throw. (Fun fact: the original Phase 0 version peeked
  backwards at `line[digitStart - 1]` which reads out of bounds when the
  number starts at position 0 — a latent bug that survived six phases until
  the scripted menu tests + ASan caught it in Phase 7. Now the sign is
  tracked while scanning forward.)
- `trimWhitespace(text)` — post-Phase-8 addition: trims spaces, tabs, and
  CRLF '\r' leftovers from both ends of a line. Every terminal prompt that
  accepts a typed/pasted value calls it (menu choice, file picker, prefix,
  scan directory), so " src/os.cc " works like "src/os.cc" and a
  whitespace-only line counts as "empty" (cancel / fixture default).

### `src/app/menu.h`
`app::run()` = banner + loop: print menu, getline, parse, `switch`, call the
matching handler. EOF exits cleanly. Garbage input → "Please enter a
number." It's in a header (not in main.cpp) so the unit tests can feed a
scripted input stream into the exact same loop a user drives.

### `src/app/menu_actions.h`
The nine menu options, one function each, plus helpers:
- `pickFile()` — the file selector used by options 3/4/5. Tries the exact
  project-relative path first (`graph.findId`), then falls back to "treat it
  as a basename prefix" using the trie. Multiple matches → numbered list.
  Empty line always cancels.
- Option 1 **scan**: asks for a directory (empty line = the bundled sample
  fixture). Scans into *fresh local* objects and only copies them into
  `AppState` on success — a failed scan keeps the old graph.
- Option 2 **view**: lists `id: path` then every edge `A -> B`.
- Option 3 **explore**: runs `bfs::explore` (B1) and groups files by BFS
  level (0 = the file, 1 = its direct includes, ...).
- Option 4 **impact**: `analysis::impactOf` — BFS over the *reverse*
  adjacency, so level 1 = files that include it directly, etc.
- Option 5 **path**: `bfs::bidirectionalPath` (B3) between two picked files.
- Option 6 **prefix search**: builds the trie (`buildNameIndex`), lists
  matches, checks exact-basename existence.
- Option 7 **stats**: counts, cycles (with the actual cycle paths), top-10
  by in-degree, deepest chain, compile order.
- Option 8 **DS demos**: seven small functions, each doing real operations
  on real `src/ds` objects and printing the actual results (see §5).
- Option 9 **benchmarks**: calls the *unchanged* benchmark runner, output to
  `results/menu_run/`. In a Debug build it warns that timings aren't
  meaningful.

### `src/graph/dependency_graph.h`
The model. Important details:
- `addNode(path, name)` → stable id 0..V-1; duplicates rejected via the
  hash table; ids never change once assigned.
- `addEdge(v, u)` → rejected if duplicate; **self-edges are recorded** (a
  self-include is a real length-1 cycle).
- `finalize()` → builds the *predecessor* lists (reverse adjacency) in one
  O(V+E) pass. Before finalize, only successors exist. Any structural
  change flips `isFinalized()` back off — algorithms assert on it.
- Storage: nodes in a DynArray, two DynArrays of LinkedLists for
  successors/predecessors, HashTable for path→id. Insertion order preserved
  everywhere → every traversal is deterministic.

### `src/io/include_parser.h`
A tiny state machine that walks each file character by character:
normal code / `//` line comment / `/* */` block comment. When a line (after
whitespace) starts with `#` and contains `include`, it classifies the
target: `"..."` → quoted, `<...>` → angle (counted, never an edge), anything
else → unresolvable (macro-generated or malformed). Handles UTF-8 BOM, CRLF,
`#  include` spacing. Comments are consumed first so `// #include "x.h"`
inside code is ignored, but `#include "a//b.h"`-style targets keep their
slashes (markers inside quotes are literal).

### `src/io/project_scanner.h`
`scanProject(root, ...)`:
- recursive directory walk, skipping `.git`, `build`, `results` (exact
  names), only `.c/.cpp/.cc/.cxx/.h/.hpp/.hh/.hxx` (lowercase exact).
- Files are **sorted before addNode** → ids are reproducible.
- Each quoted include is resolved: same directory as the includer first,
  then project root, else unresolved (counted + listed, never invented as a
  node).
- Filesystem errors → warning + skip, never crash. Only "root folder
  doesn't exist" is fatal (returns false).
- Calls `finalize()` at the end. **Precondition: the graph passed in must be
  empty** (asserted) — that's why the menu scans into a fresh graph.

### `src/graph/analysis.h` — the seven features (F2–F8)
- **F2 `findCycles`** — recursive 3-color DFS: white (unvisited), gray (on
  the current recursion path), black (done). An edge to a *gray* vertex =
  back edge = cycle. The actual cycle path is rebuilt by walking the DFS
  parent chain into a `ds::Stack`. Self-loop = `[x, x]`. `cyclesFound` is
  uncapped, but only ≤16 instances are stored (so a 10k-cycle graph can't
  flood memory/output). A depth guard (10 000) trips instead of a stack
  overflow and sets `depthGuardTripped`.
- **F3 `impactOf`** — BFS over predecessors. Level 0 = the changed file
  itself, level k = k includes away (transitively). This is "what breaks if
  I change this file".
- **F4 `topologicalOrder`** — Kahn's algorithm: count remaining unresolved
  deps per node, seed the queue with all out-degree-0 nodes in id order,
  pop → "compiled", decrement dependents. Nodes never peeled (cycle members
  + anything depending on them) come back as `cycleBlocked` — reported, not
  hidden.
- **F5 `longestChain`** — step 1: if any cycle exists → undefined (longest
  path in a graph with a reachable cycle is meaningless). Step 2: memoized
  DFS where `memo[u]` = longest chain length starting at u and `next[u]` =
  the chosen first hop, so the chain is reconstructed by following `next`.
  Ties → smaller id. Same depth guard.
- **F6 `mostDependedUpon`** — every node's in-degree pushed as
  `{score, fileId}` into the MaxHeap using `buildFrom` (one O(n) heapify —
  all scores known at once, cheaper than n inserts). Extract top k. Ties →
  smaller fileId, which mirrors the scanner's sorted ids.
- **F7 `buildNameIndex` / `filesWithPrefix` / `fileExistsByBasename`** —
  builds a trie of basenames (caller owns it, no graph reference inside),
  prefix search returns graph ids ascending; existence check for the
  "does this file exist" feature.
- **F8 `dependencyPath`** — classic forward BFS with parent tracking; path
  rebuilt through a Stack so it comes out in edge order (`from -> ... ->
  to`). Shortest by hop count. `from == to` → found `[from]`.

### `src/graph/bfs.h` — the six BFS variants (instructor addendum)
- **Shared core `runCore`** powers B1/B2/B4/B5: seeds (one or many), which
  adjacency to walk (forward/reverse), optional level bound. Produces
  `TraversalReport`: `level[]`, `parent[]` (DynArray<int>, −1 = untouched),
  counters (verticesVisited, edgesExamined, maxFrontier), `boundHit`.
- **B1 `explore`** — plain BFS from one seed.
- **B2 `exploreMultiSource`** — several seeds all at level 0; a node's level
  = distance to the *nearest* seed. One seed ≡ B1 (tested).
- **B4 `exploreReverse`** — same core, walks predecessors. This is exactly
  what impact analysis needs (tested ≡ `impactOf`).
- **B5 `exploreBounded`** — BFS that stops after `maxDepth` levels.
  `boundHit` is true only if the bound actually cut off an edge scan
  (horizon nodes that are leaves don't count).
- **B3 `bidirectionalPath`** — two searches at once: forward from A over
  successors, reverse from B over predecessors. Each round expands one
  *level* of whichever frontier is smaller; on equal sizes they strictly
  alternate (a constant tie rule starves one side forever — real bug a test
  caught). Meeting = the first time one side discovers a vertex the other
  already has; among all candidates the one with smallest
  `levelF + levelB` wins, which is provably a shortest path (BFS levels are
  exact distances). Rebuilds the route with a Stack.
- **B6 `exploreDirectionOptimizing`** — per level, estimate:
  push cost = Σ out-degrees of the current wavefront (exact), pull cost =
  unclaimed × E / n (average case; multiply before divide so small graphs
  don't collapse to 0). Take pull only when *strictly* cheaper, ties →
  push. Top-down = classic "wavefront scans its successors"; bottom-up =
  "every unclaimed node scans its predecessors until one is in the
  wavefront" (stops at the first hit). Levels come out identical to B1 —
  only discovery order/parents may differ. Honest status: on all recorded
  graphs the pull branch never fired; B6's measured win on dense graphs
  comes from better memory locality, not direction switching.

### `src/ds/` — the hand-written structures
- **`linked_list.h`** — singly linked, keeps a *tail pointer* so append is
  O(1) (that's the hot operation for adjacency). Iterator support.
- **`stack.h` / `queue.h`** — thin facades over LinkedList (stack =
  pushFront/popFront, queue = append/popFront). Zero duplicated node logic.
- **`dyn_array.h`** — grow-by-doubling array. Growth uses move-assignment
  into fresh slots so move-only types work; the value is constructed
  *before* growing because the argument may point into the array itself.
  `capacity()` exposed for demos/tests.
- **`hash_table.h`** — string keys, FNV-1a 64-bit hash, separate chaining
  with our LinkedList, bucket array is our DynArray. Rehash (double) when
  `count*4 > buckets*3` (load factor 0.75). `find` returns `const V*` so
  "absent" ≠ "stored zero". `hashFor`, `bucketCount`, `loadFactor` are
  public — that's how the menu demo shows real bucket placement.
- **`trie.h`** — prefix tree over basenames. Each node keeps its children
  in a small LinkedList of `{char, Node*}` (not a 128-slot array — that
  would waste ~1 KB per node). One terminal can hold several ids
  (duplicate basenames). `visitNodes` is a read-only DFS diagnostic view
  added in Phase 7 so the menu can draw the real branch structure.
- **`max_heap.h`** — binary max-heap in a DynArray over `{score, fileId}`.
  Ordering: higher score first, tie → smaller fileId (deterministic).
  `buildFrom` = Floyd's bottom-up heapify, O(n). `rawEntryAt` = diagnostic
  view of the live array (tests walk the heap invariant with it; the menu
  prints tree levels with it).

### `src/bench/`
- **`graph_generators.h`** — SplitMix64 RNG (fixed constants → same numbers
  on any machine). Five shapes, all acyclic *by construction* (every edge
  goes to a strictly smaller id): chain (i → i−1), tree (random earlier
  parent), sparse (2.5n random), dense (8n random), hub-heavy (2n, 40% of
  edges aimed at hubs = ids ≡ 19 mod 20). Retry loops make the edge budgets
  exact (duplicate edges are rejected and redrawn).
- **`benchmark_runner.h`** — the matrix runner. Per (graph, variant):
  warm-up + 10 trials, drop min/max, mean/median/stdev over 8, rotate the
  variant order each trial. Counters are deterministic, so it *asserts*
  they don't drift between trials. Construction/validation untimed.
  Writes raw CSV (one row per trial), summary CSV (one row per
  variant×graph), env txt. `bench` binary keeps asserts ON even in Release
  (`-UNDEBUG`) so those checks actually run where the numbers are produced.
- **`bench_main.cpp`** — `./build/Release/bench [outputDir]`.

### `tests/`
- `test_framework.h` / `test_main.cpp` — the homemade harness: `TEST(suite,
  name)` self-registers via static objects; `CHECK`/`CHECK_EQ` record
  failures; a real `assert` still kills the process (that's how ASan/UBSan
  deaths surface).
- `ds_tests.cpp` — every structure, including stress (10k ops) and invariant
  walks.
- `graph_tests.cpp`, `io_tests.cpp`, `analysis_tests.cpp`, `bfs_tests.cpp`,
  `bench_tests.cpp`, `app_tests.cpp` — feature tests, oracle comparisons,
  and scripted menu sessions (`app::run(istringstream, ostringstream)`).
- `tests/fixtures/sample_project/` — the bundled mini-project: 12 files,
  9 edges, 1 unresolved include, a planted cycle (`cycle_a.h` ↔
  `cycle_b.h`), duplicate basenames (`api/api.h` vs `network/api.h`).
  Many tests assert these exact numbers.

## 4. The fixture project — memorize these numbers

12 files / 9 edges. Sorted ids: 0 api/api.cpp, 1 api/api.h, 2 cycle_a.h,
3 cycle_b.h, 4 database/database.cpp, 5 database/database.h, 6 graph.cpp,
7 graph.h, 8 main.cpp, 9 network/api.h, 10 shared/util.h, 11 utils/utils.h.

Edges (9): api/api.cpp→api/api.h, api/api.h→database/database.h,
cycle_a.h→cycle_b.h, cycle_b.h→cycle_a.h, database/database.cpp→(nothing —
only `<string>`), graph.cpp→graph.h, graph.cpp→shared/util.h, main.cpp→
api/api.h, main.cpp→utils/utils.h, utils/utils.h→shared/util.h. (Count:
that's 9 including the cycle pair.) One unresolved: database/database.h →
missing.h.

Consequences the demos rely on:
- impact of api/api.h = 3 (itself + api/api.cpp + main.cpp),
- impact of database/database.h = 4 (itself + api/api.h + api/api.cpp +
  main.cpp),
- explore(main.cpp) levels: 0 main.cpp, 1 api/api.h + utils/utils.h,
  2 database/database.h + shared/util.h,
- path main.cpp → shared/util.h = 2 hops,
- deepest chain = *undefined* (cycle), compile order blocked for
  cycle_a.h + cycle_b.h,
- prefix "api" → 3 files.

## 5. Menu option 8 — what each demo actually shows

- **LinkedList**: appends + pushFront + removeFirst, printed as
  `head -> a -> b -> nullptr` straight from the live list.
- **Stack**: pushes show `top`; pops come back 3,2,1 (LIFO proof).
- **Queue**: enqueues show `front`; dequeues come back in input order (FIFO).
- **HashTable**: after 6 inserts prints size/buckets/load factor, then the
  *real* bucket index of every key (`hashFor(key) % bucketCount` — the
  table's own published hash) so you see which keys collided into one chain.
- **DynArray**: checkpoints at 1, 16, 17, 20 elements → capacity 16 then 32
  (the doubling, observed live).
- **Trie**: inserts api.h (two ids!), api.cpp, app.cpp, util.h; prints the
  real branch tree via `visitNodes` — you can see the shared `a-p-i` path
  stored once; then a prefix search.
- **MaxHeap**: inserts `{score,id}` pairs, prints the actual tree level by
  level from the live array (`rawEntryAt`), then extracts the top 3 and
  shows the deterministic order.

## 6. Build/test/benchmark cheat sheet

```sh
cmake -S . -B build/Debug && cmake --build build/Debug
cmake -S . -B build/Release -DCMAKE_BUILD_TYPE=Release && cmake --build build/Release

./build/Debug/unit-tests      # 188 tests, 54 364 checks, ASan+UBSan
./build/Release/unit-tests    # same, optimized
./build/Debug/dep-analyzer    # the app
./build/Release/bench         # benchmarks (Release only for real timings)
```

- Debug = no optimization + ASan + UBSan (catches memory/UB bugs).
- Release = -O3 (this is what benchmarks measure).
- `bench` target alone keeps asserts in Release (`-UNDEBUG`) so the runner's
  correctness checks execute; they sit outside the timed region.
- `results/` is gitignored. `results/frozen/` = the Phase 6 frozen record —
  never overwritten; menu option 9 writes to `results/menu_run/`.

## 7. Why-comment highlights (things worth being able to explain)

1. **Ids are ints, strings only at the boundary.** Path→id via hash table;
   all algorithms work on 0..V−1. Fast, cache-friendly, deterministic.
2. **The graph owns everything; algorithms only read.** Results are plain
   arrays the caller owns. No algorithm can corrupt the model.
3. **Self-includes are real edges** (a self-include is a genuine 1-cycle),
   but they don't break BFS — the "already claimed" guard skips them.
4. **Cycle storage is capped, detection is not** — a graph with 10 000
   cycles reports "10 000 found" but stores only the first 16 instances.
5. **B3's strict tie alternation** — expanding the *smaller* frontier per
   round needs a tie rule that alternates; a constant rule starves one side
   (this bug was caught by a test, not by reading the code).
6. **B6 ties go top-down** — the top-down branch is exactly B1's discipline,
   so "no decision" defaults to the known-good behavior.
7. **Timing is never asserted** — machine-dependent; counters are
   deterministic and asserted instead.

## 8. Known limitations (say these out loud in a demo, it builds trust)

- `#ifdef`/macros are not evaluated; the parser is deliberately simple.
- `<...>` includes are counted but never resolved (no system headers in the
  graph).
- B6 never switched to bottom-up on the recorded benchmark matrix.
- Report-to-file (F9, optional in the synopsis) is not implemented.
- macOS LeakSanitizer unsupported; Debug still uses ASan+UBSan for heap/UB.
- Benchmarks use synthetic shapes + the tiny fixture; real-world scale is
  extrapolated, not claimed.

## 9. 30-second explanation (for talking to someone)

"We scan a C++ project's `#include` lines with a small parser and build a
directed graph — files are nodes, includes are arrows. Everything else is
graph algorithms on top: DFS with 3-coloring for cycles, reverse BFS for
change impact, Kahn's algorithm for compile order, memoized DFS for the
deepest chain, a max-heap for hub ranking, a trie for filename search, and
BFS for shortest paths — plus six BFS variants, including a bidirectional
search that's provably shortest and a simplified direction-optimizing BFS.
All seven required data structures are hand-written and each one does real
work: linked lists are the adjacency lists, the hash table maps paths to
ids, the queue drives BFS, the stack rebuilds paths and cycles, the trie
powers search, the heap ranks hubs, and the dynamic array underlies all of
it. It's a menu app; tests assert every feature against a fixture project
with a planted cycle, and benchmarks compare the BFS variants on
deterministically generated graphs — timings reported honestly, counters
asserted exactly."
