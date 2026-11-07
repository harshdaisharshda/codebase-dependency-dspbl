#pragma once
// tests/test_framework.h
// Minimal test harness for Codebase Dependency Analyzer (Phase 0).
//
// Design rules (per AGENTS.md):
//  - no external dependencies;
//  - <vector> here is *test infrastructure only*: the forbidden-replacement
//    rule applies to the REQUIRED data structures (Graph/Stack/Queue/...),
//    not to the test framework, which never models project data.

#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase
{
    const char* fileName;
    const char* name;
    void (*function)();
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> instance;
    return instance;
}

inline int& failureCount()
{
    static int count = 0;
    return count;
}

inline int& checkCount()
{
    static int count = 0;
    return count;
}

inline void fail(const char* file, int line, const std::string& message)
{
    std::printf("    FAIL  %s:%d  %s\n", file, line, message.c_str());
    ++failureCount();
}

} // namespace testing

#define TEST(suite, name)                                                        \
    static void suite##_##name##_test();                                        \
    static const bool suite##_##name##_registered = [] {                         \
        ::testing::registry().push_back({#suite, #name, &suite##_##name##_test});\
        return true;                                                            \
    }();                                                                        \
    static void suite##_##name##_test()

#define CHECK(condition)                                                        \
    do {                                                                        \
        ++::testing::checkCount();                                              \
        if (!(condition)) {                                                     \
            std::ostringstream CHECK_ss;                                        \
            CHECK_ss << "CHECK(" << #condition << ") failed";                   \
            ::testing::fail(__FILE__, __LINE__, CHECK_ss.str());                \
        }                                                                       \
    } while (false)

#define CHECK_EQ(left, right)                                                   \
    do {                                                                        \
        ++::testing::checkCount();                                              \
        auto CHECK_l = (left);                                                  \
        auto CHECK_r = (right);                                                 \
        if (!(CHECK_l == CHECK_r)) {                                            \
            std::ostringstream CHECK_ss;                                        \
            CHECK_ss << "CHECK_EQ(" << #left << ", " << #right                  \
                     << ") failed: got '" << CHECK_l << "' vs expected '"       \
                     << CHECK_r << "'";                                         \
            ::testing::fail(__FILE__, __LINE__, CHECK_ss.str());                \
        }                                                                       \
    } while (false)
