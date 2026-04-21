#pragma once

#include <absl/container/flat_hash_map.h>

#include "BenchTiming.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

namespace Mercury {

    /**
     * HashMap - Compatibility wrapper around absl::flat_hash_map.
     *
     * The repository still uses the older HashMap API shape in a number of
     * places and tests, but the underlying implementation is now Abseil's
     * flat_hash_map for better locality and mature probe-table behavior.
     */
    template<typename Key, typename Value, typename Hash = std::hash<Key>>
    class HashMap {
    public:
        using Storage = absl::flat_hash_map<Key, Value, Hash>;

        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = std::pair<const Key&, Value&>;

            iterator() = default;
            explicit iterator(typename Storage::iterator it)
                : it_(it) {}

            std::pair<const Key&, Value&> operator*() const {
                return {it_->first, it_->second};
            }

            iterator& operator++() {
                ++it_;
                return *this;
            }

            iterator operator++(int) {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const iterator& other) const { return it_ == other.it_; }
            bool operator!=(const iterator& other) const { return it_ != other.it_; }

            const Key& key() const { return it_->first; }
            Value& value() const { return it_->second; }

        private:
            typename Storage::iterator it_{};
        };

        class const_iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = std::pair<const Key&, const Value&>;

            const_iterator() = default;
            explicit const_iterator(typename Storage::const_iterator it)
                : it_(it) {}

            std::pair<const Key&, const Value&> operator*() const {
                return {it_->first, it_->second};
            }

            const_iterator& operator++() {
                ++it_;
                return *this;
            }

            const_iterator operator++(int) {
                const_iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            bool operator==(const const_iterator& other) const { return it_ == other.it_; }
            bool operator!=(const const_iterator& other) const { return it_ != other.it_; }

            const Key& key() const { return it_->first; }
            const Value& value() const { return it_->second; }

        private:
            typename Storage::const_iterator it_{};
        };

        explicit HashMap(size_t initialCapacity = 16) {
            map_.reserve(initialCapacity);
        }

        ~HashMap() = default;

        HashMap(const HashMap&) = delete;
        HashMap& operator=(const HashMap&) = delete;

        HashMap(HashMap&& other) noexcept
            : map_(std::move(other.map_)) {
            other.map_.clear();
        }

        HashMap& operator=(HashMap&& other) noexcept {
            if (this != &other) {
                map_ = std::move(other.map_);
                other.map_.clear();
            }
            return *this;
        }

        iterator begin() { return iterator(map_.begin()); }
        iterator end() { return iterator(map_.end()); }
        const_iterator begin() const { return const_iterator(map_.begin()); }
        const_iterator end() const { return const_iterator(map_.end()); }
        const_iterator cbegin() const { return const_iterator(map_.cbegin()); }
        const_iterator cend() const { return const_iterator(map_.cend()); }

        [[nodiscard]] bool empty() const noexcept { return map_.empty(); }
        [[nodiscard]] size_t size() const noexcept { return map_.size(); }
        [[nodiscard]] size_t capacity() const noexcept { return map_.capacity(); }
        [[nodiscard]] float loadFactor() const noexcept {
            return map_.capacity() == 0
                ? 0.0f
                : static_cast<float>(map_.size()) / static_cast<float>(map_.capacity());
        }

        void insert(const Key& key, const Value& value) {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            map_.insert_or_assign(key, value);
        }

        void insert(const Key& key, Value&& value) {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            map_.insert_or_assign(key, std::move(value));
        }

        template<typename... Args>
        void emplace(const Key& key, Args&&... args) {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            map_.insert_or_assign(key, Value(std::forward<Args>(args)...));
        }

        [[nodiscard]] Value* find(const Key& key) {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            auto it = map_.find(key);
            return it == map_.end() ? nullptr : &it->second;
        }

        [[nodiscard]] const Value* find(const Key& key) const {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            auto it = map_.find(key);
            return it == map_.end() ? nullptr : &it->second;
        }

        [[nodiscard]] bool contains(const Key& key) const {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            return map_.contains(key);
        }

        Value& operator[](const Key& key) {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            return map_[key];
        }

        [[nodiscard]] std::optional<Value> get(const Key& key) const {
            auto it = map_.find(key);
            if (it == map_.end()) {
                return std::nullopt;
            }
            return it->second;
        }

        bool erase(const Key& key) {
            MERCURY_BENCH_SCOPE(Mercury::BenchTiming::Category::HashLookup);
            return map_.erase(key) > 0;
        }

        void clear() {
            map_.clear();
        }

        void reserve(size_t newCapacity) {
            map_.reserve(newCapacity);
        }

    private:
        Storage map_;
    };

    /**
     * Specialized hash for uint64_t order IDs.
     * Uses a fast, high-quality mixing function for sequential IDs.
     */
    struct OrderIdHash {
        size_t operator()(uint64_t key) const {
            key = (key ^ (key >> 30)) * 0xbf58476d1ce4e5b9ULL;
            key = (key ^ (key >> 27)) * 0x94d049bb133111ebULL;
            return key ^ (key >> 31);
        }
    };

}
