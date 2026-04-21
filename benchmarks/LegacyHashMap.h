#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace MercuryBenchmarks {

template<typename Key, typename Value, typename Hash = std::hash<Key>>
class LegacyHashMap {
public:
    struct Entry {
        Key key;
        Value value;
        bool occupied = false;
        uint8_t probeDistance = 0;

        Entry() = default;
        Entry(const Key& k, const Value& v, uint8_t dist = 0)
            : key(k), value(v), occupied(true), probeDistance(dist) {}
    };

    explicit LegacyHashMap(size_t initialCapacity = 16)
        : size_(0), capacity_(nextPowerOf2(initialCapacity)) {
        entries_ = new Entry[capacity_];
        mask_ = capacity_ - 1;
    }

    ~LegacyHashMap() {
        delete[] entries_;
    }

    LegacyHashMap(const LegacyHashMap&) = delete;
    LegacyHashMap& operator=(const LegacyHashMap&) = delete;

    LegacyHashMap(LegacyHashMap&& other) noexcept
        : entries_(other.entries_),
          size_(other.size_),
          capacity_(other.capacity_),
          mask_(other.mask_) {
        other.entries_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        other.mask_ = 0;
    }

    LegacyHashMap& operator=(LegacyHashMap&& other) noexcept {
        if (this != &other) {
            delete[] entries_;
            entries_ = other.entries_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            mask_ = other.mask_;
            other.entries_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            other.mask_ = 0;
        }
        return *this;
    }

    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_t size() const noexcept { return size_; }

    void insert(const Key& key, const Value& value) {
        if (shouldGrow()) {
            grow();
        }
        insertInternal(key, value);
    }

    void insert(const Key& key, Value&& value) {
        if (shouldGrow()) {
            grow();
        }
        insertInternal(key, std::move(value));
    }

    [[nodiscard]] Value* find(const Key& key) {
        size_t index = findIndex(key);
        if (index != capacity_) {
            return &entries_[index].value;
        }
        return nullptr;
    }

    [[nodiscard]] const Value* find(const Key& key) const {
        size_t index = findIndex(key);
        if (index != capacity_) {
            return &entries_[index].value;
        }
        return nullptr;
    }

    bool erase(const Key& key) {
        size_t index = findIndex(key);
        if (index == capacity_) {
            return false;
        }

        entries_[index].occupied = false;
        --size_;

        size_t current = index;
        size_t next = (current + 1) & mask_;

        while (entries_[next].occupied && entries_[next].probeDistance > 0) {
            entries_[current] = std::move(entries_[next]);
            entries_[current].probeDistance--;
            entries_[next].occupied = false;

            current = next;
            next = (current + 1) & mask_;
        }

        return true;
    }

    void clear() {
        for (size_t i = 0; i < capacity_; ++i) {
            entries_[i].occupied = false;
        }
        size_ = 0;
    }

    void reserve(size_t newCapacity) {
        if (newCapacity > capacity_) {
            rehash(nextPowerOf2(newCapacity));
        }
    }

private:
    Entry* entries_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
    size_t mask_ = 0;
    Hash hasher_;

    static constexpr float MAX_LOAD_FACTOR = 0.7f;

    static size_t nextPowerOf2(size_t n) {
        if (n == 0) {
            return 1;
        }
        --n;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        return n + 1;
    }

    bool shouldGrow() const {
        return static_cast<float>(size_ + 1) / static_cast<float>(capacity_) > MAX_LOAD_FACTOR;
    }

    void grow() {
        rehash(capacity_ * 2);
    }

    void rehash(size_t newCapacity) {
        Entry* oldEntries = entries_;
        size_t oldCapacity = capacity_;

        capacity_ = newCapacity;
        mask_ = capacity_ - 1;
        entries_ = new Entry[capacity_];
        size_ = 0;

        for (size_t i = 0; i < oldCapacity; ++i) {
            if (oldEntries[i].occupied) {
                insertInternal(oldEntries[i].key, std::move(oldEntries[i].value));
            }
        }

        delete[] oldEntries;
    }

    size_t findIndex(const Key& key) const {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        uint8_t distance = 0;

        while (entries_[index].occupied) {
            if (entries_[index].key == key) {
                return index;
            }
            if (entries_[index].probeDistance < distance) {
                return capacity_;
            }
            ++distance;
            index = (index + 1) & mask_;
        }

        return capacity_;
    }

    void insertInternal(const Key& key, const Value& value) {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        uint8_t distance = 0;

        Entry newEntry(key, value, distance);

        while (entries_[index].occupied) {
            if (entries_[index].key == key) {
                entries_[index].value = value;
                return;
            }

            if (entries_[index].probeDistance < distance) {
                std::swap(newEntry, entries_[index]);
                distance = newEntry.probeDistance;
            }

            ++distance;
            newEntry.probeDistance = distance;
            index = (index + 1) & mask_;
        }

        entries_[index] = std::move(newEntry);
        ++size_;
    }

    void insertInternal(const Key& key, Value&& value) {
        size_t hash = hasher_(key);
        size_t index = hash & mask_;
        uint8_t distance = 0;

        Entry newEntry(key, std::move(value), distance);

        while (entries_[index].occupied) {
            if (entries_[index].key == key) {
                entries_[index].value = std::move(newEntry.value);
                return;
            }

            if (entries_[index].probeDistance < distance) {
                std::swap(newEntry, entries_[index]);
                distance = newEntry.probeDistance;
            }

            ++distance;
            newEntry.probeDistance = distance;
            index = (index + 1) & mask_;
        }

        entries_[index] = std::move(newEntry);
        ++size_;
    }
};

struct LegacyOrderIdHash {
    size_t operator()(uint64_t key) const {
        key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
        key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
        return key ^ (key >> 31);
    }
};

}  // namespace MercuryBenchmarks
