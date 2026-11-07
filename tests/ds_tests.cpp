// tests/ds_tests.cpp
// Unit tests for the custom data structures, one suite per structure,
// following TESTING_STRATEGY.md §3.x. Suite `linked_list` = §3.1 checklist.

#include "ds/dyn_array.h"
#include "ds/hash_table.h"
#include "ds/linked_list.h"
#include "ds/queue.h"
#include "ds/stack.h"

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
