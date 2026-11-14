#pragma once
// src/app/menu.h
//
// Phase 7 — the menu loop itself (banner, menu text, tolerant dispatch).
// In a header so the scripted unit tests (tests/app_tests.cpp) drive the
// exact same loop the user drives; src/main.cpp is a thin shim around
// app::run(std::cin, std::cout).

#include "app/app_context.h"
#include "app/menu_actions.h"

#include <iostream>
#include <string>

namespace app
{

inline void printBanner(std::ostream& out)
{
    out << "==================================================\n"
        << "          CODEBASE DEPENDENCY ANALYZER\n"
        << "==================================================\n";
}

inline void printMenu(std::ostream& out)
{
    out << "\n"
        << "  1. Scan a Project\n"
        << "  2. View Dependency Graph\n"
        << "  3. Explore Dependencies (BFS)\n"
        << "  4. Analyze Change Impact\n"
        << "  5. Find Dependency Path\n"
        << "  6. Search Files by Prefix\n"
        << "  7. View Graph Statistics\n"
        << "  8. Demonstrate Custom Data Structures\n"
        << "  9. Run BFS Benchmarks\n"
        << "\n"
        << "  0. Exit\n";
    printDivider(out);
    out << "  Select an option: ";
}

inline int run(std::istream& in, std::ostream& out)
{
    printBanner(out);
    AppState state;
    std::string line;
    while (true)
    {
        printMenu(out);
        if (!std::getline(in, line))
        {
            out << "\n  Bye.\n";
            return 0; // EOF (<Ctrl-D>) exits cleanly instead of spinning.
        }
        int choice = 0;
        if (!parseMenuChoice(line, choice))
        {
            out << "  Please enter a number.\n";
            continue;
        }
        switch (choice)
        {
        case 0:
            out << "  Bye.\n";
            return 0;
        case 1:
            scanProjectMenu(state, in, out);
            break;
        case 2:
            viewGraphMenu(state, out);
            break;
        case 3:
            exploreMenu(state, in, out);
            break;
        case 4:
            impactMenu(state, in, out);
            break;
        case 5:
            pathMenu(state, in, out);
            break;
        case 6:
            prefixSearchMenu(state, in, out);
            break;
        case 7:
            statsMenu(state, out);
            break;
        case 8:
            dsDemoMenu(out);
            break;
        case 9:
            benchmarkMenu(out);
            break;
        default:
            out << "  Unknown menu item: " << choice << "\n";
            break;
        }
    }
}

} // namespace app
