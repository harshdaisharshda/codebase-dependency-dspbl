// tests/ds_tests.cpp
// Unit tests for the custom data structures, one suite per structure,
// following TESTING_STRATEGY.md §3.x. Suite `linked_list` = §3.1 checklist.

#include "ds/linked_list.h"

#include <string>

#include "test_framework.h"

using IntList = ds::LinkedList<int>;

// Small helper used by several linked-list tests: "1,2,3". Test infrastructure
// only — it is not a project data structure.
template <typename T>
std::string listToString(const ds::LinkedList<T>& list, char separator)
{
    std::string result;
    bool first = true;
    for (const T& value : list)
    {
        if (!first)
            result += separator;
        result += std::to_string(value);
        first = false;
    }
    return result;
}

TEST(linked_list, empty_state)
{
    IntList list;
    CHECK(list.isEmpty());
    CHECK_EQ(list.size(), std::size_t{0});
    CHECK((list.begin() == list.end())); // iteration over empty is a no-op
    CHECK(list.contains(5) == false);
    list.clear(); // idempotent on empty
    CHECK(list.isEmpty());
}

TEST(linked_list, append_grows_and_orders)
{
    IntList list;
    list.append(10);
    list.append(20);
    list.append(30);
    CHECK(!list.isEmpty());
    CHECK_EQ(list.size(), std::size_t{3});
    CHECK_EQ(std::string("10,20,30"), listToString(list, ','));
    CHECK_EQ(list.front(), 10);
    CHECK_EQ(list.at(0), 10);
    CHECK_EQ(list.at(2), 30);
    CHECK(list.contains(20));
    CHECK(list.contains(99) == false);
    CHECK(list.removeFirst(99) == false); // absent value -> false, list untouched
    CHECK_EQ(list.size(), std::size_t{3});
}

TEST(linked_list, push_front_fixes_tail_on_empty)
{
    IntList list;
    list.pushFront(1); // first insert must become head AND tail
    list.append(2);    // then normal tail appends must continue after it
    list.pushFront(0); // new head
    CHECK_EQ(std::string("0,1,2"), listToString(list, ','));
    CHECK_EQ(list.size(), std::size_t{3});
}

TEST(linked_list, pop_front_until_empty_resets_tail)
{
    IntList list;
    list.append(1);
    list.append(2);
    list.append(3);
    CHECK_EQ(list.popFront(), 1);
    CHECK_EQ(list.popFront(), 2);
    CHECK_EQ(list.popFront(), 3);
    CHECK(list.isEmpty());
    CHECK_EQ(list.size(), std::size_t{0});

    // The drained list must be fully reusable again (head AND tail fixed up).
    list.append(7);
    list.pushFront(6);
    CHECK_EQ(std::string("6,7"), listToString(list, ','));
    CHECK_EQ(list.popFront(), 6);
    CHECK_EQ(list.popFront(), 7);
}

TEST(linked_list, remove_first_cases)
{
    IntList list;
    list.append(1);
    list.append(2);
    list.append(3);

    // middle removal keeps order
    CHECK(list.removeFirst(2));
    CHECK_EQ(std::string("1,3"), listToString(list, ','));
    CHECK_EQ(list.size(), std::size_t{2});

    // head removal advances head
    CHECK(list.removeFirst(1));
    CHECK_EQ(std::string("3"), listToString(list, ','));
    CHECK_EQ(list.front(), 3);

    // single-element removal resets both head and tail
    CHECK(list.removeFirst(3));
    CHECK(list.isEmpty());

    // repeated removal is false (element gone)
    CHECK(list.removeFirst(3) == false);
}

TEST(linked_list, remove_if_uses_predicate)
{
    IntList list;
    list.append(4);
    list.append(7);
    list.append(9);
    CHECK(list.removeIf([](int v) { return v % 2 == 1; })); // removes 7 (first odd)
    CHECK_EQ(std::string("4,9"), listToString(list, ','));
    CHECK(list.removeIf([](int v) { return v < 0; }) == false);
    CHECK_EQ(list.size(), std::size_t{2});
}

TEST(linked_list, clear_and_reuse)
{
    IntList list;
    for (int i = 0; i < 50; ++i)
        list.append(i);
    list.clear();
    CHECK(list.isEmpty());
    CHECK((list.begin() == list.end()));
    list.append(9);
    CHECK_EQ(listToString(list, ','), std::string("9"));
}

TEST(linked_list, iteration_is_forward_and_const_safe)
{
    IntList list;
    list.append(1);
    list.append(2);
    list.append(3);
    const IntList& readOnly = list; // iterators must work on a const list
    int sum = 0;
    int count = 0;
    for (int v : readOnly)
    {
        sum += v;
        ++count;
    }
    CHECK_EQ(sum, 6);
    CHECK_EQ(count, 3);
}

TEST(linked_list, move_steals_nodes_source_reusable)
{
    IntList source;
    source.append(1);
    source.append(2);

    IntList target(std::move(source));
    CHECK(source.isEmpty()); // moved-from becomes empty-and-valid
    CHECK_EQ(target.size(), std::size_t{2});
    CHECK_EQ(std::string("1,2"), listToString(target, ','));

    IntList target2;
    target2.append(100);
    target2 = std::move(target); // move-assignment frees target2's old nodes
    CHECK(target.isEmpty());
    CHECK_EQ(target2.size(), std::size_t{2});
    CHECK_EQ(target2.front(), 1);

    CHECK(target2.removeFirst(2));
    CHECK(!target2.removeFirst(2)); // already unlinked
}

TEST(linked_list, stress_10k_appends_iterate_clear)
{
    IntList list;
    for (int i = 0; i < 10000; ++i)
        list.append(i);
    CHECK_EQ(list.size(), std::size_t{10000});

    long long sum = 0;
    std::size_t visited = 0;
    for (int v : list)
    {
        sum += v;
        ++visited;
    }
    CHECK_EQ(visited, std::size_t{10000});
    CHECK_EQ(sum, 10000LL * 9999 / 2);

    CHECK(list.removeFirst(5000)); // mid-list removal in big list
    CHECK_EQ(list.size(), std::size_t{9999});
    list.clear();
    CHECK_EQ(list.size(), std::size_t{0});
}
