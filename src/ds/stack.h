#pragma once
// src/ds/stack.h
//
// LIFO stack built ON TOP of the custom ds::LinkedList (reuses its node
// management and RAII — no duplicated new/delete logic per AGENTS.md).
//
// Project roles (approved decision #3): dependency-path / cycle reconstruction
// in F2 (cycles) and F8 (path search) — push a parent chain, pop it to print
// in correct edge order. Not the DFS traversal container by design (DFS is
// recursive per the synopsis "Recursion" topic + depth guard).
//
// Policy (established with LinkedList):
//  - empty-stack pop()/top() is a *programming error*: assert() in Debug, never UB;
//  - moved-from stacks are empty and reusable;
//  - copies deleted implicitly (LinkedList is non-copyable) — move instead.

#include "ds/linked_list.h"

#include <cstddef>

namespace ds
{

template <typename T>
class Stack
{
public:
    Stack() = default;

    // Only member is a non-copyable, noexcept-movable LinkedList; default
    // special members give us exactly the ownership rules we want.
    Stack(Stack&&) noexcept = default;
    Stack& operator=(Stack&&) noexcept = default;
    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;
    ~Stack() = default; // LinkedList destructor frees every node (RAII)

    // Pushes onto the top (list head) — O(1), no value copy beyond the node insert.
    void push(const T& value) { list_.pushFront(value); }

    // Removes and returns the top element. Precondition: !isEmpty().
    T pop() { return list_.popFront(); }

    // Precondition: !isEmpty().
    const T& top() const { return list_.front(); }

    std::size_t size() const { return list_.size(); }
    bool isEmpty() const { return list_.isEmpty(); }

    // O(n): frees every node; the stack is reusable afterwards.
    void clear() { list_.clear(); }

private:
    LinkedList<T> list_; // head of list == top of stack
};

} // namespace ds
