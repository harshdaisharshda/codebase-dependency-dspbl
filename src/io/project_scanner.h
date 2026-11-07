#pragma once
// src/io/project_scanner.h
//
// File discovery + include resolution for the Codebase Dependency Analyzer
// (Phase 3). Discovers the C/C++ sources of a project folder, parses their
// `#include` directives (io/include_parser.h), resolves them against the set
// of scanned files, and fills a graph::DependencyGraph.
//
// Approved semantics (ARCHITECTURE.md §8/§9 + Phase 3 review rulings):
//  - File identity = project-relative path (duplicate basenames stay separate
//    nodes); paths are lexically normalized with forward slashes.
//  - Discovery: recursive walk; skip DIRECTORIES named exactly ".git",
//    "build", "results"; extensions (lowercase exact): .c .cpp .cc .cxx .h
//    .hpp .hh .hxx. Paths are SORTED lexicographically before addNode, so
//    node ids 0..V-1 are reproducible (Phase 3 review ruling #6).
//  - Resolution order for quoted includes (per ARCHITECTURE.md §8):
//      1. relative to the including file's directory,
//      2. relative to the project root,
//      3. otherwise UNRESOLVED: graph count via addUnresolvedInclude() +
//         issue record + warning. Resolution matches against ALREADY-SCANNED
//         nodes only — never invents nodes, never invents edges.
//  - Unresolved means: quoted target that resolved to no scanned node.
//    Macro-form/malformed directives are counted separately (Unresolvable)
//    and do NOT touch the node counter (Phase 3 review ruling #2). Every
//    unresolved DIRECTIVE is counted, repeated targets included (ruling #3).
//  - A resolved self-reference is a real edge (addEdge(id,id), Phase 2
//    ruling B1) and additionally counted as selfIncludes.
//  - Filesystem errors never abort the scan (ruling #4): unreadable entries
//    and unreadable subdirectories become warnings and are skipped; the only
//    fatal case is a missing root folder (returns false).
//
// Precondition (Phase 3 review ruling #5): graphOut must be EMPTY. The
// scanner never clears caller-owned graph data silently; passing a
// non-empty graph is a programming error (assert). The output parameters
// (summary/issues/warnings) ARE reset — they are this function's outputs.
//
// Trie population is deliberately deferred until the search feature (Phase
// 4/7) actually consumes it (approved Phase 3 deviation (a)).

#include "graph/dependency_graph.h"
#include "io/include_parser.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace io
{

struct ScanSummary
{
    int filesScanned = 0;        // files added as graph nodes
    int quotedIncludes = 0;      // "..." directives (each one, dup targets too)
    int angleIncludes = 0;       // <...> directives, counted but never edges
    int unresolvableIncludes = 0;// macro form / malformed (parser-level)
    int unresolvedIncludes = 0;  // quoted directives with no scanned match
    int selfIncludes = 0;        // resolved to the including file itself
};

// One unresolved quoted include: who included what (metric + report fodder).
struct ResolutionIssue
{
    std::string includerPath;
    std::string target;
};

// Extension check is lowercase-exact (documented limitation): `HEADER.CPP`
// is not treated as a C++ source. Kept as a free helper for tests.
inline bool isSupportedExtension(const std::string& extension)
{
    static const char* const kSupported[] = {".c", ".cpp", ".cc", ".cxx",
                                             ".h", ".hpp", ".hh", ".hxx"};
    for (const char* candidate : kSupported)
        if (extension == candidate)
            return true;
    return false;
}

// Directory skip list — exact names (Phase 3 plan). Kept as a free helper
// for tests.
inline bool isSkippedDirectoryName(const std::string& name)
{
    return name == ".git" || name == "build" || name == "results";
}

// Scans `projectRoot` into `graphOut` and finalize()s it. Returns false ONLY
// when the root folder does not exist / is not a directory; the graph is
// then left untouched. Warnings collect recoverable I/O problems.
inline bool scanProject(const std::string& projectRoot,
                        graph::DependencyGraph& graphOut,
                        ScanSummary& summaryOut,
                        std::vector<ResolutionIssue>& unresolvedOut,
                        std::vector<std::string>& warningsOut)
{
    assert(graphOut.nodeCount() == 0 &&
           "scanProject precondition: expects an EMPTY graph (ruling #5)");
    summaryOut = ScanSummary{};
    unresolvedOut.clear();
    warningsOut.clear();

    const std::filesystem::path root(projectRoot);
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_directory(root, ec) || ec)
    {
        // The synopsis' single fatal case: no project folder to load.
        warningsOut.push_back("Error: not a readable directory: " + projectRoot);
        return false;
    }

    // --- 1. discover + sort file paths (deterministic ids) ------------------
    std::vector<std::string> relativePaths;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec)
    {
        // Folder exists but cannot be opened: an empty scan, not a crash.
        warningsOut.push_back("Warning: cannot open " + projectRoot + ": " +
                              ec.message());
        graphOut.finalize();
        return true;
    }
    for (const std::filesystem::recursive_directory_iterator end;
         it != end;)
    {
        const std::filesystem::path entryPath = it->path();
        std::error_code entryEc;
        const bool isDir = it->is_directory(entryEc);
        if (entryEc)
        {
            warningsOut.push_back("Warning: cannot inspect " +
                                  entryPath.string() + ": " + entryEc.message());
            it.disable_recursion_pending(); // do not descend into the unknown
        }
        else if (isDir)
        {
            if (isSkippedDirectoryName(entryPath.filename().string()))
                it.disable_recursion_pending(); // excluded subtree (plan §2)
        }
        else if (it->is_regular_file(entryEc) && !entryEc)
        {
            if (isSupportedExtension(entryPath.extension().string()))
            {
                const std::filesystem::path relative =
                    std::filesystem::relative(entryPath, root, ec);
                if (ec)
                    warningsOut.push_back("Warning: cannot relativize path " +
                                          entryPath.string());
                else
                    relativePaths.push_back(relative.generic_string());
            }
            // Other regular files (wrong extension) are ignored silently:
            // the extension filter is the documented discovery rule.
        }
        else if (entryEc)
        {
            warningsOut.push_back("Warning: cannot inspect " +
                                  entryPath.string() + ": " + entryEc.message());
        }

        it.increment(ec);
        if (ec)
        {
            // Ruling #4: warn and keep going when possible; a persistent
            // increment failure ends the walk with what we have.
            warningsOut.push_back("Warning: cannot walk past " +
                                  entryPath.string() + ": " + ec.message());
            ec.clear();
            it.disable_recursion_pending();
            it.increment(ec);
            if (ec)
                break;
        }
    }

    std::sort(relativePaths.begin(), relativePaths.end());

    // --- 2. create the nodes (ids = sorted-path order) ----------------------
    for (const std::string& relPath : relativePaths)
    {
        const std::string base =
            std::filesystem::path(relPath).filename().generic_string();
        graphOut.addNode(relPath, base);
    }
    summaryOut.filesScanned = static_cast<int>(relativePaths.size());

    // --- 3. read each file, parse, resolve, add edges -----------------------
    for (const std::string& relPath : relativePaths)
    {
        int includerId = graph::DependencyGraph::kInvalidNodeId;
        // The node was just created in a loop over the same list — this must
        // always resolve; an assert documents the invariant.
        const bool found = graphOut.findId(relPath, includerId);
        assert(found && "scanner invariant: scanned path must be a node");
        if (!found)
            continue; // Release-build safety: skip rather than misuse an id

        std::ifstream file(root / relPath);
        if (!file.is_open())
        {
            warningsOut.push_back("Warning: cannot read " + relPath);
            continue; // ruling #4: report, then skip; scan continues
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        if (file.bad())
        {
            warningsOut.push_back("Warning: read failure on " + relPath);
            continue;
        }

        const std::filesystem::path includerDir =
            std::filesystem::path(relPath).parent_path();

        for (const ParsedInclude& include : parseIncludes(content))
        {
            switch (include.kind)
            {
            case ParsedInclude::Angle:
                ++summaryOut.angleIncludes;
                continue; // system include: counted, never an edge
            case ParsedInclude::Unresolvable:
                ++summaryOut.unresolvableIncludes;
                continue; // macro/malformed: distinct from unresolved quoted
            case ParsedInclude::Quoted:
                break;
            }
            ++summaryOut.quotedIncludes;

            // Candidate 1: relative to the including file's directory.
            std::filesystem::path first = includerDir.empty()
                                              ? std::filesystem::path(include.target)
                                              : includerDir / include.target;
            first = first.lexically_normal();
            // Candidate 2: relative to the project root.
            const std::filesystem::path second =
                std::filesystem::path(include.target).lexically_normal();

            int depId = graph::DependencyGraph::kInvalidNodeId;
            bool resolved = graphOut.findId(first.generic_string(), depId) ||
                            graphOut.findId(second.generic_string(), depId);

            if (resolved)
            {
                // addEdge deduplicates (Phase 2): repeated directives add one
                // edge. Self-references are real edges (ruling B1).
                graphOut.addEdge(includerId, depId);
                if (depId == includerId)
                    ++summaryOut.selfIncludes;
            }
            else
            {
                // Ruling #3: every unresolved directive counts.
                ++summaryOut.unresolvedIncludes;
                graphOut.addUnresolvedInclude(includerId);
                unresolvedOut.push_back(
                    ResolutionIssue{relPath, include.target});
                warningsOut.push_back("Warning: cannot resolve '" +
                                      include.target + "' included by " +
                                      relPath);
            }
        }
    }

    graphOut.finalize();
    return true;
}

} // namespace io
