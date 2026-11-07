// tests/io_tests.cpp
// Phase 3 tests: include parser unit tests (TESTING_STRATEGY.md Layer 1/2
// style) and scanner tests including the Layer-3 fixture integration —
// scanning tests/fixtures/sample_project must produce the EXACT graph the
// algorithm tests assume (explicit 12 nodes / 9 edges per review request).
//
// The temp-project test materializes its own mini tree with
// std::filesystem (created and removed inside the test) to cover discovery
// mechanics the fixture does not have (skip directories, extension mix).

#include "graph/dependency_graph.h"
#include "io/include_parser.h"
#include "io/project_scanner.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_framework.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// helpers (test infrastructure only — not project data structures)
// ---------------------------------------------------------------------------

// Builds "1,5,10" from an adjacency list for compact exact-list asserts.
static std::string idsToString(const ds::LinkedList<int>& list)
{
    std::string result;
    bool first = true;
    for (const int id : list)
    {
        if (!first)
            result += ',';
        result += std::to_string(id);
        first = false;
    }
    return result;
}

static int countQuoted(const std::vector<io::ParsedInclude>& parsed,
                       io::ParsedInclude::Kind kind)
{
    int count = 0;
    for (const io::ParsedInclude& include : parsed)
        if (include.kind == kind)
            ++count;
    return count;
}

// Ascends from the working directory until the test fixture is found, so the
// test binary can be started from anywhere.
static std::string findFixtureRoot()
{
    fs::path current = fs::current_path();
    for (int depth = 0; depth < 8; ++depth)
    {
        const fs::path candidate = current / "tests/fixtures/sample_project";
        if (fs::is_directory(candidate))
            return candidate.string();
        if (!current.has_parent_path())
            break;
        current = current.parent_path();
    }
    return std::string(); // caller CHECKs the failure explicitly
}

static void writeTestFile(const fs::path& path, const std::string& content)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

// ===========================================================================
// INCLUDE PARSER tests
// ===========================================================================

TEST(include_parser, quoted_includes_in_file_order)
{
    const std::string source =
        "#include \"api.h\"\n"
        "int x = 0;\n"
        "  #include \"shared/util.h\"\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{2});
    CHECK_EQ(parsed[0].target, std::string("api.h"));
    CHECK_EQ(parsed[0].kind, io::ParsedInclude::Quoted);
    CHECK_EQ(parsed[1].target, std::string("shared/util.h"));
    CHECK_EQ(parsed[1].kind, io::ParsedInclude::Quoted);
}

TEST(include_parser, whitespace_between_tokens)
{
    const std::string source =
        "#  include \"spaced.h\"\n"
        "#\tinclude\t\"tabbed.h\"\n"
        "\t #include \"indented.h\"\n"
        "#include\"nospace.h\"\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{4});
    CHECK_EQ(parsed[0].target, std::string("spaced.h"));
    CHECK_EQ(parsed[1].target, std::string("tabbed.h"));
    CHECK_EQ(parsed[2].target, std::string("indented.h"));
    CHECK_EQ(parsed[3].target, std::string("nospace.h"));
    for (const io::ParsedInclude& include : parsed)
        CHECK_EQ(include.kind, io::ParsedInclude::Quoted);
}

TEST(include_parser, angle_brackets_classified_never_quoted)
{
    const std::string source =
        "#include <string>\n"
        "#include <vector>\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{2});
    CHECK_EQ(countQuoted(parsed, io::ParsedInclude::Angle), 2);
    CHECK_EQ(countQuoted(parsed, io::ParsedInclude::Quoted), 0);
    CHECK_EQ(parsed[0].target, std::string("string"));
    CHECK_EQ(parsed[1].target, std::string("vector"));
}

TEST(include_parser, line_comments_ignored)
{
    const std::string source =
        "// #include \"whole_line.h\"\n"
        "int x = 1; // #include \"after_code.h\"\n"
        "/// #include \"triple_slash.h\"\n"
        "#include \"real.h\"\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{1});
    CHECK_EQ(parsed[0].target, std::string("real.h"));
}

TEST(include_parser, block_comments_ignored_single_and_multiline)
{
    const std::string source =
        "/* #include \"in_block.h\" */\n"
        "/* start\n"
        "   spanning lines #include \"mid_block.h\"\n"
        "   end */\n"
        "#include \"true0.h\" /* tail ignored #include \"tail.h\" */\n"
        "#include \"true1.h\"\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    // One directive per line (preprocessor rule): "true0.h" and "true1.h" are
    // on directive-starting lines; text after the target never starts one.
    CHECK_EQ(parsed.size(), std::size_t{2});
    CHECK_EQ(parsed[0].target, std::string("true0.h"));
    CHECK_EQ(parsed[1].target, std::string("true1.h"));
}

TEST(include_parser, unresolvable_forms_distinct_from_quoted)
{
    // Review ruling #2: macro-form and malformed are their own category, not
    // "quoted unresolved".
    const std::string source =
        "#include FEATURE_MACRO\n"
        "#include \"unterminated.h\n"          // opening quote only: EOF/EOL
        "#include <unterminated.angle\n";      // before '>'                 
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{3});
    CHECK_EQ(parsed[0].kind, io::ParsedInclude::Unresolvable);
    CHECK_EQ(parsed[0].target, std::string("FEATURE_MACRO"));
    CHECK_EQ(parsed[1].kind, io::ParsedInclude::Unresolvable);
    CHECK_EQ(parsed[1].target, std::string("unterminated.h"));
    CHECK_EQ(parsed[2].kind, io::ParsedInclude::Unresolvable);
    CHECK_EQ(parsed[2].target, std::string("unterminated.angle"));
    CHECK_EQ(countQuoted(parsed, io::ParsedInclude::Quoted), 0);
}

TEST(include_parser, comment_markers_inside_targets_are_literal)
{
    // Review note 1: '/' and '*' inside a quoted path must not open comments.
    const std::string source =
        "#include \"dir//with//slashes.h\"\n"
        "#include \"dir/*star*/chunk.h\"\n"
        "#include <lib*star*.h>\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{3});
    CHECK_EQ(parsed[0].target, std::string("dir//with//slashes.h"));
    CHECK_EQ(parsed[0].kind, io::ParsedInclude::Quoted);
    CHECK_EQ(parsed[1].target, std::string("dir/*star*/chunk.h"));
    CHECK_EQ(parsed[1].kind, io::ParsedInclude::Quoted);
    CHECK_EQ(parsed[2].target, std::string("lib*star*.h"));
    CHECK_EQ(parsed[2].kind, io::ParsedInclude::Angle);
    // The parser is still sane after the fake markers (no comment state left).
    const std::vector<io::ParsedInclude> later =
        io::parseIncludes(source + "#include \"after_all.h\"\n");
    CHECK_EQ(later.size(), std::size_t{4});
    CHECK_EQ(later[3].target, std::string("after_all.h"));
}

TEST(include_parser, directive_must_start_the_line)
{
    const std::string source =
        "int hash = value # include ;\n" // real code, not a directive
        "std::string tag = \"#include \\\"fake.h\\\"\";\n"
        "#include \"genuine.h\"\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{1});
    CHECK_EQ(parsed[0].target, std::string("genuine.h"));
}

TEST(include_parser, crlf_and_bom_content)
{
    const std::string bom = {static_cast<char>(0xEF), static_cast<char>(0xBB),
                             static_cast<char>(0xBF)};
    const std::string source = bom +
        "#include \"win_a.h\"\r\n"
        "int x = 0;\r\n"
        "  #include \"win_b.h\"\r\n";
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes(source);

    CHECK_EQ(parsed.size(), std::size_t{2});
    CHECK_EQ(parsed[0].target, std::string("win_a.h"));
    CHECK_EQ(parsed[1].target, std::string("win_b.h"));
}

TEST(include_parser, empty_content_yields_nothing)
{
    const std::vector<io::ParsedInclude> parsed = io::parseIncludes("");
    CHECK(parsed.empty());

    const std::vector<io::ParsedInclude> blank =
        io::parseIncludes("\n\n   \t \r\n");
    CHECK(blank.empty());
}

// ===========================================================================
// SCAN tests (discovery, resolution, integration)
// ===========================================================================

TEST(scan, missing_root_returns_false_and_graph_stays_empty)
{
    // Precondition ruling #5 (empty-graph): scanning is only ever done into
    // an empty graph (Debug builds assert on violations), so "untouched" for
    // the fatal case means: still empty, never finalized, no fallback data.
    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;

    const bool ok = io::scanProject("no_such_project_dir_phase3", graph,
                                    summary, issues, warnings);
    CHECK_EQ(ok, false);
    CHECK_EQ(graph.nodeCount(), 0);
    CHECK_EQ(graph.edgeCount(), std::size_t{0});
    CHECK(graph.isFinalized() == false);
    CHECK_EQ(summary.filesScanned, 0);
    CHECK(issues.empty());
}

TEST(scan, temp_project_discovery_resolution_and_summary)
{
    // Full mechanics test in a throwaway tree:
    //   - nested directories, all extensions, skip dirs, wrong extensions
    //   - quoted/angle/macro forms, duplicate directives, self-include
    //   - unresolved ruling #3 (repeats would each count)
    //   - root-fallback resolution (candidate 2) from a nested file
    const fs::path root = fs::temp_directory_path() / "dep-analyzer-scan-p3";
    fs::remove_all(root); // idempotent cleanup from possible earlier runs
    writeTestFile(root / "src/main.cpp",
                  "#include \"util.h\"\n"      // resolved (dir-relative)
                  "#include \"util.h\"\n"      // duplicate directive, 1 edge
                  "#include \"main.cpp\"\n"    // self-include (edge, B1)
                  "#include \"nope.h\"\n"      // unresolved
                  "#include \"nope.h\"\n"      // unresolved again (counts twice)
                  "#include <vector>\n"        // angle: counted, no edge
                  "#include COMMON_MACRO\n"    // unresolvable form
                  "#include \"deep/nested.h\"\n");
    writeTestFile(root / "src/util.h", "// leaf\n");
    writeTestFile(root / "src/deep/nested.h",
                  "#include \"shared.h\"\n");  // only the ROOT copy exists
    writeTestFile(root / "shared.h", "// root header\n");
    writeTestFile(root / "e1.c", "\n");
    writeTestFile(root / "e2.cc", "\n");
    writeTestFile(root / "e3.cxx", "\n");
    writeTestFile(root / "e4.h", "\n");
    writeTestFile(root / "e5.hpp", "\n");
    writeTestFile(root / "e6.hh", "\n");
    writeTestFile(root / "HEADER.CPP", "\n");  // uppercase ext: not a source
    writeTestFile(root / "data.txt", "\n");    // wrong extension
    writeTestFile(root / ".git/junk.cpp", "\n");   // skipped directory
    writeTestFile(root / "build/generated.cpp", "\n"); // skipped directory
    writeTestFile(root / "results/summary.cpp", "\n"); // skipped directory

    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;

    const bool ok = io::scanProject(root.string(), graph, summary, issues,
                                    warnings);
    CHECK(ok);
    CHECK(graph.isFinalized());

    // Discovery: 10 scanned files, sorted ids; skips/uppers/wrong-ext gone.
    CHECK_EQ(graph.nodeCount(), 10);
    CHECK_EQ(summary.filesScanned, 10);
    CHECK_EQ(graph.node(0).path, std::string("e1.c"));
    CHECK_EQ(graph.node(5).path, std::string("e6.hh"));
    CHECK_EQ(graph.node(6).path, std::string("shared.h"));
    CHECK_EQ(graph.node(9).path, std::string("src/util.h"));
    CHECK_EQ(graph.node(9).name, std::string("util.h"));
    int id = graph::DependencyGraph::kInvalidNodeId;
    CHECK(graph.findId("HEADER.CPP", id) == false);
    CHECK(graph.findId("build/generated.cpp", id) == false);

    // id map: 6 shared.h | 7 src/deep/nested.h | 8 src/main.cpp | 9 src/util.h
    CHECK_EQ(idsToString(graph.successors(8)), std::string("9,8,7"));
    // Insertion order: util.h first (line 1), then the self-edge, then the
    // nested include. 7→6 exercises the project-root fallback (no
    // src/deep/shared.h exists); self-edge 8→8 present per ruling B1; the
    // duplicate util.h directive produced exactly one edge.
    CHECK_EQ(idsToString(graph.successors(7)), std::string("6"));
    CHECK_EQ(idsToString(graph.successors(6)), std::string(""));
    CHECK_EQ(graph.edgeCount(), std::size_t{4}); // 8→9, 8→8, 8→7, 7→6
    CHECK_EQ(graph.inDegree(6), std::size_t{1});
    CHECK_EQ(graph.inDegree(9), std::size_t{1});

    // Summary counts per the rulings: every DIRECTIVE counted, dedup is edge-
    // level only.
    CHECK_EQ(summary.quotedIncludes, 7); // util×2, main.cpp, nope×2, nested,
                                         // shared.h — every directive counts
    CHECK_EQ(summary.angleIncludes, 1);
    CHECK_EQ(summary.unresolvableIncludes, 1);
    CHECK_EQ(summary.unresolvedIncludes, 2); // nope.h twice (ruling #3)
    CHECK_EQ(summary.selfIncludes, 1);
    CHECK_EQ(issues.size(), std::size_t{2});
    CHECK_EQ(issues[0].includerPath, std::string("src/main.cpp"));
    CHECK_EQ(issues[0].target, std::string("nope.h"));
    CHECK_EQ(issues[1].target, std::string("nope.h"));
    CHECK_EQ(graph.node(8).unresolvedIncludes, std::size_t{2}); // node counter

    // One warning per unresolved directive (ruling #4 kept the walk alive: a
    // permission error would appear here as its own warning and not abort).
    CHECK_EQ(warnings.size(), std::size_t{2});
    CHECK(warnings[0].find("nope.h") != std::string::npos);

    fs::remove_all(root);
}

TEST(scan, fixture_exact_graph_12_nodes_9_edges)
{
    const std::string fixtureRoot = findFixtureRoot();
    CHECK(!fixtureRoot.empty()); // fixture must exist for this suite

    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;

    const bool ok =
        io::scanProject(fixtureRoot, graph, summary, issues, warnings);
    CHECK(ok);
    CHECK(graph.isFinalized());

    // EXPLICIT exit-criterion totals (review request: not just examples).
    CHECK_EQ(summary.filesScanned, 12);
    CHECK_EQ(graph.nodeCount(), 12);
    CHECK_EQ(graph.edgeCount(), std::size_t{9});
    CHECK_EQ(summary.quotedIncludes, 10);  // 9 resolved + missing.h
    CHECK_EQ(summary.angleIncludes, 1);    // <string> ignored as system
    CHECK_EQ(summary.unresolvableIncludes, 0);
    CHECK_EQ(summary.unresolvedIncludes, 1); // database.h → missing.h
    CHECK_EQ(summary.selfIncludes, 0);
    CHECK_EQ(issues.size(), std::size_t{1});
    CHECK_EQ(issues[0].includerPath, std::string("database/database.h"));
    CHECK_EQ(issues[0].target, std::string("missing.h"));
    CHECK_EQ(graph.node(5).unresolvedIncludes, std::size_t{1});
    CHECK_EQ(warnings.size(), std::size_t{1});

    // Exact sorted id ↔ path round-trip (determinism ruling #6).
    static const char* kExpectedPaths[12] = {
        "api/api.cpp",        "api/api.h",           "cycle_a.h",
        "cycle_b.h",          "database/database.cpp", "database/database.h",
        "graph.cpp",          "graph.h",             "main.cpp",
        "network/api.h",      "shared/util.h",       "utils/utils.h"};
    for (int id = 0; id < 12; ++id)
        CHECK_EQ(graph.node(id).path, std::string(kExpectedPaths[id]));

    // FULL edge-list equality (hand-computed on the fixture):
    //   0 api/api.cpp  → 1              | 5 database.h → (unresolved only)
    //   1 api/api.h    → 5              | 6 graph.cpp   → 7,10
    //   2 cycle_a.h    → 3              | 7 graph.h     → —
    //   3 cycle_b.h    → 2              | 8 main.cpp    → 1,11
    //   4 database.cpp → (angle only)   | 9 network/api.h → —
    //                                    | 10 shared/util.h → —
    //                                    | 11 utils/utils.h → 10
    CHECK_EQ(idsToString(graph.successors(0)), std::string("1"));
    CHECK_EQ(idsToString(graph.successors(1)), std::string("5"));
    CHECK_EQ(idsToString(graph.successors(2)), std::string("3"));
    CHECK_EQ(idsToString(graph.successors(3)), std::string("2"));
    CHECK_EQ(idsToString(graph.successors(4)), std::string(""));
    CHECK_EQ(idsToString(graph.successors(5)), std::string(""));
    CHECK_EQ(idsToString(graph.successors(6)), std::string("7,10"));
    CHECK_EQ(idsToString(graph.successors(7)), std::string(""));
    CHECK_EQ(idsToString(graph.successors(8)), std::string("1,11"));
    CHECK_EQ(idsToString(graph.successors(9)), std::string(""));
    CHECK_EQ(idsToString(graph.successors(10)), std::string(""));
    CHECK_EQ(idsToString(graph.successors(11)), std::string("10"));

    // Reverse mirror on every edge + Σin = Σout = E (model invariants).
    std::size_t sumIn = 0;
    std::size_t sumOut = 0;
    for (int u = 0; u < graph.nodeCount(); ++u)
    {
        sumOut += graph.outDegree(u);
        sumIn += graph.inDegree(u);
        for (const int v : graph.successors(u))
        {
            bool mirrored = false;
            for (const int back : graph.predecessors(v))
                if (back == u)
                {
                    mirrored = true;
                    break;
                }
            CHECK(mirrored);
        }
    }
    CHECK_EQ(sumIn, std::size_t{9});
    CHECK_EQ(sumOut, std::size_t{9});

    // Duplicate basenames = separate nodes (approved decision #4/§9).
    CHECK_EQ(graph.node(1).name, std::string("api.h"));
    CHECK_EQ(graph.node(9).name, std::string("api.h"));
    CHECK(graph.node(1).path != graph.node(9).path);
}

TEST(scan, helper_classification_rules)
{
    CHECK(io::isSupportedExtension(".h"));
    CHECK(io::isSupportedExtension(".cpp"));
    CHECK(io::isSupportedExtension(".cxx"));
    CHECK(io::isSupportedExtension(".hpp"));
    CHECK(io::isSupportedExtension(".hh"));
    CHECK(io::isSupportedExtension(".c"));
    CHECK(io::isSupportedExtension(".cc"));
    CHECK(io::isSupportedExtension(".hxx"));
    CHECK(io::isSupportedExtension(".C") == false); // lowercase-exact policy
    CHECK(io::isSupportedExtension(".ii") == false);
    CHECK(io::isSupportedExtension("") == false);

    CHECK(io::isSkippedDirectoryName(".git"));
    CHECK(io::isSkippedDirectoryName("build"));
    CHECK(io::isSkippedDirectoryName("results"));
    CHECK(io::isSkippedDirectoryName("builds") == false); // exact names only
    CHECK(io::isSkippedDirectoryName(".github") == false);
}
