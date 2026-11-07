// tests/ds_tests.cpp
// Unit tests for the custom data structures, one suite per structure,
// following TESTING_STRATEGY.md §3.x. Suite `linked_list` = §3.1 checklist.

#include "ds/dyn_array.h"
#include "ds/hash_table.h"
#include "ds/linked_list.h"
#include "ds/max_heap.h"
#include "ds/queue.h"
#include "ds/stack.h"
#include "ds/trie.h"

#include <cmath>
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

// ===========================================================================
// STACK tests (TESTING_STRATEGY.md §3.2 — LIFO, underflow-by-assert policy,
// reuse after drain/clear, move semantics, 10k stress)
// ===========================================================================

using IntStack = ds::Stack<int>;

TEST(stack, initially_empty)
{
    IntStack stack;
    CHECK(stack.isEmpty());
    CHECK_EQ(stack.size(), std::size_t{0});
    stack.clear(); // idempotent on empty (items 1 & 7 partially covered here)
    CHECK(stack.isEmpty());
    // NOTE: pop()/top() on empty intentionally go through assert() (frozen
    // project policy): an assert kills the process, so it cannot be observed
    // inside the test binary — matching the LinkedList precondition policy.
}

TEST(stack, push_one_element)
{
    IntStack stack;
    stack.push(42);
    CHECK(!stack.isEmpty());
    CHECK_EQ(stack.size(), std::size_t{1});
    CHECK_EQ(stack.top(), 42);
}

TEST(stack, push_multiple_grows_size)
{
    IntStack stack;
    stack.push(1);
    stack.push(2);
    stack.push(3);
    CHECK_EQ(stack.size(), std::size_t{3});
}

TEST(stack, lifo_ordering)
{
    IntStack stack;
    stack.push(1);
    stack.push(2);
    stack.push(3);
    CHECK_EQ(stack.pop(), 3);
    CHECK_EQ(stack.pop(), 2);
    CHECK_EQ(stack.pop(), 1);
    CHECK(stack.isEmpty());
}

TEST(stack, top_does_not_remove)
{
    IntStack stack;
    stack.push(7);
    stack.push(9);
    CHECK_EQ(stack.top(), 9);
    CHECK_EQ(stack.top(), 9); // unchanged
    CHECK_EQ(stack.size(), std::size_t{2});
    CHECK_EQ(stack.pop(), 9);
    CHECK_EQ(stack.top(), 7); // new top after pop
}

TEST(stack, pop_until_empty_then_reuse)
{
    IntStack stack;
    for (int i = 0; i < 5; ++i)
        stack.push(i);
    for (int i = 4; i >= 0; --i)
        CHECK_EQ(stack.pop(), i);
    CHECK(stack.isEmpty());

    stack.push(10); // fully reusable after the drain
    stack.push(11);
    CHECK_EQ(stack.size(), std::size_t{2});
    CHECK_EQ(stack.pop(), 11);
    CHECK_EQ(stack.pop(), 10);
    CHECK(stack.isEmpty());
}

TEST(stack, clear_and_reuse)
{
    IntStack stack;
    stack.push(1);
    stack.push(2);
    stack.clear();
    CHECK(stack.isEmpty());
    CHECK_EQ(stack.size(), std::size_t{0});

    stack.push(5); // reusable
    CHECK_EQ(stack.top(), 5);
    stack.clear();
    CHECK(stack.isEmpty());

    // Re-pushing after clear must reproduce a correct LIFO order.
    stack.push(100);
    stack.push(200);
    CHECK_EQ(stack.pop(), 200);
    CHECK_EQ(stack.pop(), 100);
}

TEST(stack, move_steals_and_source_reusable)
{
    IntStack source;
    source.push(1);
    source.push(2);

    IntStack target(std::move(source));
    CHECK(source.isEmpty()); // moved-from: empty and valid
    CHECK_EQ(target.size(), std::size_t{2});
    CHECK_EQ(target.pop(), 2);

    IntStack target2;
    target2.push(50);
    target2 = std::move(target); // move-assign drops target2's old contents
    CHECK(target.isEmpty());
    CHECK_EQ(target2.size(), std::size_t{1});
    CHECK_EQ(target2.top(), 1);

    CHECK_EQ(target2.pop(), 1);
    CHECK(target2.isEmpty());
}

TEST(stack, stress_10k_lifo)
{
    IntStack stack;
    const int n = 10000;
    for (int i = 0; i < n; ++i)
        stack.push(i);

    CHECK_EQ(stack.size(), std::size_t{n});
    CHECK_EQ(stack.top(), n - 1);

    long long sum = 0; // pop everything in reverse order (i == n-1 .. 0)
    int count = 0;
    while (!stack.isEmpty())
    {
        sum += stack.pop();
        ++count;
    }
    CHECK_EQ(count, n);
    CHECK_EQ(sum, static_cast<long long>(n) * (n - 1) / 2);
    CHECK(stack.isEmpty());

    stack.push(-1); // reuse after a 10k drain
    CHECK_EQ(stack.top(), -1);
}

// ===========================================================================
// QUEUE tests (TESTING_STRATEGY.md §3.3 — FIFO discipline, wrap-free linked
// storage, underflow-by-assert policy, reuse after drain/clear, move
// semantics, interleaved operations, 10k stress)
// ===========================================================================

using IntQueue = ds::Queue<int>;

TEST(queue, initially_empty)
{
    IntQueue queue;
    CHECK(queue.isEmpty());
    CHECK_EQ(queue.size(), std::size_t{0});
    queue.clear(); // idempotent on empty
    CHECK(queue.isEmpty());
    // NOTE: dequeue()/front() on empty go through assert() (frozen project
    // policy) — same rationale as LinkedList/Stack tests.
}

TEST(queue, enqueue_one_element)
{
    IntQueue queue;
    queue.enqueue(42);
    CHECK(!queue.isEmpty());
    CHECK_EQ(queue.size(), std::size_t{1});
    CHECK_EQ(queue.front(), 42);
}

TEST(queue, enqueue_multiple_grows_size)
{
    IntQueue queue;
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);
    CHECK_EQ(queue.size(), std::size_t{3});
}

TEST(queue, fifo_ordering)
{
    IntQueue queue;
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);
    CHECK_EQ(queue.dequeue(), 1); // oldest first
    CHECK_EQ(queue.dequeue(), 2);
    CHECK_EQ(queue.dequeue(), 3);
    CHECK(queue.isEmpty());
}

TEST(queue, front_does_not_remove)
{
    IntQueue queue;
    queue.enqueue(7);
    queue.enqueue(9);
    CHECK_EQ(queue.front(), 7);
    CHECK_EQ(queue.front(), 7); // unchanged by inspection
    CHECK_EQ(queue.size(), std::size_t{2});
    CHECK_EQ(queue.dequeue(), 7);
    CHECK_EQ(queue.front(), 9); // next element after dequeue
}

TEST(queue, dequeue_until_empty_then_reuse)
{
    IntQueue queue;
    for (int i = 0; i < 5; ++i)
        queue.enqueue(i);
    for (int i = 0; i < 5; ++i)
        CHECK_EQ(queue.dequeue(), i);
    CHECK(queue.isEmpty());

    queue.enqueue(10); // fully reusable after the drain
    queue.enqueue(11);
    CHECK_EQ(queue.dequeue(), 10);
    CHECK_EQ(queue.dequeue(), 11);
    CHECK(queue.isEmpty());
}

TEST(queue, clear_and_reuse)
{
    IntQueue queue;
    queue.enqueue(1);
    queue.enqueue(2);
    queue.clear();
    CHECK(queue.isEmpty());
    CHECK_EQ(queue.size(), std::size_t{0});

    queue.enqueue(5); // reusable
    CHECK_EQ(queue.front(), 5);
    queue.clear();
    CHECK(queue.isEmpty());

    // Re-enqueueing after clear must reproduce correct FIFO order.
    queue.enqueue(100);
    queue.enqueue(200);
    CHECK_EQ(queue.dequeue(), 100);
    CHECK_EQ(queue.dequeue(), 200);
}

TEST(queue, move_steals_and_source_reusable)
{
    IntQueue source;
    source.enqueue(1);
    source.enqueue(2);

    IntQueue target(std::move(source));
    CHECK(source.isEmpty()); // moved-from: empty and valid
    CHECK_EQ(target.size(), std::size_t{2});
    CHECK_EQ(target.dequeue(), 1);

    IntQueue target2;
    target2.enqueue(50);
    target2 = std::move(target); // move-assign drops target2's old contents
    CHECK(target.isEmpty());
    CHECK_EQ(target2.size(), std::size_t{1});
    CHECK_EQ(target2.front(), 2);

    CHECK_EQ(target2.dequeue(), 2);
    CHECK(target2.isEmpty());
}

TEST(queue, interleaved_enqueue_dequeue)
{
    IntQueue queue;
    queue.enqueue(1);
    queue.enqueue(2);
    queue.enqueue(3);
    CHECK_EQ(queue.dequeue(), 1);

    queue.enqueue(4);
    queue.enqueue(5);
    CHECK_EQ(queue.front(), 2);
    CHECK_EQ(queue.dequeue(), 2);

    CHECK_EQ(queue.dequeue(), 3); // drain middle section
    CHECK_EQ(queue.front(), 4);
    queue.enqueue(6);
    queue.enqueue(7);
    CHECK_EQ(queue.size(), std::size_t{4}); // 4,5,6,7
    CHECK_EQ(queue.dequeue(), 4);
    CHECK_EQ(queue.dequeue(), 5);
    CHECK_EQ(queue.dequeue(), 6);
    CHECK_EQ(queue.dequeue(), 7);
    CHECK(queue.isEmpty());
}

TEST(queue, stress_10k_fifo)
{
    IntQueue queue;
    const int n = 10000;
    for (int i = 0; i < n; ++i)
        queue.enqueue(i);

    CHECK_EQ(queue.size(), std::size_t{n});
    CHECK_EQ(queue.front(), 0);

    long long sum = 0;
    int count = 0;
    while (!queue.isEmpty())
    {
        sum += queue.dequeue();
        ++count;
    }
    CHECK_EQ(count, n);
    CHECK_EQ(sum, static_cast<long long>(n) * (n - 1) / 2);
    CHECK(queue.isEmpty());

    queue.enqueue(-1); // reuse after a 10k drain
    CHECK_EQ(queue.front(), -1);
}

// ===========================================================================
// DYN ARRAY tests (internal HashTable bucket-array building block — kept
// small; deeper growth tests arrive with Phase 1.7 usage)
// ===========================================================================

TEST(dyn_array, default_state_is_empty)
{
    ds::DynArray<int> array;
    CHECK(array.isEmpty());
    CHECK_EQ(array.size(), std::size_t{0});
}

TEST(dyn_array, sized_construction_default_values_and_writes)
{
    ds::DynArray<int> array(5); // int elements are zero-initialised
    CHECK(!array.isEmpty());
    CHECK_EQ(array.size(), std::size_t{5});
    CHECK_EQ(array.capacity(), std::size_t{5});
    for (std::size_t i = 0; i < array.size(); ++i)
        CHECK_EQ(array[i], 0);

    array[0] = 10;
    array[4] = 14;
    CHECK_EQ(array[0], 10);
    CHECK_EQ(array[4], 14);
}

TEST(dyn_array, move_steals_and_old_storage_is_freed)
{
    ds::DynArray<int> array(8);
    array[7] = 77;

    ds::DynArray<int> target(std::move(array));
    CHECK(array.isEmpty()); // moved-from: nothing left to index
    CHECK_EQ(target.size(), std::size_t{8});
    CHECK_EQ(target[7], 77);
}

TEST(dyn_array, move_assignment_frees_destination)
{
    ds::DynArray<int> destination(4);
    ds::DynArray<int> source(9);
    source[8] = 88;

    destination = std::move(source); // destination's old buffer must be freed
    CHECK(source.isEmpty());
    CHECK_EQ(destination.size(), std::size_t{9});
    CHECK_EQ(destination[8], 88);
}

TEST(dyn_array, used_as_chain_bucket_array_like_hashtable)
{
    // Slightly larger than minimal: mimics the real HashTable bucket pattern
    // (array of LinkedList chains) to exercise move/assign on that shape.
    ds::DynArray<ds::LinkedList<int>> buckets(4);
    buckets[1].append(21);
    buckets[1].append(22);
    buckets[3].append(43);

    CHECK_EQ(buckets[0].size(), std::size_t{0});
    CHECK_EQ(buckets[1].size(), std::size_t{2});
    CHECK_EQ(buckets[3].front(), 43);

    ds::DynArray<ds::LinkedList<int>> target(std::move(buckets));
    CHECK(buckets.isEmpty());
    CHECK_EQ(target[1].size(), std::size_t{2});
}

// --- Phase 2 additions: emplaceBack growth (approved decision A1) ------------

TEST(dyn_array, emplace_back_growth_preserves_elements)
{
    ds::DynArray<int> array;
    for (int i = 0; i < 100; ++i)
        array.emplaceBack(i * 10);

    CHECK_EQ(array.size(), std::size_t{100});
    CHECK(array.capacity() >= array.size());
    for (std::size_t i = 0; i < array.size(); ++i)
        CHECK_EQ(array[i], static_cast<int>(i) * 10); // survived every realloc
}

TEST(dyn_array, emplace_back_grows_from_sized_start)
{
    // Growth path starting from the fixed-count constructor (capacity fully
    // used, then one more append must reallocate and keep the prefix intact).
    ds::DynArray<int> array(3);
    array[0] = 7;
    array[1] = 8;
    array[2] = 9;

    array.emplaceBack(10);
    CHECK_EQ(array.size(), std::size_t{4});
    CHECK_EQ(array[0], 7);
    CHECK_EQ(array[1], 8);
    CHECK_EQ(array[2], 9);
    CHECK_EQ(array[3], 10);
}

TEST(dyn_array, emplace_back_accepts_move_only_elements)
{
    // LinkedList deletes its copy constructor: growth must MOVE elements, and
    // emplaceBack itself must move the newly built value into the slot.
    ds::DynArray<ds::LinkedList<int>> lists;
    lists.emplaceBack(); // default-constructed empty list
    lists[0].append(1);
    lists.emplaceBack(std::move(lists[0])); // move an existing element in

    CHECK_EQ(lists.size(), std::size_t{2});
    CHECK_EQ(lists[0].size(), std::size_t{0}); // it was moved out on purpose
    CHECK_EQ(lists[1].front(), 1);
    CHECK_EQ(lists[1].size(), std::size_t{1});
}

TEST(dyn_array, emplace_back_repeated_growth_move_only_stress)
{
    // Many doublings (16>32>64>128>256): every reallocation must MOVE each
    // list's nodes, never copy them, and never lose a node (leak check is the
    // sanitizer's job; this asserts the observable content).
    // Pattern: even steps append a fresh list (holds i), odd steps move the
    // freshly appended element into one more slot — so every appended element
    // is moved at least once and the array is exercised while it reallocates.
    ds::DynArray<ds::LinkedList<int>> lists;
    for (int i = 0; i < 300; ++i)
    {
        if (i % 2 == 0)
        {
            ds::LinkedList<int>& fresh = lists.emplaceBack();
            fresh.append(i); // no DynArray growth between this and the append
        }
        else
        {
            ds::LinkedList<int> previous(std::move(lists[lists.size() - 1]));
            lists.emplaceBack(std::move(previous)); // safe even if it grows:
                                                    // the value is built before
                                                    // grow() runs
        }
    }
    CHECK_EQ(lists.size(), std::size_t{300});
    for (std::size_t i = 0; i < lists.size(); i += 2)
        CHECK(lists[i].isEmpty()); // even steps: elements moved out at odd steps
    for (std::size_t i = 1; i < lists.size(); i += 2)
    {
        CHECK_EQ(lists[i].size(), std::size_t{1});
        CHECK_EQ(lists[i].front(), static_cast<int>(i) - 1);
    }
}

TEST(dyn_array, emplace_back_returns_latest_reference)
{
    ds::DynArray<std::string> strings;
    strings.emplaceBack("first");
    CHECK_EQ(strings[0], "first");
    std::string& last = strings.emplaceBack("second");
    last += "!";
    CHECK_EQ(strings[1], "second!");
}

// ===========================================================================
// HASH TABLE tests (TESTING_STRATEGY.md §3.4 — collisions, duplicate keys,
// rehash integrity, absent-vs-zero-value semantics, empty string, 10k stress,
// clear & move reuse)
// ===========================================================================

using PathTable = ds::HashTable<int>;

// Deterministic collision finder: two distinct keys landing in the same
// bucket under `bucketCount` (uses the public hashFor()).
static std::pair<std::string, std::string> findCollidingKeys(std::size_t bucketCount)
{
    const std::size_t probeBucket = ds::HashTable<int>::hashFor("collision_probe_0") % bucketCount;
    for (int i = 1; i < 2000; ++i)
    {
        std::string candidate = "collision_probe_" + std::to_string(i);
        if (ds::HashTable<int>::hashFor(candidate) % bucketCount == probeBucket)
            return {"collision_probe_0", candidate};
    }
    // No collisions found in 2000 probes would itself be a broken hash.
    return {"", ""};
}

TEST(hash_table, empty_state)
{
    PathTable table;
    CHECK(table.isEmpty());
    CHECK_EQ(table.size(), std::size_t{0});
    CHECK(table.contains("main.cpp") == false);
    CHECK(table.find("main.cpp") == nullptr); // absent is distinct from zero (below)
    CHECK(table.remove("main.cpp") == false);
    CHECK(table.loadFactor() == 0.0);
    table.clear(); // idempotent on empty
    CHECK(table.isEmpty());
}

TEST(hash_table, insert_lookup_zero_value_and_absent_distinction)
{
    PathTable table;
    CHECK(table.insert("utils.h", 0)); // value IS legitimately zero
    const int* value = table.find("utils.h");
    CHECK(value != nullptr);      // present …
    CHECK_EQ(*value, 0);          // … and holds zero
    CHECK(table.contains("utils.h"));
    CHECK_EQ(table.size(), std::size_t{1});
    CHECK(table.find("nope.cpp") == nullptr); // true absence
}

TEST(hash_table, duplicate_insertion_rejected_without_overwrite)
{
    PathTable table;
    CHECK(table.insert("api.h", 7));
    CHECK(table.insert("api.h", 100) == false); // rejected
    const int* value = table.find("api.h");
    CHECK(value != nullptr);
    CHECK_EQ(*value, 7);   // original value intact
    CHECK_EQ(table.size(), std::size_t{1}); // no phantom entries
}

TEST(hash_table, multiple_keys_distinct_values)
{
    PathTable table;
    CHECK(table.insert("main.cpp", 1));
    CHECK(table.insert("api/api.h", 2));
    CHECK(table.insert("network/api.h", 3)); // duplicate basename, different path
    CHECK(table.insert("utils/utils.h", 4));

    CHECK_EQ(*table.find("main.cpp"), 1);
    CHECK_EQ(*table.find("api/api.h"), 2);
    CHECK_EQ(*table.find("network/api.h"), 3);
    CHECK_EQ(*table.find("utils/utils.h"), 4);
    CHECK_EQ(table.size(), std::size_t{4});
    CHECK_EQ(table.bucketCount(), std::size_t{16});
    CHECK(table.contains("graph.cpp") == false);
}

TEST(hash_table, load_factor_tracks_fill_level)
{
    PathTable table;
    table.insert("a", 1);
    table.insert("b", 2);
    table.insert("c", 3);
    const double factor = table.loadFactor(); // 3/16 = 0.1875
    CHECK(factor > 0.18 && factor < 0.19);
    table.remove("a");
    CHECK(table.loadFactor() > 0.12 && table.loadFactor() < 0.13); // 2/16
}

TEST(hash_table, collision_chain_insert_lookup_remove_middle)
{
    const auto [firstKey, secondKey] = findCollidingKeys(16);
    CHECK(firstKey != secondKey && firstKey != "");

    PathTable table;
    table.insert(firstKey, 10);
    table.insert(secondKey, 20);
    table.insert("independent.h", 30);

    // Both collided keys survived in the SAME bucket with distinct values.
    CHECK_EQ(*table.find(firstKey), 10);
    CHECK_EQ(*table.find(secondKey), 20);
    CHECK_EQ(table.size(), std::size_t{3});

    CHECK(table.remove(firstKey)); // collided-key removal
    CHECK_EQ(*table.find(secondKey), 20); // sibling unaffected
    CHECK_EQ(*table.find("independent.h"), 30);
    CHECK(table.find(firstKey) == nullptr);
    CHECK_EQ(table.size(), std::size_t{2});
    CHECK(!table.remove(firstKey)); // already gone
}

TEST(hash_table, rehash_boundary_is_integer_exact)
{
    PathTable table;
    for (int i = 0; i < 12; ++i) // 12*4 == 48 == 16*3 → boundary NOT exceeded
        CHECK(table.insert("resize_key_" + std::to_string(i), i));
    CHECK_EQ(table.bucketCount(), std::size_t{16});
    CHECK_EQ(table.size(), std::size_t{12});

    CHECK(table.insert("resize_key_12", 12)); // 13*4=52 > 48 → rehash to 32
    CHECK_EQ(table.bucketCount(), std::size_t{32});
    CHECK_EQ(table.size(), std::size_t{13});

    // Every pre-resize pair must still be intact.
    for (int i = 0; i < 12; ++i)
        CHECK_EQ(*table.find("resize_key_" + std::to_string(i)), i);
    CHECK_EQ(*table.find("resize_key_12"), 12);
}

TEST(hash_table, rehash_preserves_all_pairs_through_growth)
{
    PathTable table;
    const int n = 10000;
    for (int i = 0; i < n; ++i)
        CHECK(table.insert("big_" + std::to_string(i), i));
    CHECK_EQ(table.size(), std::size_t{n});
    CHECK(table.bucketCount() > 16);          // grew several times
    CHECK(table.loadFactor() <= 0.75);        // threshold respected

    for (int i = 0; i < n; ++i) // every surviving pair reachable after rehashes
        CHECK_EQ(*table.find("big_" + std::to_string(i)), i);

    CHECK(table.remove("big_0"));
    CHECK(table.contains("big_1"));
    CHECK(table.find("big_0") == nullptr);
    CHECK_EQ(table.size(), std::size_t{n - 1});
}

TEST(hash_table, empty_string_key_works)
{
    PathTable table;
    CHECK(table.insert("", 99));
    CHECK(table.contains(""));
    CHECK_EQ(*table.find(""), 99);
    CHECK(table.remove(""));
    CHECK(table.find("") == nullptr);
    CHECK(table.isEmpty());
}

TEST(hash_table, remove_missing_key_is_noop)
{
    PathTable table;
    table.insert("x", 1);
    CHECK(table.remove("missing") == false);
    CHECK_EQ(table.size(), std::size_t{1});
}

TEST(hash_table, clear_and_reuse)
{
    PathTable table;
    for (int i = 0; i < 5; ++i)
        table.insert("k" + std::to_string(i), i);
    table.clear();
    CHECK(table.isEmpty());
    CHECK_EQ(table.size(), std::size_t{0});
    CHECK(table.contains("k0") == false);
    CHECK_EQ(table.loadFactor(), 0.0);
    CHECK_EQ(table.bucketCount(), std::size_t{16}); // capacity kept for reuse

    CHECK(table.insert("fresh", 1));
    CHECK_EQ(*table.find("fresh"), 1);
    CHECK_EQ(table.size(), std::size_t{1});
}

TEST(hash_table, move_steals_and_moved_from_is_reusable)
{
    PathTable source;
    source.insert("a.h", 1);
    source.insert("b.h", 2);

    PathTable target(std::move(source));
    CHECK(source.isEmpty()); // moved-from
    CHECK_EQ(target.size(), std::size_t{2});
    CHECK_EQ(*target.find("a.h"), 1);
    CHECK_EQ(*target.find("b.h"), 2);

    PathTable target2;
    target2.insert("c.h", 3);
    target2 = std::move(target);
    CHECK(target.isEmpty()); // source of the assignment drained
    CHECK_EQ(target2.size(), std::size_t{2}); // stolen pairs arrived
    CHECK(target2.find("c.h") == nullptr); // old contents replaced

    // Moved-from table must be immediately reusable (bucket restore path).
    CHECK(source.insert("d.h", 4));
    CHECK_EQ(*source.find("d.h"), 4);
    CHECK_EQ(source.size(), std::size_t{1});
}

TEST(hash_table, works_with_non_integer_values)
{
    ds::HashTable<std::string> table;
    CHECK(table.insert("path/a.cpp", "main translation unit"));
    CHECK_EQ(*table.find("path/a.cpp"), std::string("main translation unit"));
    CHECK(table.remove("path/a.cpp"));
    CHECK(table.find("path/a.cpp") == nullptr);
}

// ===========================================================================
// TRIE tests (TESTING_STRATEGY.md §3.5 — prefix search, shared prefixes,
// duplicate basenames sharing one terminal, dedup, case sensitivity, deep
// chains, clear & move reuse). Keys are BASENAMES; ids are graph node ids.
// ===========================================================================

// Helper: does the result list contain exactly the given ids?
static bool containsExactly(const ds::LinkedList<int>& list, const std::initializer_list<int>& expectedIds)
{
    if (list.size() != expectedIds.size())
        return false;
    for (int id : expectedIds)
        if (!list.contains(id))
            return false;
    return true;
}

TEST(trie, empty_state)
{
    ds::Trie trie;
    CHECK_EQ(trie.size(), std::size_t{0});
    CHECK(trie.containsExact("api.h") == false);
    CHECK_EQ(trie.searchPrefix("api").size(), std::size_t{0});
    CHECK_EQ(trie.searchPrefix("").size(), std::size_t{0}); // "" = whole trie
    trie.clear(); // idempotent on empty
    CHECK_EQ(trie.size(), std::size_t{0});
}

TEST(trie, single_insertion_exact_and_prefix)
{
    ds::Trie trie;
    CHECK(trie.insert("api.h", 1));
    CHECK_EQ(trie.size(), std::size_t{1});
    CHECK(trie.containsExact("api.h"));
    CHECK(containsExactly(trie.searchPrefix("api"), {1}));
    CHECK(containsExactly(trie.searchPrefix("api.h"), {1}));
    CHECK(trie.containsExact("api") == false); // prefix is not an exact name
    CHECK(trie.containsExact("api.hextra") == false);
}

TEST(trie, multiple_names_sharing_prefixes)
{
    ds::Trie trie;
    trie.insert("api.h", 1);
    trie.insert("api.cpp", 2);
    trie.insert("application.cpp", 3);

    CHECK(trie.containsExact("api.h"));
    CHECK(trie.containsExact("api.cpp"));
    CHECK(trie.containsExact("application.cpp"));

    CHECK(containsExactly(trie.searchPrefix("api"), {1, 2}));
    CHECK(containsExactly(trie.searchPrefix("app"), {3}));
    CHECK(containsExactly(trie.searchPrefix("a"), {1, 2, 3}));
    CHECK_EQ(trie.searchPrefix("b").size(), std::size_t{0}); // no crash, empty list
    CHECK_EQ(trie.size(), std::size_t{3});
}

TEST(trie, duplicate_basename_multiple_ids_share_terminal)
{
    // The synopsis use-case: network/api.h (id 5) and database/api.h (id 9)
    // are separate graph nodes whose BASENAME is identical — both must be
    // returned for prefix "api".
    ds::Trie trie;
    CHECK(trie.insert("api.h", 5));
    CHECK(trie.insert("api.h", 9)); // same basename, different graph node
    CHECK_EQ(trie.size(), std::size_t{2}); // two (name, id) pairs
    CHECK(trie.containsExact("api.h"));
    CHECK(containsExactly(trie.searchPrefix("api"), {5, 9}));
    CHECK(containsExactly(trie.searchPrefix("api.h"), {5, 9}));
}

TEST(trie, duplicate_insertion_of_same_pair_rejected)
{
    ds::Trie trie;
    CHECK(trie.insert("api.h", 1));
    CHECK(trie.insert("api.h", 1) == false); // same pair again
    CHECK_EQ(trie.size(), std::size_t{1});
    CHECK(containsExactly(trie.searchPrefix("api"), {1}));
    CHECK_EQ(trie.searchPrefix("api").size(), std::size_t{1}); // no id stored twice
}

TEST(trie, search_results_deduplicated_across_names)
{
    // Same fileId legitimately stored under two different names.
    ds::Trie trie;
    trie.insert("api.h", 1);
    trie.insert("api_helper.h", 1);

    // Prefix "api" walks BOTH terminals; the id must appear exactly once.
    CHECK(containsExactly(trie.searchPrefix("api"), {1}));
    CHECK_EQ(trie.searchPrefix("api").size(), std::size_t{1});
    CHECK_EQ(trie.searchPrefix("api_hel").size(), std::size_t{1});
    CHECK_EQ(trie.size(), std::size_t{2}); // still two stored pairs
}

TEST(trie, missing_prefix_returns_empty_not_garbage)
{
    ds::Trie trie;
    trie.insert("utils.h", 7);
    ds::LinkedList<int> emptyResult = trie.searchPrefix("zzz");
    CHECK_EQ(emptyResult.size(), std::size_t{0});
    CHECK(trie.containsExact("zzz") == false);
    CHECK(trie.remove("zzz", 7) == false); // unknown name -> false
}

TEST(trie, empty_prefix_matches_everything)
{
    ds::Trie trie;
    trie.insert("main.cpp", 1);
    trie.insert("utils.h", 2);
    CHECK(containsExactly(trie.searchPrefix(""), {1, 2}));
}

TEST(trie, deep_prefix_chain)
{
    ds::Trie trie; // increasingly detailed names that extend each other
    trie.insert("a", 1);
    trie.insert("au", 2);
    trie.insert("aut", 3);
    trie.insert("auto", 4);
    trie.insert("automated", 5);

    CHECK(trie.containsExact("a"));
    CHECK(trie.containsExact("automated"));
    CHECK(trie.containsExact("automatedxxxxx") == false);
    CHECK(containsExactly(trie.searchPrefix("aut"), {3, 4, 5}));
    CHECK(containsExactly(trie.searchPrefix("auto"), {4, 5}));
    CHECK(containsExactly(trie.searchPrefix("automated"), {5}));
}

TEST(trie, case_sensitivity_is_byte_exact)
{
    ds::Trie trie;
    trie.insert("Utils.h", 1);
    trie.insert("utils.h", 2);

    CHECK(trie.containsExact("Utils.h"));
    CHECK(trie.containsExact("utils.h"));
    CHECK(trie.containsExact("UTILS.H") == false);

    CHECK(containsExactly(trie.searchPrefix("util"), {2}));
    CHECK(containsExactly(trie.searchPrefix("Util"), {1}));
    CHECK_EQ(trie.searchPrefix("persist").size(), std::size_t{0});
}

TEST(trie, remove_and_clear_behaviour)
{
    ds::Trie trie;
    trie.insert("api.h", 5);
    trie.insert("api.h", 9);
    CHECK(trie.remove("api.h", 5)); // one id out
    CHECK_EQ(trie.size(), std::size_t{1});
    CHECK(containsExactly(trie.searchPrefix("api"), {9}));
    CHECK(trie.containsExact("api.h")); // still terminal for id 9

    CHECK(trie.remove("api.h", 9)); // last id: no longer a complete name
    CHECK_EQ(trie.size(), std::size_t{0});
    CHECK(trie.containsExact("api.h") == false);
    CHECK_EQ(trie.searchPrefix("api").size(), std::size_t{0}); // subtree unreachable for search

    trie.insert("stdlib.h", 3); // reuse
    CHECK_EQ(trie.searchPrefix("std").size(), std::size_t{1});

    trie.clear();
    CHECK_EQ(trie.size(), std::size_t{0});
    CHECK(trie.containsExact("stdlib.h") == false);
    CHECK(trie.insert("fresh.h", 1)); // reusable after clear
    CHECK(containsExactly(trie.searchPrefix("fres"), {1}));
}

TEST(trie, move_steals_and_moved_from_is_reusable)
{
    ds::Trie source;
    source.insert("api.h", 1);
    source.insert("utils.h", 2);

    ds::Trie target(std::move(source));
    CHECK_EQ(source.size(), std::size_t{0});      // moved-from: empty
    CHECK(source.containsExact("api.h") == false);
    CHECK(containsExactly(target.searchPrefix("api"), {1}));

    ds::Trie target2;
    target2.insert("x.h", 9);
    target2 = std::move(target);
    CHECK_EQ(target.size(), std::size_t{0});      // drained by assignment
    CHECK(containsExactly(target2.searchPrefix("api"), {1}));
    CHECK(target2.containsExact("x.h") == false); // old contents replaced

    CHECK(source.insert("d.h", 4)); // moved-from trie reusable
    CHECK(containsExactly(source.searchPrefix("d"), {4}));
}

TEST(trie, stress_shared_prefixes_10k_names)
{
    ds::Trie trie;
    const int n = 10000;
    for (int i = 0; i < n; ++i)
        CHECK(trie.insert("util_string_" + std::to_string(i), i));
    CHECK_EQ(trie.size(), std::size_t{n});

    // Shared long prefix "util_string_" must be stored once, not 10k times.
    CHECK(trie.searchPrefix("util_string_1").contains(1));   // id 1 among 1,10..19,100..199,1000..1999
    CHECK(trie.searchPrefix("util_string_2").contains(2));   // similarly for id 2

    ds::LinkedList<int> all = trie.searchPrefix("util_string_");
    CHECK_EQ(all.size(), std::size_t{n}); // every id found, none duplicated

    for (int i = 0; i < n; i += 2500) // spot checks across the range
        CHECK(trie.containsExact("util_string_" + std::to_string(i)));

    trie.clear();
    CHECK_EQ(trie.size(), std::size_t{0});
    CHECK_EQ(trie.searchPrefix("util_string_").size(), std::size_t{0});
}

// ===========================================================================
// MAX HEAP tests (TESTING_STRATEGY.md §3.6 — heap invariant walked from the
// raw array after series of ops, deterministic tie-break, buildFrom ≡ insert
// equivalence, clear/move reuse, 10k stress)
// ===========================================================================

using Entry = ds::MaxHeap::Entry;

static bool heapInvariantHolds(const ds::MaxHeap& heap)
{
    const std::size_t n = heap.size();
    for (std::size_t i = 1; i < n; ++i)
    {
        // parent must dominate child under the documented ordering
        if (ds::MaxHeap::higherPriority(heap.rawEntryAt(i), heap.rawEntryAt((i - 1) / 2)))
            return false;
    }
    return true;
}

TEST(max_heap, empty_state)
{
    ds::MaxHeap heap;
    CHECK(heap.isEmpty());
    CHECK_EQ(heap.size(), std::size_t{0});
    heap.clear(); // idempotent on empty
    CHECK(heap.isEmpty());
    // NOTE: peekMax()/extractMax() on empty go through assert() (project
    // policy — cannot be observed without killing the test process).
}

TEST(max_heap, single_entry_peek_then_extract)
{
    ds::MaxHeap heap;
    heap.insert(Entry{7, 1});
    CHECK(!heap.isEmpty());
    CHECK_EQ(heap.size(), std::size_t{1});

    const Entry& peeked = heap.peekMax();
    CHECK_EQ(peeked.score, 7);
    CHECK_EQ(peeked.fileId, 1);
    CHECK_EQ(heap.size(), std::size_t{1}); // peek did not extract

    const Entry extracted = heap.extractMax();
    CHECK_EQ(extracted.score, 7);
    CHECK_EQ(extracted.fileId, 1);
    CHECK(heap.isEmpty());
}

TEST(max_heap, multiple_insertions_report_maximum)
{
    ds::MaxHeap heap;
    heap.insert(Entry{7, 1});
    heap.insert(Entry{42, 2});
    heap.insert(Entry{13, 3});
    CHECK_EQ(heap.size(), std::size_t{3});
    CHECK_EQ(heap.peekMax().score, 42);
    CHECK_EQ(heap.peekMax().fileId, 2);
    CHECK(heapInvariantHolds(heap));
}

TEST(max_heap, extraction_is_descending_order)
{
    ds::MaxHeap heap;
    heap.insert(Entry{7, 1});
    heap.insert(Entry{42, 2});
    heap.insert(Entry{13, 3});
    heap.insert(Entry{99, 4});
    heap.insert(Entry{5, 5});

    CHECK_EQ(heap.extractMax().score, 99);
    CHECK_EQ(heap.extractMax().score, 42);
    CHECK_EQ(heap.extractMax().score, 13);
    CHECK_EQ(heap.extractMax().score, 7);
    CHECK_EQ(heap.extractMax().score, 5);
    CHECK(heap.isEmpty());

    heap.insert(Entry{1, 9}); // reusable after the drain
    CHECK_EQ(heap.extractMax().score, 1);
}

TEST(max_heap, equal_scores_tie_broken_by_smaller_file_id)
{
    ds::MaxHeap heap;
    // Same score, ids inserted SHUFFLED — extraction must be id-ascending:
    // 5, 7, 9 anywhere the inputs come (graph assigns ids in sorted order).
    heap.insert(Entry{5, 9});
    heap.insert(Entry{5, 5});
    heap.insert(Entry{5, 7});
    CHECK_EQ(heap.extractMax().fileId, 5);
    CHECK_EQ(heap.extractMax().fileId, 7);
    CHECK_EQ(heap.extractMax().fileId, 9);
    CHECK(heap.isEmpty());

    // And smaller scoring beats tie? no: score dominates id — sanity check.
    heap.insert(Entry{6, 9});
    heap.insert(Entry{5, 5});
    CHECK_EQ(heap.peekMax().fileId, 9); // score 6 > 5 despite bigger id
}

TEST(max_heap, invariant_holds_after_series_of_ops)
{
    ds::MaxHeap heap;
    CHECK(heapInvariantHolds(heap)); // empty heap vacuously valid
    for (int i = 1; i <= 32; ++i)
        heap.insert(Entry{(i * 37) % 13, i}); // many duplicate scores
    CHECK(heapInvariantHolds(heap));
    for (int i = 0; i < 16; ++i)
    {
        heap.extractMax();
        CHECK(heapInvariantHolds(heap)); // after every extraction
    }
    for (int i = 100; i < 124; ++i)
        heap.insert(Entry{i % 4, i});
    CHECK(heapInvariantHolds(heap));
}

TEST(max_heap, clear_and_reuse)
{
    ds::MaxHeap heap;
    heap.insert(Entry{3, 1});
    heap.insert(Entry{4, 2});
    heap.clear();
    CHECK(heap.isEmpty());
    CHECK_EQ(heap.size(), std::size_t{0});

    heap.insert(Entry{9, 3}); // reusable — and resize/grow path re-checks
    CHECK_EQ(heap.peekMax().fileId, 3);

    // Grow beyond several doublings to exercise ensureCapacity repeatedly.
    for (int i = 0; i < 200; ++i)
        heap.insert(Entry{50, i});
    CHECK_EQ(heap.size(), std::size_t{201}); // 1 reuse insert + 200 loop
    CHECK(heapInvariantHolds(heap));
}

TEST(max_heap, move_steals_and_moved_from_is_reusable)
{
    ds::MaxHeap source;
    source.insert(Entry{10, 1});
    source.insert(Entry{20, 2});

    ds::MaxHeap target(std::move(source));
    CHECK(source.isEmpty()); // moved-from: empty
    CHECK_EQ(target.size(), std::size_t{2});
    CHECK_EQ(target.peekMax().score, 20);

    ds::MaxHeap target2;
    target2.insert(Entry{1, 99});
    target2 = std::move(target);
    CHECK(target.isEmpty()); // assign-source drained
    CHECK_EQ(target2.size(), std::size_t{2});
    CHECK_EQ(target2.peekMax().fileId, 2); // stale "1/99" replaced

    source.insert(Entry{3, 7}); // moved-from heap immediately reusable
    CHECK_EQ(source.extractMax().fileId, 7);
}

TEST(max_heap, build_from_equivalent_to_repeated_insert)
{
    // Deterministic pseudo-random-ish duplicates + tie-breaks at n=4000:
    // extraction sequences of (a) insert-built and (b) heapify-built MUST be
    // identical, because with distinct (score,id) pairs the comparator is a
    // strict total order.
    ds::DynArray<Entry> entries(4000);
    for (int i = 0; i < 4000; ++i)
        entries[i] = Entry{(i * 37) % 88, i}; // heavy duplication of scores

    // (a) incremental inserts
    ds::MaxHeap byInsert;
    for (std::size_t i = 0; i < entries.size(); ++i)
        byInsert.insert(entries[i]);
    CHECK(heapInvariantHolds(byInsert));

    // (b) O(n) bottom-up heapify
    ds::MaxHeap byHeapify;
    byHeapify.buildFrom(entries);
    CHECK(heapInvariantHolds(byHeapify));

    // identical extraction sequences
    while (!byHeapify.isEmpty())
    {
        CHECK_EQ(byInsert.extractMax().fileId, byHeapify.extractMax().fileId);
        CHECK_EQ(byInsert.size(), byHeapify.size()); // stay in lockstep
    }
    CHECK(byInsert.isEmpty());
    CHECK(byHeapify.isEmpty());
}

TEST(max_heap, stress_10k)
{
    ds::MaxHeap heap;
    const int n = 10000;
    for (int i = 0; i < n; ++i)
        heap.insert(Entry{(i * 31) % 1000, i}); // only 1000 distinct scores
    CHECK_EQ(heap.size(), std::size_t{n});
    CHECK(heapInvariantHolds(heap));

    long long sumScores = 0;
    int previousScore = 2000000000; // above any input score
    while (!heap.isEmpty())
    {
        const Entry top = heap.extractMax();
        CHECK(top.score <= previousScore); // non-increasing extraction
        previousScore = top.score;
        sumScores += top.score;
    }
    // (i*31) % 1000 is a permutation of 0..999 within every block of 1000
    // indices (gcd(31,1000)==1); n = 10 blocks => 10 * sum(0..999).
    CHECK_EQ(sumScores, 4995000LL);
    heap.insert(Entry{1, 1}); // reuse after full drain
    CHECK_EQ(heap.extractMax().score, 1);
}
