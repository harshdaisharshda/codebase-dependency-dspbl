// src/main.cpp — Phase 7 entry point. The whole terminal interface lives in
// src/app/ (app_context.h, menu_actions.h, menu.h) so the scripted unit
// tests drive the same loop; this file only wires std::cin/std::cout.

#include "app/menu.h"

int main()
{
    return app::run(std::cin, std::cout);
}
