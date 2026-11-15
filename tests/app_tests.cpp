// tests/app_tests.cpp
// Phase 7 — scripted terminal-menu tests: app::run over a scripted
// std::istream, asserting observable behaviour (guards, scan summary, graph
// view, analytics output, DS demos, tolerant input). Interactive sub-flows
// are driven by feeding the exact lines a user would type.
//
// Fixture facts relied on here (all asserted since Phases 3-5):
// 12 files / 9 edges; 1 unresolved (missing.h); cycle_a.h <-> cycle_b.h
// (1 back edge); duplicate basenames api/api.h + network/api.h.

#include "app/menu.h"

#include <sstream>
#include <string>
#include <utility>

#include "test_framework.h"

namespace
{

// Runs one scripted session; returns (exitCode, captured output).
std::pair<int, std::string> runScripted(const std::string& input)
{
    std::istringstream in(input);
    std::ostringstream out;
    const int code = app::run(in, out);
    return {code, out.str()};
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::size_t occurrences(const std::string& haystack, const std::string& needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos)
    {
        ++count;
        ++pos;
    }
    return count;
}

} // namespace

TEST(app_menu, garbage_input_reprompts_and_exit_works)
{
    const auto [code, output] = runScripted("abc\n99\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "Please enter a number."));
    CHECK(contains(output, "Unknown menu item: 99"));
    CHECK(contains(output, "Bye."));
}

TEST(app_menu, eof_exits_cleanly_without_spinning)
{
    const auto [code, output] = runScripted(""); // immediate EOF
    CHECK_EQ(code, 0);
    CHECK(contains(output, "Bye."));
}

TEST(app_menu, options_guard_against_missing_scan)
{
    // Options 2/3/4/5/6/7 before any scan → the same guard message each time.
    const auto [code, output] = runScripted("2\n3\n4\n5\n6\n7\n0\n");
    CHECK_EQ(code, 0);
    CHECK_EQ(occurrences(output, "No project scanned yet"), std::size_t{6});
}

TEST(app_menu, scan_fixture_then_view_prefix_and_stats)
{
    // Option 1 with an empty line scans the bundled fixture.
    const auto [code, output] = runScripted("1\n\n2\n6\napi\n7\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "Scan complete"));
    CHECK(contains(output, "files scanned:        12"));
    CHECK(contains(output, "dependency edges:     9"));
    CHECK(contains(output, "unresolved (no project file):   1"));
    // Option 2 shows nodes and directed edges (A -> B = A depends on B).
    CHECK(contains(output, "api/api.cpp -> api/api.h"));
    // Option 6 (Trie prefix search): duplicate basenames resolve separately.
    CHECK(contains(output, "3 file name(s) start with \"api\""));
    CHECK(contains(output, "network/api.h"));
    CHECK(contains(output, "Exact basename \"api\" exists: no"));
    // Option 7 statistics. The planted cycle makes the deepest chain
    // undefined and blocks compile order for the cycle members.
    CHECK(contains(output, "cycles: 1 back edge(s) detected"));
    CHECK(contains(output, "cycle_a.h"));
    CHECK(contains(output, "cycle_b.h"));
    CHECK(contains(output, "most depended-upon files"));
    CHECK(contains(output, "shared/util.h (2 dependent(s))"));
    CHECK(contains(output, "deepest chain: undefined"));
    CHECK(contains(output, "compile order: blocked for"));
}

TEST(app_menu, explore_impact_and_path_use_real_bfs_variants)
{
    // Scan fixture, then option 3 (B1), 4 (F3 impact), 5 (B3 path).
    // main.cpp includes api/api.h + utils/utils.h (direct edges), so its
    // explore shows level 1 with both; impact flows over DEPENDENTS only:
    // api/api.h is included by api/api.cpp AND main.cpp → 3 affected
    // (itself + 2); path main.cpp -> shared/util.h: main -> utils/utils.h
    // -> shared/util.h.
    const auto [code, output] = runScripted(
        "1\n\n3\nmain.cpp\n4\napi/api.h\n5\nmain.cpp\nshared/util.h\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "level 0 (the file itself): main.cpp"));
    CHECK(contains(output, "utils/utils.h"));
    CHECK(contains(output, "traversal counters:"));
    CHECK(contains(output, "Change impact of api/api.h"));
    CHECK(contains(output, "affected files total: 3"));
    CHECK(contains(output, "Shortest path (2 hop(s))"));
    CHECK(contains(output, "main.cpp -> utils/utils.h -> shared/util.h"));
}

TEST(app_menu, file_picker_lists_prefix_matches_and_selects)
{
    // "api.h" matches api/api.h AND network/api.h → numbered sub-selection;
    // choice 2 selects network/api.h (lexicographically later path).
    const auto [code, output] = runScripted("1\n\n3\napi.h\n2\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "2 files match \"api.h\""));
    CHECK(contains(output, "1. api/api.h"));
    CHECK(contains(output, "2. network/api.h"));
    // network/api.h has no include edges → only level 0 is shown.
    CHECK(contains(output, "level 0 (the file itself): network/api.h"));
    CHECK(contains(output, "not reachable from this file: 11 file(s)"));
}

TEST(app_menu, invalid_and_cancelled_picks_recover)
{
    // Unknown prefix; bad sub-selection number (re-prompted); then cancel.
    const auto [code, output] = runScripted("1\n\n3\nzzz\n3\napi.h\n99\n\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "No file matches \"zzz\""));
    CHECK(contains(output, "2 files match \"api.h\""));
    CHECK(contains(output, "Invalid selection."));
    // After the cancels the loop is still alive (option 0 exits).
    CHECK(contains(output, "Bye."));
}

TEST(app_menu, ds_demonstrations_run_on_real_structures)
{
    // Option 8 needs no graph.
    const auto [code, output] = runScripted("8\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "LinkedList<std::string>"));
    CHECK(contains(output, "config.h -> main.cpp -> graph.h -> nullptr"));
    CHECK(contains(output, "Stack<int>"));
    CHECK(contains(output, "Queue<std::string>"));
    // HashTable: real FNV-1a placement on the table's own bucket count.
    CHECK(contains(output, "HashTable<int>"));
    CHECK(contains(output, "actual bucket placement"));
    CHECK(contains(output, "find(\"graph.h\") -> 1"));
    CHECK(contains(output, "remove(\"graph.h\") -> removed"));
    // DynArray growth: 16 -> 32 at the 17th element (documented doubling).
    CHECK(contains(output, "size 16, capacity 16"));
    CHECK(contains(output, "size 17, capacity 32"));
    // Trie branch drawing via visitNodes: terminals show their stored ids.
    CHECK(contains(output, "- a"));
    CHECK(contains(output, "- h  [TERMINAL 0 9]"));
    CHECK(contains(output, "searchPrefix(\"api\") -> ids: 0 9 3"));
    // MaxHeap levels from the real array + deterministic extraction
    // (tie (5,0) vs (5,4): smaller fileId wins → (5,0) on top).
    CHECK(contains(output, "level 0: (5,0)"));
    CHECK(contains(output, "extractMax() -> (5,0)"));
}

// ---------------------------------------------------------------------------
// Phase 8 regression — whitespace-tolerant input (trimWhitespace fix).
// Origin: a real fmt-repository run failed every file pick once pasted paths
// carried a leading/trailing space or tab ("No file matches " src/os.cc"").
// ---------------------------------------------------------------------------

TEST(app_menu, parse_menu_choice_accepts_padded_and_crlf_lines)
{
    // Direct unit level: the shared parser trims spaces, tabs and the CRLF
    // carriage-return leftover; whitespace-only lines are "not a number".
    int value = 0;
    CHECK(app::parseMenuChoice(" 5 ", value));
    CHECK_EQ(value, 5);
    CHECK(app::parseMenuChoice("\t3\r", value));
    CHECK_EQ(value, 3);
    CHECK(app::parseMenuChoice("0\r", value));
    CHECK_EQ(value, 0);
    CHECK(!app::parseMenuChoice("   ", value));
    CHECK(!app::parseMenuChoice(" 5x ", value)); // padding does not rescue garbage
}

TEST(app_menu, file_picks_tolerate_surrounding_whitespace)
{
    // Leading space and trailing space used to fail the exact-path lookup
    // AND the basename-prefix search (both saw the untrimmed line).
    const auto [code, output] =
        runScripted("1\n\n3\n main.cpp\n3\napi/api.h \n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "Transitive dependencies of main.cpp"));
    CHECK(contains(output, "Transitive dependencies of api/api.h"));
    CHECK(!contains(output, "No file matches"));
}

TEST(app_menu, crlf_terminated_input_parses_everywhere)
{
    // Piped Windows-style input: getline keeps the '\r' of every line; the
    // menu choice, the scan default and the file pick must all survive it.
    const auto [code, output] =
        runScripted("1\r\n\n3\r\nmain.cpp\r\n0\r\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "Transitive dependencies of main.cpp"));
    CHECK(contains(output, "Bye."));
}

TEST(app_menu, whitespace_only_input_behaves_like_empty)
{
    // Picker prompts promise "empty line = cancel"; a whitespace-only line
    // trims to empty, so it cancels instead of erroring.
    const auto [code, output] = runScripted("1\n\n3\n   \n5\n\t\n6\n \n0\n");
    CHECK_EQ(code, 0);
    CHECK(!contains(output, "No file matches"));
    CHECK(!contains(output, "No stored file name starts with"));
    // The cancelled option-5 START pick must not reach the TARGET prompt.
    CHECK(occurrences(output, "Path START file") == 1u);
    CHECK(!contains(output, "Path TARGET file"));
    CHECK(contains(output, "Bye."));
}

TEST(app_menu, numbered_selection_accepts_padded_choice)
{
    // The sub-selection read used to reject " 2\t" via parseMenuChoice's
    // old trailing-whitespace rule.
    const auto [code, output] = runScripted("1\n\n3\napi.h\n 2\t\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "2 files match \"api.h\""));
    CHECK(contains(output, "level 0 (the file itself): network/api.h"));
    CHECK(!contains(output, "Invalid selection."));
}

TEST(app_menu, scan_prompt_whitespace_only_uses_fixture_default)
{
    // Option 1's empty line = bundled fixture; whitespace-only is the same
    // "default" input after trimming.
    const auto [code, output] = runScripted("1\n \n2\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "1: api/api.h"));
}

TEST(app_menu, scan_prompt_trailing_space_still_scans)
{
    // Explicit directory pasted with a trailing space (the option-1 variant
    // of the fmt report — the untrimmed path made the scan fatal).
    const std::string fixtureRoot = bench::locateFixtureRoot();
    if (fixtureRoot.empty())
        return; // no fixture discoverable in this environment
    const auto [code, output] =
        runScripted("1\n" + fixtureRoot + " \n2\n0\n");
    CHECK_EQ(code, 0);
    CHECK(contains(output, "files scanned:        12"));
    CHECK(contains(output, "1: api/api.h"));
}
