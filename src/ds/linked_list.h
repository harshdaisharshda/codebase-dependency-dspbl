#pragma once
// src/ds/linked_list.h
//
// Singly linked list — the adjacency-list foundation of the dependency graph
// (graph nodes store LinkedList<Edge>), hash-table chaining (buckets are small
// lists), and result containers for traversal outputs.
//
// Design (AGENTS.md / ARCHITECTURE.md):
//  - O(1) append via a tail pointer (adjacency lists mostly append).
//  - Copy is DELETED (deep copies have no use here); moves steal the nodes.
//  - Empty-list access to front/popFront/at is a *programming error* and is
//    guarded by assert() in Debug (AGENTS.md: no exceptions across ds; UB never).
//  - Raw new/delete appear only inside this class (memory-ownership rule).

#include <cassert>
#include <cstddef>
#include <utility>

namespace ds
{

template <typename T>
class LinkedList
{
    struct Node
    {
        T value;
        Node* next = nullptr;

        Node(const T& v, Node* n) : value(v), next(n) {}
        explicit Node(T&& v, Node* n) : value(std::move(v)), next(n) {}
    };

public:
    class Iterator
    {
    public:
        explicit Iterator(const Node* node = nullptr) : node_(node) {}

        const T& operator*() const { return node_->value; }

        Iterator& operator++()
        {
            node_ = node_->next;
            return *this;
        }

        bool operator==(const Iterator& other) const { return node_ == other.node_; }
        bool operator!=(const Iterator& other) const { return node_ != other.node_; }

    private:
        const Node* node_; // non-owning view; see class comment
    };

    LinkedList() = default;
    ~LinkedList() { clear(); }

    LinkedList(LinkedList&& other) noexcept
        : head_(other.head_), tail_(other.tail_), size_(other.size_)
    {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }

    LinkedList& operator=(LinkedList&& other) noexcept
    {
        if (this != &other)
        {
            clear();
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    // Copying a linked list node-by-node is never wanted in our hot paths;
    // a silent O(n) deep copy would hide real cost. Deleted per AGENTS.md.
    LinkedList(const LinkedList&) = delete;
    LinkedList& operator=(const LinkedList&) = delete;

    // --- insertion ---------------------------------------------------------

    void append(const T& value)
    {
        Node* node = new Node(value, nullptr);
        if (head_ == nullptr)
            head_ = tail_ = node;
        else
        {
            tail_->next = node;
            tail_ = node;
        }
        ++size_;
    }

    void pushFront(const T& value)
    {
        head_ = new Node(value, head_);
        if (tail_ == nullptr)
            tail_ = head_;
        ++size_;
    }

    // Removes the first element and returns it. Precondition: !isEmpty().
    T popFront()
    {
        assert(head_ != nullptr && "LinkedList::popFront on empty list");
        Node* node = head_;
        head_ = node->next;
        if (head_ == nullptr)
            tail_ = nullptr;
        --size_;
        T value(std::move(node->value));
        delete node;
        return value;
    }

    void clear()
    {
        while (head_ != nullptr)
        {
            Node* next = head_->next;
            delete head_;
            head_ = next;
        }
        tail_ = nullptr;
        size_ = 0;
    }

    // --- deletion ----------------------------------------------------------

    // Deletes the first node whose value satisfies pred. O(n). Iterator-safe
    // usage from HashTable bucket unlinking (Phase 1.4).
    template <typename Pred>
    bool removeIf(Pred pred)
    {
        Node* prev = nullptr;
        Node* current = head_;
        while (current != nullptr)
        {
            if (pred(current->value))
            {
                if (prev == nullptr)
                    head_ = current->next;
                else
                    prev->next = current->next;
                if (current == tail_)
                    tail_ = prev;
                delete current;
                --size_;
                return true;
            }
            prev = current;
            current = current->next;
        }
        return false;
    }

    // Deletes the first node whose value == value (needs T::operator==). O(n).
    bool removeFirst(const T& value) { return removeIf([&](const T& v) { return v == value; }); }

    // --- lookup/traversal --------------------------------------------------

    // O(n). Needs T::operator==.
    bool contains(const T& value) const
    {
        for (const Node* node = head_; node != nullptr; node = node->next)
            if (node->value == value)
                return true;
        return false;
    }

    bool isEmpty() const { return head_ == nullptr; }
    std::size_t size() const { return size_; }

    // Precondition: !isEmpty().
    const T& front() const
    {
        assert(head_ != nullptr && "LinkedList::front on empty list");
        return head_->value;
    }

    // O(n) walk. Precondition: index < size().
    const T& at(std::size_t index) const
    {
        assert(index < size_ && "LinkedList::at out of range");
        const Node* node = head_;
        for (std::size_t i = 0; i < index; ++i)
            node = node->next;
        return node->value;
    }

    Iterator begin() const { return Iterator(head_); }
    Iterator end() const { return Iterator(nullptr); }

private:
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace ds
