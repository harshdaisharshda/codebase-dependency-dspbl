#pragma once
// src/ds/dyn_array.h
//
// Minimal dynamic array used INTERNALLY as HashTable bucket storage (Phase
// 1.4) and later as the dist/parent/visited/colour tables inside graph
// algorithms (Phase 2+). It is NOT one of the 8 required structures — it is a
// small building block whose algorithmic role (flat indexed storage) never
// replaces a required Linked List/Stack/Queue.
//
// Policy (project-wide):
//  - raw new[]/delete[] belong ONLY inside this class (RAII: destructor frees);
//  - copy deleted (would silently be O(n)); move steals;
//  - operator[] on an out-of-range index is a *programming error* → assert.

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

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
    bool isEmpty() const { return size_ == 0; }

private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

} // namespace ds
