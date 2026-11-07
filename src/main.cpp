// src/main.cpp — Phase 0 minimal application (placeholder menu).
// Phase 7 will flesh out the full menu flow (see DEVELOPMENT_PLAN.md).
// Rules honored here (AGENTS.md):
//  - no STL container is used where a required custom structure belongs;
//  - menu input tolerates garbage (getline + robust number parsing).

#include <iostream>
#include <string>

namespace app
{

void printBanner(std::ostream& out)
{
    out << "==============================\n"
        << " Codebase Dependency Analyzer\n"
        << " Phase 0 (setup) - v0.1\n"
        << "==============================\n";
}

void printMenu(std::ostream& out)
{
    // Phase 0: no features wired yet. Phases 3-7 insert real items here.
    out << "\nMenu:\n"
        << "  (0) Exit\n"
        << "> ";
}

// Parses a free-form line as a menu number; returns false when the line is
// not a parseable integer (tolerant-input requirement, AGENTS.md §6).
bool parseMenuChoice(const std::string& line, int& choice)
{
    if (line.empty())
        return false;
    std::size_t index = 0;
    while (index < line.size() && (line[index] == ' ' || line[index] == '\t'))
        ++index;
    if (index < line.size() && (line[index] == '-' || line[index] == '+'))
        ++index;
    if (index == line.size())
        return false;
    std::size_t digitStart = index;
    while (index < line.size() && line[index] >= '0' && line[index] <= '9')
        ++index;
    if (index != line.size())
        return false;
    // std::stoi-like conversion done manually so a huge entry cannot throw.
    bool negative = (line[digitStart - 1] == '-');
    long long value = 0;
    for (std::size_t i = digitStart; i < index; ++i)
        value = value * 10 + (line[i] - '0');
    choice = static_cast<int>(negative ? -value : value);
    return true;
}

int run(std::istream& in, std::ostream& out)
{
    printBanner(out);
    std::string line;
    while (true)
    {
        printMenu(out);
        if (!std::getline(in, line))
            break; // EOF (or <Ctrl-D>) exits cleanly instead of spinning.
        int choice = 0;
        if (!parseMenuChoice(line, choice))
        {
            out << "  Please enter a number.\n";
            continue;
        }
        if (choice == 0)
        {
            out << "  Bye.\n";
            return 0;
        }
        out << "  Unknown menu item: " << choice
            << " (features arrive in later phases)\n";
    }
    return 0;
}

} // namespace app

int main()
{
    return app::run(std::cin, std::cout);
}
