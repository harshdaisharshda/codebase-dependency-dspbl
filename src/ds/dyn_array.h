#pragma once
// src/ds/dyn_array.h
//
// Minimal dynamic array used INTERNALLY as HashTable bucket storage (Phase
// 1.4) and later as the dist/parent/visited/colour tables inside graph
// algorithms (Phase 2+). It is NOT one of the 8 required structures — it is a
// small building block whose algorithmic role (flat indexed storage) never
// replaces a required Linked List/Stack/Queue.
//
// Phase 2 addition: dynamic growth via emplaceBack (amortized doubling) so the
// dependency graph can grow its node table and adjacency lists incrementally
// while a project is scanned, without pre-knowing the node count.
//
// Policy (project-wide):
//  - raw new[]/delete[] belong ONLY inside this class (RAII: destructor frees);
//  - copy deleted (would silently be O(n)); move steals;
//  - operator[] on an out-of-range index is a *programming error* → assert.
//
// Growth semantics (Phase 2, approved A1):
//  - `new T[n]()` default-initialises every slot, so the array never exposes
//    junk memory and there is no out-of-band "constructed" bookkeeping to get
//    wrong; appending therefore = build the value locally, then move-assign it
//    into the next free slot. This works for move-only T (e.g. ds::LinkedList,
//    the graph adjacency lists — copy construction is deleted by design).
//  - Introduced instead of separate "IntArray/BoolArray" (DEVELOPMENT_PLAN
//    1.7): one generic growable array serves both the node table (strings) and
//    the algorithm tables (int/bool) — fewer overlapping insertion interfaces,
//    per the keep-the-API-small rule.

#include <cassert>
#include <cstddef>
#include <utility>

namespace ds
{

template <typename T>
class DynArray
{
public:
    DynArray() = default;

    // Creates an array of `count` value-initialised elements (int → 0, class
    // types → default-constructed) so the array never exposes junk memory.
    explicit DynArray(std::size_t count)
        : data_(new T[count]()), size_(count), capacity_(count)
    {
    }

    ~DynArray() { delete[] data_; }

    DynArray(DynArray&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    DynArray& operator=(DynArray&& other) noexcept
    {
        if (this != &other)
        {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }

    DynArray(const DynArray&) = delete;
    DynArray& operator=(const DynArray&) = delete;

    // Precondition: index < size().
    T& operator[](std::size_t index)
    {
        assert(index < size_ && "DynArray::operator[] out of range");
        return data_[index];
    }

    const T& operator[](std::size_t index) const
    {
        assert(index < size_ && "DynArray::operator[] out of range");
        return data_[index];
    }

    // Appends one element (amortized O(1)); capacity doubles when full, never
    // growing an empty array a slot at a time (small initial floor).
    // With no arguments it default-constructs the value; with move-only T
    // pass e.g. emplaceBack(std::move(list)). The value is built FIRST,
    // before grow() runs — that order matters: an argument may be
    // `std::move(someArray[i])`, i.e. reference a slot of this same array,
    // and building `value` before the reallocation guarantees that reference
    // is still in live storage when it is read.
    // Returns a reference to the stored element — INVALID after any further
    // growth; use it immediately or re-index with operator[].
    template <typename... Args>
    T& emplaceBack(Args&&... args)
    {
        T value(std::forward<Args>(args)...);
        if (size_ == capacity_)
            grow();
        T* slot = &data_[size_];
        *slot = std::move(value);
        ++size_;
        return *slot;
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    bool isEmpty() const { return size_ == 0; }

private:
    // Replaces the storage with a doubled-capacity array: old elements are
    // MOVE-ASSIGNED into the fresh array (copy is impossible for move-only T),
    // and `new T[n]()` default-initialises every slot past the moved range.
    void grow()
    {
        std::size_t freshCapacity =
            capacity_ == 0 ? kInitialCapacity : capacity_ * 2;
        T* fresh = new T[freshCapacity]();
        for (std::size_t i = 0; i < size_; ++i)
            fresh[i] = std::move(data_[i]);
        delete[] data_;
        data_ = fresh;
        capacity_ = freshCapacity;
    }

    static constexpr std::size_t kInitialCapacity = 16;

    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

} // namespace ds
