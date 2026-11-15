#pragma once
// src/app/app_context.h
//
// Phase 7 — shared application state for the terminal menu.
//
// One AppState owns everything a menu option needs; scanProject's
// empty-graph precondition means a re-scan REPLACES the graph (the old
// analysis is discarded together with it — documented menu semantics).
//
// parseMenuChoice lives here (moved verbatim from the Phase 0 main.cpp) so
// the file picker and the unit tests reuse the same tolerant parser instead
// of duplicating it.

#include "graph/dependency_graph.h"
#include "io/project_scanner.h"

#include <string>
#include <vector>

namespace app
{

struct AppState
{
    graph::DependencyGraph graph;
    io::ScanSummary summary;
    std::vector<io::ResolutionIssue> issues;
    std::vector<std::string> warnings;
    bool hasGraph = false; // set once a scan succeeded (even if 0 files)

    // Options 2-8 need a scanned graph with at least one file; option 9
    // (benchmarks) runs without one.
    bool graphReady() const
    {
        return hasGraph && graph.nodeCount() > 0;
    }
};

// Whitespace treated as padding: spaces, tabs, and a CRLF carriage-return
// leftover (getline strips only '\n', so piped/editable input can carry '\r').
inline bool isPaddingChar(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

// Trims leading/trailing padding characters in place. Pasted paths and menu
// answers frequently carry surrounding whitespace; treating it as significant
// made exact path lookups fail that clean input handles (Phase 8 fix).
inline void trimWhitespace(std::string& text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isPaddingChar(text[begin]))
        ++begin;
    while (end > begin && isPaddingChar(text[end - 1]))
        --end;
    if (begin != 0 || end != text.size())
        text = text.substr(begin, end - begin);
}

// Parses a free-form line as a number; false when the line is not a
// parseable integer (tolerant-input requirement, AGENTS.md §6).
// Takes the line by value and trims it, so " 5 " and "5\r" parse like "5"
// and a whitespace-only line is simply not a number.
inline bool parseMenuChoice(std::string line, int& choice)
{
    trimWhitespace(line);
    if (line.empty())
        return false;
    std::size_t index = 0;
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t'))
        ++index;
    // Sign tracked EXPLICITLY — peeking backwards at line[digitStart - 1]
    // underflows when digits start at position 0 (Phase 0 latent bug, caught
    // by ASan when the Phase 7 scripted tests exercised the parser).
    bool negative = false;
    if (index < line.size() && (line[index] == '-' || line[index] == '+'))
    {
        negative = (line[index] == '-');
        ++index;
    }
    if (index == line.size())
        return false;
    std::size_t digitStart = index;
    while (index < line.size() && line[index] >= '0' && line[index] <= '9')
        ++index;
    if (index != line.size())
        return false;
    // std::stoi-like conversion done manually so a huge entry cannot throw.
    long long value = 0;
    for (std::size_t i = digitStart; i < index; ++i)
        value = value * 10 + (line[i] - '0');
    choice = static_cast<int>(negative ? -value : value);
    return true;
}

} // namespace app
