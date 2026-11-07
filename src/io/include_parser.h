#pragma once
// src/io/include_parser.h
//
// Lightweight `#include` extraction for the Codebase Dependency Analyzer
// (Phase 3). This is a PARSER, not a preprocessor: it finds directives and
// classifies them, and deliberately nothing more.
//
// Scope decisions (documented; ARCHITECTURE.md §2.5 is the contract):
//  - Directive placement: like the real preprocessor, a directive must START
//    a line (only whitespace/comments may precede the '#'), and a line holds
//    at most one directive. Text after the target on the same line is not
//    another directive.
//  - Forms:  "..."  -> Quoted   (the only form that can create an edge)
//            <...>  -> Angle   (system dependency; counted but never an edge)
//            else   -> Unresolvable (macro form / malformed target; counted,
//                    never an edge, and DISTINCT from an unresolved quoted
//                    include per the Phase 3 review ruling #2)
//  - Comments: `//` to end of line, `/* ... */` possibly across lines.
//    Comment markers INSIDE a quoted include path are literal characters —
//    the target is consumed between its quotes first, so
//    `#include "a//b.h"`, `#include "a/*b*/c.h"` are extracted intact
//    (Phase 3 review note 1).
//  - Conditional compilation (`#ifdef`/`#if`) is NOT evaluated: an include
//    under any branch is recorded. Line splicing (`\` at EOL) and directive
//    text inside string literals are out of scope. A UTF-8 BOM at the very
//    start of the content is skipped so BOM-headed files still parse.
//
// Input: the raw text of ONE source file. Output: directives in file order.
// Stateless free function — safe to call per file as the scanner reads them.

#include <string>
#include <vector>

namespace io
{

struct ParsedInclude
{
    enum Kind
    {
        Quoted,        // #include "target" — project dependency candidate
        Angle,         // #include <target> — system include, never an edge
        Unresolvable   // macro form / malformed — reported, never an edge
    };

    std::string target;
    Kind kind = Unresolvable;
};

// --- character classification helpers (file-local by inline linkage) --------

inline bool isIncludeIdentChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c == '_') || (c >= '0' && c <= '9');
}

// Whitespace allowed BETWEEN directive tokens (not newlines: a newline ends
// the directive line).
inline bool isDirectiveSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

inline std::vector<ParsedInclude> parseIncludes(const std::string& source)
{
    std::vector<ParsedInclude> out;
    enum State
    {
        InCode,
        InLineComment,
        InBlockComment
    };
    State state = InCode;
    bool lineHasCode = false; // non-whitespace, non-comment content seen on
                              // the current line before the '#'? (directive-
                              // placement check; comment openers also block)
    const std::size_t n = source.size();
    std::size_t i = 0;

    // Skip a UTF-8 BOM so a BOM-headed file still parses its first line.
    if (n >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB &&
        static_cast<unsigned char>(source[2]) == 0xBF)
        i = 3;

    while (i < n)
    {
        const char c = source[i];

        if (state == InLineComment)
        {
            if (c == '\n')
            {
                state = InCode;
                lineHasCode = false;
            }
            ++i;
            continue;
        }
        if (state == InBlockComment)
        {
            if (c == '*' && i + 1 < n && source[i + 1] == '/')
            {
                state = InCode;
                i += 2;
                // No lineHasCode reset: `*/` followed by `#include` on the
                // same line is NOT a preprocessor directive (the `*/` token
                // is content), so the line stays "used".
            }
            else
                ++i;
            continue;
        }

        // --- InCode -------------------------------------------------------
        if (c == '\n')
        {
            lineHasCode = false;
            ++i;
            continue;
        }
        if (isDirectiveSpace(c))
        {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && source[i + 1] == '/')
        {
            state = InLineComment;
            i += 2;
            continue;
        }
        if (c == '/' && i + 1 < n && source[i + 1] == '*')
        {
            state = InBlockComment;
            lineHasCode = true; // `/*` consumes the rest of the line's meaning
            i += 2;
            continue;
        }
        if (c == '#' && !lineHasCode)
        {
            // Parse: '#' [ws] directive-name [ws] target
            std::size_t j = i + 1;
            while (j < n && isDirectiveSpace(source[j]))
                ++j;
            std::size_t k = j;
            while (k < n && isIncludeIdentChar(source[k]))
                ++k;
            if (j == k)
            {
                // `#` alone / line continuation unsupported: not a directive.
                lineHasCode = true;
                ++i;
                continue;
            }
            if (source.compare(j, k - j, "include") != 0)
            {
                // `#if`, `#define`, `#pragma`, ... — plain code for our
                // purposes; skip past the word.
                lineHasCode = true;
                i = k;
                continue;
            }

            j = k;
            while (j < n && isDirectiveSpace(source[j]))
                ++j;

            if (j < n && source[j] == '"')
            {
                ++j;
                std::string target;
                while (j < n && source[j] != '"' && source[j] != '\n')
                    target += source[j++]; // comment markers here are literal
                if (j < n && source[j] == '"')
                {
                    out.push_back(ParsedInclude{target, ParsedInclude::Quoted});
                    i = j + 1;
                }
                else
                {
                    // Unterminated quoted include: counted, not an edge.
                    out.push_back(
                        ParsedInclude{target, ParsedInclude::Unresolvable});
                    i = j;
                }
                lineHasCode = true;
                continue;
            }
            if (j < n && source[j] == '<')
            {
                ++j;
                std::string target;
                while (j < n && source[j] != '>' && source[j] != '\n')
                    target += source[j++]; // comment markers here are literal
                if (j < n && source[j] == '>')
                {
                    out.push_back(ParsedInclude{target, ParsedInclude::Angle});
                    i = j + 1;
                }
                else
                {
                    out.push_back(
                        ParsedInclude{target, ParsedInclude::Unresolvable});
                    i = j;
                }
                lineHasCode = true;
                continue;
            }

            // Macro form (`#include HEADER_MACRO`) or garbage: capture the
            // identifier for reporting, never resolve.
            std::size_t t = j;
            while (t < n && isIncludeIdentChar(source[t]))
                ++t;
            out.push_back(ParsedInclude{source.substr(j, t - j),
                                        ParsedInclude::Unresolvable});
            i = (t > j) ? t : j + 1;
            lineHasCode = true;
            continue;
        }

        // Any other code character "uses up" the line for directive purposes.
        lineHasCode = true;
        ++i;
    }
    return out;
}

} // namespace io
