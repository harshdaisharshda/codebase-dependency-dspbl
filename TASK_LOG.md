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
| — | Phase 0 — Project setup | team | pending | skeleton, build decision, git init, test harness, fixture project |
| — | Phase 1 — Custom data structures | team | pending | LinkedList, Stack, Queue, HashTable, Trie, MaxHeap, int/bool arrays — with unit tests |
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
