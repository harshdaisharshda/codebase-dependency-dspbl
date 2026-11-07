# TASK_LOG.md — Codebase Dependency Analyzer

Simple progress tracker. One row per work session/task.
Status values: `pending` → `in progress` → `done` · `blocked` (needs a decision).
Add a row at the end of every work session; nothing is ever renamed away — apps and
history stay in the Notes column.

| Date | Phase / Task | Owner | Status | Notes |
|------|--------------|-------|--------|-------|
| 2026-11-07 | Workspace inspection | Harshda (via agent) | done | Workspace originally held 5 empty docs + empty `untitled folder/`; no source code. |
| 2026-11-07 | Read synopsis | Harshda (via agent) | done | NOTE: file is `Project_Synopsis_Codebase_Dependency_Analyzer.docx` (Word file) — not the expected `Project_Synopsis.pdf`. Read in full via text extraction. |
| 2026-11-07 | Documentation setup | Harshda (via agent) | done | Created AGENTS.md (project rulebook); rewrote ARCHITECTURE.md v2.0 (overview grounded in synopsis; earlier pre-synopsis draft superseded); created DEVELOPMENT_PLAN.md, TESTING_STRATEGY.md, TASK_LOG.md. No application code written. |
| 2026-11-07 | Decisions approved & locked | team | done | (1) B1–B6 BFS variants provisionally kept — implement the instructor addendum, NOT claimed as teacher-mandated exact algorithms; distinguished from synopsis. (2) Deepest chain: longest path when DAG; if any cycle exists → report "undefined" (no SCC-condensation chain). (3) DFS implemented **recursively** (recursion demo); own Stack used meaningfully (path/cycle reconstruction); no STL replacements. (4) Include resolution rules accepted (includer's dir → project root; `<...>` ignored; conditional directives not evaluated; limitations documented). (5) Files identified by project-relative path; duplicate basenames = separate nodes. (6) Synopsis is DOCX, preserved unchanged; folder renamed `docs ` → `docs/`, references updated. (7) Scope locked: 8 synopsis features + 8 data structures; no silent additions. |
| — | Phase 0 — Project setup | Harshda (via agent) | done | Skeleton created; CMake installed via Homebrew & configured (C++17, `-Wall -Wextra`, Debug+ASan/UBSan, Release); git initialised, first commit `18dca71`; test harness written (1 self-check test, passing both configs); fixture sample project created; menu app tolerates garbage/EOF sanitizer-clean. |
| 2026-11-07 | Phase 1.1 — LinkedList | Harshda (via agent) | done | `src/ds/linked_list.h` (header-only template): append/pushFront/popFront/front/at/removeFirst/removeIf/contains/size/iterator; O(1) tail append; copy deleted, move-steal RAII; nodes owned exclusively in-class. 11 tests incl. 10k stress — green in Debug (ASan/UBSan) + Release, 0 warnings. Include-path convention fixed (`"ds/...", src/ on include path`). NEXT (awaiting approval): Stack. |
| 2026-11-07 | Phase 1.2 — Stack | Harshda (via agent) | done | `src/ds/stack.h` — thin RAII facade over `ds::LinkedList` (no node logic duplicated): push/pop/top/size/isEmpty/clear, all O(1); move defaulted-noexcept, copy deleted automatically; underflow asserted (same policy as LinkedList). 9 new tests → 20 tests, 106 checks green in Debug (ASan/UBSan) + Release, 0 warnings. Commit `0cb4c2c`. NEXT (awaiting approval): Queue. |
| 2026-11-07 | Phase 1.3 — Queue | Harshda (via agent) | done | `src/ds/queue.h` — thin RAII facade over `ds::LinkedList` (design decision by user: no ring buffer, reuse tested list): enqueue=O(1) append, dequeue=O(1) popFront, front/size/isEmpty O(1), clear O(n); FIFO discipline; move defaulted-noexcept; underflow asserted. 10 new tests (incl. interleaved ops & 10k stress) → 30 tests, 162 checks green in Debug (ASan/UBSan) + Release, 0 warnings. Commit `87f66bf`. NEXT (awaiting approval): Hash Table. |
| 2026-11-07 | Phase 1.4 — Hash Table | Harshda (via agent) | done | `src/ds/hash_table.h` (template `HashTable<V>`, string keys) + new `src/ds/dyn_array.h` bucket primitive. Separate chaining via `ds::LinkedList`, FNV-1a-64, doubling rehash at load > 0.75, duplicate-insert rejected, `find`→`const V*` (absent ≠ stored zero), moved-from reusable. Tests caught 2 real bugs: DynArray non-value-init; defaulted HashTable move left stale `count_` → both fixed. 18 new tests → 48 tests / 20 293 checks green Debug (ASan/UBSan) + Release, 0 warnings. Commit `63e8adb`. NEXT (awaiting approval): Trie. |
| 2026-11-07 | Phase 1.5 — Trie | Harshda (via agent) | done | `src/ds/trie.h` — basename-keyed prefix trie (per user requirement): duplicate basenames → one terminal holding multiple graph-node ids, ids↔paths resolved later via node table. Children in own LinkedList (memory rationale documented). insert/containsExact/searchPrefix(→dedup ids)/remove(name,id)/size/clear; case-sensitive; moved-from reusable. Compile fixes: Node fwd-decl; iterator const-ref binding. 13 new tests incl. 10k shared-prefix stress → 61 tests / 30 375 checks green Debug (ASan/UBSan) + Release, 0 warnings. NEXT (awaiting approval): Max Heap. |
| — | Phase 1 — Custom data structures | team | in progress | 5/6 done (LinkedList, Stack, Queue, HashTable, Trie); Max Heap remains |
| — | Phase 2 — Dependency graph model | team | pending | node table, adjacency + reverse lists, finalize() |
| — | Phase 3 — Source scanning & include parsing | team | pending | filesystem walk, `#include` extraction, resolution rules |
| — | Phase 4 — Core algorithms (F2–F8) | team | pending | cycle DFS/SCC, topo (Kahn), impact (reverse BFS), deepest chain, hubs (MaxHeap), Trie search, bidirectional path |
| — | Phase 5 — Six BFS variations | team | pending | B1–B6 per ARCHITECTURE.md §5 + equivalence tests |
| — | Phase 6 — Benchmarking suite | team | pending | generators, runner, steady_clock, CSV output |
| — | Phase 7 — Menu integration, report saving, polish | team | pending | full menu flow, console tree/levels, `F9` report file |
| — | Phase 8 — Testing, documentation, submission prep | team | pending | final suite audit, README, frozen benchmark run, viva dry-run |

## Decisions needed (blockers for later phases)

1. **Six BFS variant list (B1–B6)** — kept **provisionally**; they implement the instructor addendum (the synopsis does not list them). Confirm the exact expected variants with the instructor before Phase 5 sign-off; adjust names/mapping if their list differs. Not an implementation blocker for Phases 0–4.
2. **Benchmark demo format** (CSV schema, how results are presented) — worth confirming at demo time; not a blocker.

Resolved (2026-11-07, team-approved — recorded in ARCHITECTURE.md §9): deepest-chain-with-cycles policy; recursive DFS + Stack role; include resolution rules; relative-path file identity with separate duplicate basenames; DOCX synopsis preserved with `docs/` rename; scope lock.

## Ideas proposed (do NOT implement without approval)

- (none yet)
