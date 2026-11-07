#pragma once
// src/ds/max_heap.h
//
// Array-backed binary max-heap over (score, fileId) entries — ranks modules
// by in-degree ("most depended-upon modules", synopsis F6) and mirror
// rankings by out-degree ("include-heavy"). Same structure, different score.
//
// Storage: ds::DynArray<Entry> (our RAII primitive) + separate count_ so an
// extractMax simply shrinks the logical heap without reallocating.
//
// Ordering (deterministic, documented):
//   higherPriority(a, b):
//     a.score > b.score, OR
//     equal scores  ->  smaller fileId wins.
//   Rationale: Phase 2 assigns graph node ids in sorted directory-walk order,
//   so "smaller id first" reproduces the approved deterministic
//   lexicographic-by-path tie-break at the graph level.
//
// Move semantics: hand-written (a defaulted move would *copy* count_ and
// break the moved-from-is-empty invariant — same trap found in Phase 1.4).
// Copies deleted. Copies of Entry itself are trivial ({int,int}) and kept.
//
// Empty-heap peekMax()/extractMax(): programming error -> assert (project
// convention; tests document but cannot observe process-death).
//
// Complexity:
//   insert / extractMax : O(log n)
//   peekMax, size       : O(1)
//   buildFrom (O(n) heapify, see below) : O(n)
//   clear               : O(1) (lazy — logical count reset, storage reused)
//   space               : O(n)
//
// Bottom-up construction (buildFrom): INCLUDED because the graph knows all V
// in-degrees at once at finalize() time; heapifying a complete array is
// O(n) versus O(n log n) with repeated inserts. insert stays for incremental
// use.

#include "ds/dyn_array.h"

#include <cassert>
#include <cstddef>
#include <utility>

namespace ds
{

class MaxHeap
{
public:
    struct Entry
    {
        int score;  // e.g. in-degree (dependents count) of the file
        int fileId; // graph node id of the file
    };

    static constexpr std::size_t kInitialCapacity = 16;

    // strict-ish total order: true when a must be extracted before b
    static bool higherPriority(const Entry& a, const Entry& b)
    {
        if (a.score != b.score)
            return a.score > b.score;
        return a.fileId < b.fileId; // deterministic tie-break (see class doc)
    }

    MaxHeap() = default;

    MaxHeap(MaxHeap&& other) noexcept
        : storage_(std::move(other.storage_)), count_(other.count_)
    {
        other.count_ = 0;
    }

    MaxHeap& operator=(MaxHeap&& other) noexcept
    {
        if (this != &other)
        {
            storage_ = std::move(other.storage_);
            count_ = other.count_;
            other.count_ = 0;
        }
        return *this;
    }

    MaxHeap(const MaxHeap&) = delete;
    MaxHeap& operator=(const MaxHeap&) = delete;
    ~MaxHeap() = default; // DynArray frees storage (RAII)

    // O(log n). Maintains the heap property via sift-up.
    void insert(const Entry& entry)
    {
        ensureCapacity();
        storage_[count_] = entry;
        ++count_;
        siftUp(count_ - 1);
    }

    // O(1). Precondition: !isEmpty().
    const Entry& peekMax() const
    {
        assert(!isEmpty() && "MaxHeap::peekMax on empty heap");
        return storage_[0];
    }

    // O(log n). Removes and returns the highest-priority entry.
    // Precondition: !isEmpty().
    Entry extractMax()
    {
        assert(!isEmpty() && "MaxHeap::extractMax on empty heap");
        Entry top = storage_[0];
        --count_;
        if (count_ > 0)
        {
            storage_[0] = storage_[count_]; // move last into the hole
            siftDown(0);
        }
        return top;
    }

    // O(n) heapify (bottom-up Floyd construction): builds from a complete
    // entry array — the graph hubs path where all V scores are known at once.
    // Reuses the storage when it is large enough.
    void buildFrom(const DynArray<Entry>& entries)
    {
        if (storage_.size() < entries.size())
            storage_ = DynArray<Entry>(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i)
            storage_[i] = entries[i];
        count_ = entries.size();
        if (count_ >= 2) // leaves (n/2..n-1) are trivially valid heaps
            for (std::size_t parent = count_ / 2; parent-- > 0;)
                siftDown(parent);
    }

    std::size_t size() const { return count_; }
    bool isEmpty() const { return count_ == 0; }

    // Logical reset; storage is kept for reuse (matches HashTable::clear).
    void clear() { count_ = 0; }

    // Test/diagnostic view of the live heap array (NOT an ordered view):
    // an entry position only follows the binary-heap layout (parent at
    // (i-1)/2). Precondition: index < size().
    const Entry& rawEntryAt(std::size_t index) const
    {
        assert(index < count_ && "rawEntryAt beyond logical heap");
        return storage_[index];
    }

private:
    void ensureCapacity()
    {
        if (storage_.size() == 0)
            storage_ = DynArray<Entry>(kInitialCapacity);
        else if (count_ == storage_.size())
        {
            DynArray<Entry> bigger(storage_.size() * 2);
            for (std::size_t i = 0; i < count_; ++i)
                bigger[i] = storage_[i];
            storage_ = std::move(bigger);
        }
    }

    void siftUp(std::size_t hole)
    {
        const Entry value = storage_[hole]; // copy: {int,int}
        while (hole > 0)
        {
            const std::size_t parent = (hole - 1) / 2;
            if (!higherPriority(value, storage_[parent]))
                break;
            storage_[hole] = storage_[parent];
            hole = parent;
        }
        storage_[hole] = value;
    }

    void siftDown(std::size_t hole)
    {
        const Entry value = storage_[hole];
        while (true)
        {
            const std::size_t left = hole * 2 + 1;
            if (left >= count_)
                break; // hole is a leaf
            std::size_t child = left;
            const std::size_t right = left + 1;
            if (right < count_ &&
                higherPriority(storage_[right], storage_[left]))
                child = right;
            if (!higherPriority(storage_[child], value))
                break; // value already dominates its best child
            storage_[hole] = storage_[child];
            hole = child;
        }
        storage_[hole] = value;
    }

    DynArray<Entry> storage_; // sized >= count_ once first insert runs
    std::size_t count_ = 0;
};

} // namespace ds
