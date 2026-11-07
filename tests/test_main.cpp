// tests/test_main.cpp
// Entry point for the unit-test binary (Phase 0).
// Suites will be registered in their own translation units from Phase 1 on
// (ds_tests.cpp, graph_tests.cpp, io_tests.cpp, analysis_tests.cpp).

#include <cstdio>

#include "test_framework.h"

TEST(harness, self_check)
{
    // Sanity test: proves the harness itself compiles, registers, checks
    // and reports. Remove nothing here — it keeps the "0 tests" state honest.
    CHECK(true);
    CHECK_EQ(1 + 1, 2);
    CHECK_EQ(std::string("a") + "b", "ab");
}

int main()
{
    std::printf("=== unit-tests (Codebase Dependency Analyzer) ===\n");
    int passed = 0;
    for (const auto& test : ::testing::registry())
    {
        const int before = ::testing::failureCount();
        test.function();
        if (::testing::failureCount() == before)
        {
            ++passed;
            std::printf("  ok    %s.%s\n", test.fileName, test.name);
        }
    }

    std::printf("=== %zu tests registered, %d passed, %d failed, %d checks ===\n",
                ::testing::registry().size(), passed,
                ::testing::failureCount(), ::testing::checkCount());
    return ::testing::failureCount() == 0 ? 0 : 1;
}
