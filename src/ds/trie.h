#pragma once
// src/ds/trie.h
//
// Prefix trie over FILENAMES for the synopsis search feature (F7): find files
// by name prefix and check whether a file exists, autocomplete-style.
//
// Keyed design (approved decision #5 fits here):
//  - the trie key is the FILE BASENAME (e.g. "api.h");
//  - each terminal node holds a deduplicated list of graph NODE IDs, so two
//    files with the same basename in different directories (network/api.h,
//    database/api.h) resolve through the SAME terminal node and BOTH ids are
//    returned for prefix "api";
//  - project-relative paths remain the graph's identity (HashTable path->id in
//    Phase 3); translating id -> path happens in analysis code via the graph
//    node table (integration later, not here).
//
// Children representation (documented choice): an own ds::LinkedList of
// {char, Node*} edges — NOT an STL associative container, and deliberately not
// a fixed 128-slot array (that would be ~1 KB per node: >100 MB on realistic
// scans). Filename fanout per node is tiny, so walking the short edges list is
// cheap. Consequence: O(fanout) scan per character — see complexity below.
//
// Semantics (documented choices):
//  - keys and comparisons are BYTE-EXACT: case-sensitive ("API.h" != "api.h").
//  - insert(name, id) makes `name` a terminal carrying `id`; duplicate
//    (name, id) pairs are rejected (false), no id ever stored twice.
//  - searchPrefix returns ids of ALL names under the prefix, DEDUPLICATED (one
//    id may be present under two different names).
//  - remove(name, id) drops that id from the terminal; structural nodes are
//    NOT pruned (the architecture rebuilds the whole trie via clear() on
//    re-scan; pruning is out of scope and safe to skip).
//  - moved-from tries are empty-but-reusable; copies deleted (raw ownership).
//
// Complexity: L = key length, F = children fanout scanned per character
// (<= 255, usually a few), S = subtree size, K = result size.
//   insert / containsExact / remove : O(L * F)
//   searchPrefix                    : O(prefix walk + S + K^2 dedup)
//   clear                           : O(total stored nodes)
//   space                           : O(unique characters across stored names)

#include "ds/linked_list.h"

#include <cstddef>
#include <string>

namespace ds
{

class Trie
{
public:
    Trie() : root_(new Node()) {}

    // Hand-written (not defaulted) move: a plain Node* + size_t would be
    // *copied* by a defaulted move, breaking the moved-from-is-empty rule.
    Trie(Trie&& other) noexcept : root_(other.root_), size_(other.size_)
    {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    Trie& operator=(Trie&& other) noexcept
    {
        if (this != &other)
        {
            destroySubtree(root_);
            root_ = other.root_;
            size_ = other.size_;
            other.root_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    Trie(const Trie&) = delete;
    Trie& operator=(const Trie&) = delete;

    ~Trie() { destroySubtree(root_); }

    // Inserts (name, fileId). Returns false (nothing changes) when the same
    // pair already exists; a new id on an already-terminal name IS allowed.
    bool insert(const std::string& name, int fileId)
    {
        if (root_ == nullptr)
            root_ = new Node(); // moved-from safety
        Node* terminal = walkOrCreate(name);
        if (terminal->isTerminal && terminal->fileIds.contains(fileId))
            return false; // duplicate (name, fileId)
        if (!terminal->isTerminal)
            terminal->isTerminal = true;
        terminal->fileIds.append(fileId);
        ++size_;
        return true;
    }

    // True when the exact name was inserted (regardless of how many ids).
    bool containsExact(const std::string& name) const
    {
        const Node* terminal = walkExisting(name);
        return terminal != nullptr && terminal->isTerminal;
    }

    // Ids of every stored name under `prefix`, deduplicated. Empty list when
    // nothing matches (also for ""): never null, never crashes. NOTE: prefix
    // "" matches every stored name by definition.
    LinkedList<int> searchPrefix(const std::string& prefix) const
    {
        LinkedList<int> result;
        const Node* start = walkExisting(prefix);
        if (start != nullptr)
            collectFrom(start, result);
        return result;
    }

    // Drops one id from the terminal `name`; false when the name is unknown,
    // not terminal, or does not carry the id.
    bool remove(const std::string& name, int fileId)
    {
        if (root_ == nullptr) // moved-from / cleared trie: nothing to remove
            return false;
        Node* terminal = walkExistingMutable(name);
        if (terminal == nullptr || !terminal->isTerminal ||
            !terminal->fileIds.contains(fileId))
            return false;
        terminal->fileIds.removeFirst(fileId);
        --size_;
        if (terminal->fileIds.isEmpty())
            terminal->isTerminal = false;
        return true;
    }

    // Number of stored (name, fileId) pairs.
    std::size_t size() const { return size_; }

    // Frees the whole tree; the trie is reusable afterwards.
    void clear()
    {
        destroySubtree(root_);
        root_ = nullptr;
        size_ = 0;
    }

private:
    struct Node; // forward declaration; defined right below

    struct ChildEdge
    {
        char ch;
        Node* child; // owned by the trie
    };

    struct Node
    {
        bool isTerminal = false;
        LinkedList<int> fileIds;       // deduplicated at insert
        LinkedList<ChildEdge> children; // own-list edges (see class comment)
    };

    Node* walkOrCreate(const std::string& text)
    {
        Node* node = root_;
        for (unsigned char ch : text)
        {
            Node* next = childOf(node, ch);
            if (next == nullptr)
            {
                next = new Node();
                node->children.append(ChildEdge{static_cast<char>(ch), next});
            }
            node = next;
        }
        return node;
    }

    const Node* walkExisting(const std::string& text) const
    {
        if (root_ == nullptr)
            return nullptr;
        const Node* node = root_;
        for (unsigned char ch : text)
        {
            node = childOf(node, ch);
            if (node == nullptr)
                return nullptr;
        }
        return node;
    }

    // Read-only walk that never creates — same as walkExisting but returns a
    // mutable node for remove().
    Node* walkExistingMutable(const std::string& text)
    {
        if (root_ == nullptr)
            return nullptr;
        Node* node = root_;
        for (unsigned char ch : text)
        {
            node = childOf(node, ch);
            if (node == nullptr)
                return nullptr;
        }
        return node;
    }

    static Node* childOf(Node* node, unsigned char ch)
    {
        for (const ChildEdge& edge : node->children)
            if (static_cast<unsigned char>(edge.ch) == ch)
                return edge.child;
        return nullptr;
    }

    static const Node* childOf(const Node* node, unsigned char ch)
    {
        for (const ChildEdge& edge : node->children)
            if (static_cast<unsigned char>(edge.ch) == ch)
                return edge.child;
        return nullptr;
    }

    // Collects ids from the subtree, skipping duplicates.
    static void collectFrom(const Node* node, LinkedList<int>& out)
    {
        if (node->isTerminal)
            for (int id : node->fileIds)
                if (!out.contains(id))
                    out.append(id);
        for (const ChildEdge& edge : node->children)
            collectFrom(edge.child, out);
    }

    static void destroySubtree(Node* node)
    {
        if (node == nullptr)
            return;
        for (const ChildEdge& edge : node->children)
            destroySubtree(edge.child);
        node->children.clear();
        delete node;
    }

    Node* root_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace ds
