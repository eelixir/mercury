#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include <optional>
#include <vector>
#include <functional>

namespace Mercury {

    /**
     * HashMap - A custom hash map using open addressing with linear probing
     * 
     * Design choices for trading systems:
     * 1. Open addressing (no pointer chasing, cache-friendly)
     * 2. Power-of-2 sizing (fast modulo via bitmask)
     * 3. Robin Hood hashing (reduces probe sequence variance)
     * 4. Tombstone-free deletion (backward shift)
     * 
     * Time Complexity (average case):
     * - insert: O(1)
     * - find: O(1)
     * - erase: O(1)
     * 
     * Thread Safety:
     * - This class is NOT thread-safe. External synchronization required.
     * 
     * Iterator Validity:
     * - Iterators are invalidated by insert(), erase(), clear(), and rehash().
     * - Do not modify the map while iterating.
     * 
     * This is optimized for order ID lookups which are critical path in trading.
     */
    template<typename Key, typename Value, typename Hash = std::hash<Key>>
    class HashMap {
    public:
        struct Entry {
            Key key;
            Value value;
            bool occupied = false;
            uint8_t probeDistance = 0;  // For Robin Hood hashing

            Entry() = default;
            Entry(const Key& k, const Value& v, uint8_t dist = 0)
                : key(k), value(v), occupied(true), probeDistance(dist) {}
        };

        // Iterator for the hash map
        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::pair<const Key&, Value&>;
            using difference_type = std::ptrdiff_t;

            iterator(Entry* entries, size_t capacity, size_t index)
                : entries_(entries), capacity_(capacity), index_(index) {
                advanceToOccupied();
            }

            std::pair<const Key&, Value&> operator*() const {
                return {entries_[index_].key, entries_[index_].value};
            }

            iterator& operator++() {
                ++index_;
                advanceToOccupied();
                return *this;
            }

            iterator operator++(int) {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const iterator& other) const { return index_ == other.index_; }
            bool operator!=(const iterator& other) const { return index_ != other.index_; }

            Key& key() const { return entries_[index_].key; }
            Value& value() const { return entries_[index_].value; }

        private:
            void advanceToOccupied() {
                while (index_ < capacity_ && !entries_[index_].occupied) {
                    ++index_;
                }
            }

            Entry* entries_;
            size_t capacity_;
            size_t index_;
        };

        // Const iterator
        class const_iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type = std::pair<const Key&, const Value&>;
            using difference_type = std::ptrdiff_t;

            const_iterator(const Entry* entries, size_t capacity, size_t index)
                : entries_(entries), capacity_(capacity), index_(index) {
                advanceToOccupied();
            }

            std::pair<const Key&, const Value&> operator*() const {
                return {entries_[index_].key, entries_[index_].value};
            }

            const_iterator& operator++() {
                ++index_;
                advanceToOccupied();
                return *this;
            }

            const_iterator operator++(int) {
                const_iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const const_iterator& other) const { return index_ == other.index_; }
            bool operator!=(const const_iterator& other) const { return index_ != other.index_; }

            const Key& key() const { return entries_[index_].key; }
            const Value& value() const { return entries_[index_].value; }

        private:
            void advanceToOccupied() {
                while (index_ < capacity_ && !entries_[index_].occupied) {
                    ++index_;
                }
            }

            const Entry* entries_;
            size_t capacity_;
            size_t index_;
        };

        // Constructor with initial capacity (will be rounded up to power of 2)
        explicit HashMap(size_t initialCapacity = 16)
            : size_(0), capacity_(nextPowerOf2(initialCapacity)) {
            entries_ = new Entry[capacity_];
            mask_ = capacity_ - 1;
        }

        ~HashMap() {
            delete[] entries_;
        }

        // Disable copy (can implement if needed)
        HashMap(const HashMap&) = delete;
        HashMap& operator=(const HashMap&) = delete;

        // Enable move
        HashMap(HashMap&& other) noexcept
            : entries_(other.entries_), size_(other.size_), 
              capacity_(other.capacity_), mask_(other.mask_) {
            other.entries_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            other.mask_ = 0;
        }

        HashMap& operator=(HashMap&& other) noexcept {
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

        // Iterators
        iterator begin() { return iterator(entries_, capacity_, 0); }
        iterator end() { return iterator(entries_, capacity_, capacity_); }
        const_iterator begin() const { return const_iterator(entries_, capacity_, 0); }
        const_iterator end() const { return const_iterator(entries_, capacity_, capacity_); }
        const_iterator cbegin() const { return const_iterator(entries_, capacity_, 0); }
        const_iterator cend() const { return const_iterator(entries_, capacity_, capacity_); }

        // Capacity
        [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
        [[nodiscard]] size_t size() const noexcept { return size_; }
        [[nodiscard]] size_t capacity() const noexcept { return capacity_; }
        [[nodiscard]] float loadFactor() const noexcept { return static_cast<float>(size_) / capacity_; }

        // Insert or update
        void insert(const Key& key, const Value& value) {
            if (shouldGrow()) {
                grow();
            }
            insertInternal(key, value);
        }

        // Insert with move semantics
        void insert(const Key& key, Value&& value) {
            if (shouldGrow()) {
                grow();
            }
            insertInternal(key, std::move(value));
        }

        // Emplace (construct in place)
        template<typename... Args>
        void emplace(const Key& key, Args&&... args) {
            if (shouldGrow()) {
                grow();
            }
            emplaceInternal(key, std::forward<Args>(args)...);
        }

        // Find - returns pointer to value or nullptr
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

        // Contains check
        [[nodiscard]] bool contains(const Key& key) const {
            return findIndex(key) != capacity_;
        }

        // Operator[] - inserts default value if not exists
        Value& operator[](const Key& key) {
            size_t index = findIndex(key);
            if (index != capacity_) {
                return entries_[index].value;
            }
            // Insert default value
            if (shouldGrow()) {
                grow();
            }
            insertInternal(key, Value{});
            return *find(key);
        }

        // Get with optional
        [[nodiscard]] std::optional<Value> get(const Key& key) const {
            const Value* ptr = find(key);
            if (ptr) {
                return *ptr;
            }
            return std::nullopt;
        }

        // Erase - uses backward shift deletion (no tombstones)
        bool erase(const Key& key) {
            size_t index = findIndex(key);
            if (index == capacity_) {
                return false;  // Key not found
            }

            // Backward shift deletion
            entries_[index].occupied = false;
            --size_;

            // Shift subsequent entries back
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

        // Clear all entries
        void clear() {
            for (size_t i = 0; i < capacity_; ++i) {
                entries_[i].occupied = false;
            }
            size_ = 0;
        }

        // Reserve capacity
        void reserve(size_t newCapacity) {
            if (newCapacity > capacity_) {
                rehash(nextPowerOf2(newCapacity));
            }
        }

    private:
        Entry* entries_;
        size_t size_;
        size_t capacity_;
        size_t mask_;  // capacity_ - 1, used for fast modulo
        Hash hasher_;

        static constexpr float MAX_LOAD_FACTOR = 0.7f;

        // Round up to next power of 2
        static size_t nextPowerOf2(size_t n) {
            if (n == 0) return 1;
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
            return static_cast<float>(size_ + 1) / capacity_ > MAX_LOAD_FACTOR;
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

            // Re-insert all entries
            for (size_t i = 0; i < oldCapacity; ++i) {
                if (oldEntries[i].occupied) {
                    insertInternal(oldEntries[i].key, std::move(oldEntries[i].value));
                }
            }

            delete[] oldEntries;
        }

        // Find the index of a key, or capacity_ if not found
        size_t findIndex(const Key& key) const {
            size_t hash = hasher_(key);
            size_t index = hash & mask_;
            uint8_t distance = 0;

            while (entries_[index].occupied) {
                if (entries_[index].key == key) {
                    return index;
                }
                // Robin Hood: if current entry has shorter probe distance, key doesn't exist
                if (entries_[index].probeDistance < distance) {
                    return capacity_;  // Not found
                }
                ++distance;
                index = (index + 1) & mask_;
            }

            return capacity_;  // Not found
        }

        void insertInternal(const Key& key, const Value& value) {
            size_t hash = hasher_(key);
            size_t index = hash & mask_;
            uint8_t distance = 0;

            Entry newEntry(key, value, distance);

            while (entries_[index].occupied) {
                // Check for existing key (update)
                if (entries_[index].key == key) {
                    entries_[index].value = value;
                    return;
                }

                // Robin Hood: swap if current entry has shorter probe distance
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

        template<typename... Args>
        void emplaceInternal(const Key& key, Args&&... args) {
            size_t hash = hasher_(key);
            size_t index = hash & mask_;
            uint8_t distance = 0;

            // Check if key exists first
            size_t existingIndex = findIndex(key);
            if (existingIndex != capacity_) {
                entries_[existingIndex].value = Value(std::forward<Args>(args)...);
                return;
            }

            Entry newEntry;
            newEntry.key = key;
            newEntry.value = Value(std::forward<Args>(args)...);
            newEntry.occupied = true;
            newEntry.probeDistance = 0;

            while (entries_[index].occupied) {
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

    /**
     * Specialized hash for uint64_t order IDs
     * Uses a fast, high-quality mixing function
     */
    struct OrderIdHash {
        size_t operator()(uint64_t key) const {
            // Splitmix64 - excellent distribution for sequential IDs
            key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
            key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
            return key ^ (key >> 31);
        }
    };

}
