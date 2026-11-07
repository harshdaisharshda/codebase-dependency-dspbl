#pragma once
// src/ds/queue.h
//
// FIFO queue built ON TOP of the custom ds::LinkedList (reuses its node
// management and RAII — no duplicated new/delete logic per AGENTS.md).
//
// Project roles (approved synopsis mapping): BFS working queue for the
// level-by-level impact analysis (F3, B1/B2/B4/B5/B6 traversal variants) and
// Kahn's topological ordering (F4) — all of which seed multiple sources and
// expand level by level.
//
// Policy (established with LinkedList/Stack):
//  - empty-queue dequeue()/front() is a *programming error*: assert() in Debug,
//    never UB;
//  - enqueue appends at the TAIL (O(1) via the list's tail pointer), dequeue
//    pops from the HEAD (O(1)) — classic FIFO discipline;
//  - moved-from queues are empty and reusable;
//  - copies deleted implicitly (LinkedList is non-copyable) — move instead;
//  - no STL container is the underlying storage.

#include "ds/linked_list.h"

#include <cstddef>

namespace ds
{

template <typename T>
class Queue
{
public:
    Queue() = default;

    Queue(Queue&&) noexcept = default;
    Queue& operator=(Queue&&) noexcept = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    ~Queue() = default; // LinkedList destructor frees every node (RAII)

    // O(1) — appends at the tail.
    void enqueue(const T& value) { list_.append(value); }

    // Removes and returns the oldest element. Precondition: !isEmpty().
    T dequeue() { return list_.popFront(); }

    // Precondition: !isEmpty().
    const T& front() const { return list_.front(); }

    std::size_t size() const { return list_.size(); }
    bool isEmpty() const { return list_.isEmpty(); }

    // O(n): frees every node; the queue is reusable afterwards.
    void clear() { list_.clear(); }

private:
    LinkedList<T> list_; // head == next to dequeue, tail == most recently enqueued
};

} // namespace ds
