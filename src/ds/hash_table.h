#pragma once
// src/ds/hash_table.h
//
// String-keyed hash table with separate chaining on our custom ds::LinkedList,
// FNV-1a 64-bit hashing, and dynamic doubling rehash when the load factor
// exceeds 0.75 (integer-exact check: count*4 > buckets*3).
//
// Project role (approved synopsis mapping): HashTable maps each file name /
// project-relative path to its node id while the dependency graph is built and
// queried (Phase 2/3 integration — NOT plugged in here yet); later analyses
// reuse the same name->id path.
//
// Policy / semantics (documented choices):
//  - find(key) returns const V* : nullptr == ABSENT. This distinguishes an
//    absent key from a stored value that happens to be zero.
//  - insert(key, value) returns false and leaves the table (and the stored
//    value) UNCHANGED when key already exists — no overwriting.
//  - contains / remove return bool; remove of an absent key is a no-op false.
//  - moved-from tables are empty-but-valid: the next insert re-creates the
//    initial bucket array.
//  - clear() empties every chain and resets size to 0 but keeps the bucket
//    array allocated for predictable reuse.
//  - no STL associative container is used; chains are the required LinkedList;
//    the bucket array is our ds::DynArray.
//
// Complexity (documented requirement):
//   average O(1) while load factor <= 0.75, worst case O(n) when every key
//   falls into one bucket (single-chain walk). Rehash is O(n) and happens at
//   each doubling of the load threshold -> amortized O(1) per insert.

#include "ds/dyn_array.h"
#include "ds/linked_list.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace ds
{

// FNV-1a, 64-bit variant (offset basis and prime are the public constants).
inline std::uint64_t fnv1aHash64(const std::string& text)
{
    std::uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : text)
    {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename V>
class HashTable
{
public:
    // Public for tests/diagnostics: hash of a single key (pre-modulo).
    static std::size_t hashFor(const std::string& key)
    {
        return static_cast<std::size_t>(fnv1aHash64(key));
    }

    HashTable() : buckets_(BucketArray(kInitialBuckets)) {}

    // Hand-written (not defaulted) move: DynArray's move transfers the bucket
    // array, but a plain size_t count_ would be *copied* by a defaulted move,
    // leaving the moved-from table claiming entries it no longer owns.
    HashTable(HashTable&& other) noexcept
        : buckets_(std::move(other.buckets_)), count_(other.count_)
    {
        other.count_ = 0;
    }

    HashTable& operator=(HashTable&& other) noexcept
    {
        if (this != &other)
        {
            buckets_ = std::move(other.buckets_); // drops old storage, steals
            count_ = other.count_;
            other.count_ = 0;
        }
        return *this;
    }
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;
    ~HashTable() = default;

    // Inserts (key, value). Returns false (and changes nothing) if key exists.
    // Amortized O(1) expected; O(chain length) worst case.
    bool insert(const std::string& key, const V& value)
    {
        if (buckets_.size() == 0)
            buckets_ = BucketArray(kInitialBuckets); // moved-from safety
        Bucket& bucket = buckets_[indexOf(key)];
        if (bucketContains(bucket, key))
            return false;
        bucket.append(Entry{key, value});
        ++count_;
        if (count_ * 4 > buckets_.size() * 3)
            rehash(buckets_.size() * 2); // double the bucket count
        return true;
    }

    // nullptr means the key is ABSENT (differs from a stored zero value).
    // O(1) expected; O(chain length) worst case.
    const V* find(const std::string& key) const
    {
        const Entry* entry = findEntry(key);
        return entry == nullptr ? nullptr : &entry->value;
    }

    bool contains(const std::string& key) const { return findEntry(key) != nullptr; }

    // Removes key if present; false when absent (no side effects). O(1) expected.
    bool remove(const std::string& key)
    {
        if (buckets_.size() == 0)
            return false;
        Bucket& bucket = buckets_[indexOf(key)];
        if (bucket.removeIf([&key](const Entry& entry) { return entry.key == key; }))
        {
            --count_;
            return true;
        }
        return false;
    }

    std::size_t size() const { return count_; }
    bool isEmpty() const { return count_ == 0; }

    // count / buckets. 0.0 on the moved-from state (no buckets yet).
    double loadFactor() const
    {
        return buckets_.size() == 0
                   ? 0.0
                   : static_cast<double>(count_) / static_cast<double>(buckets_.size());
    }

    std::size_t bucketCount() const { return buckets_.size(); }

    // Empties every chain; keeps buckets allocated for reuse. O(n + buckets).
    void clear()
    {
        for (std::size_t i = 0; i < buckets_.size(); ++i)
            buckets_[i].clear();
        count_ = 0;
    }

private:
    struct Entry
    {
        std::string key;
        V value;
    };

    using Bucket = LinkedList<Entry>;
    using BucketArray = DynArray<Bucket>;

    static constexpr std::size_t kInitialBuckets = 16;

    std::size_t indexOf(const std::string& key) const
    {
        assert(buckets_.size() != 0 && "hash index requested without buckets");
        return static_cast<std::size_t>(hashFor(key) % buckets_.size());
    }

    static bool bucketContains(const Bucket& bucket, const std::string& key)
    {
        for (const Entry& entry : bucket)
            if (entry.key == key)
                return true;
        return false;
    }

    const Entry* findEntry(const std::string& key) const
    {
        if (buckets_.size() == 0)
            return nullptr;
        const Bucket& bucket = buckets_[indexOf(key)];
        for (const Entry& entry : bucket)
            if (entry.key == key)
                return &entry;
        return nullptr;
    }

    // Rebuilds every chain under a (larger) bucket count. O(n). Preserves all
    // (key,value) pairs.
    void rehash(std::size_t newBucketCount)
    {
        assert(newBucketCount > buckets_.size());
        BucketArray fresh(newBucketCount);
        for (std::size_t i = 0; i < buckets_.size(); ++i)
        {
            Bucket& bucket = buckets_[i];
            for (const Entry& entry : bucket)
            {
                const std::size_t freshIndex =
                    static_cast<std::size_t>(hashFor(entry.key) % newBucketCount);
                fresh[freshIndex].append(entry);
            }
            bucket.clear();
        }
        buckets_ = std::move(fresh);
    }

    BucketArray buckets_;
    std::size_t count_ = 0;
};

} // namespace ds
