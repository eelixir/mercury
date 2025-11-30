#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace Mercury {

    /**
     * IntrusiveListNode - Base class for objects that can be stored in an IntrusiveList
     * 
     * Intrusive lists are more cache-friendly than std::list because:
     * 1. No separate node allocation - the object IS the node
     * 2. Better memory locality when iterating
     * 3. No pointer chasing to get to data
     * 
     * Usage: Inherit from IntrusiveListNode<YourClass>
     */
    template<typename T>
    struct IntrusiveListNode {
        T* prev = nullptr;
        T* next = nullptr;

        // Check if this node is linked into a list
        bool isLinked() const { return prev != nullptr || next != nullptr; }
    };

    /**
     * IntrusiveList - A doubly-linked list where nodes are embedded in the data
     * 
     * Time Complexity:
     * - push_front/push_back: O(1)
     * - pop_front/pop_back: O(1)
     * - insert_after/insert_before: O(1)
     * - remove: O(1) - no search needed since node knows its position
     * - iteration: O(n)
     * 
     * This is ideal for order queues at each price level where:
     * - Orders are added to the back (time priority)
     * - Orders are removed from front (fills) or anywhere (cancels)
     * - We need fast iteration for matching
     */
    template<typename T>
    class IntrusiveList {
        static_assert(std::is_base_of<IntrusiveListNode<T>, T>::value,
            "T must inherit from IntrusiveListNode<T>");

    public:
        // Forward iterator
        class iterator {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = T;
            using difference_type = std::ptrdiff_t;
            using pointer = T*;
            using reference = T&;

            iterator() : node_(nullptr) {}
            explicit iterator(T* node) : node_(node) {}

            reference operator*() const { return *node_; }
            pointer operator->() const { return node_; }

            iterator& operator++() {
                node_ = node_->next;
                return *this;
            }

            iterator operator++(int) {
                iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            iterator& operator--() {
                node_ = node_->prev;
                return *this;
            }

            iterator operator--(int) {
                iterator tmp = *this;
                --(*this);
                return tmp;
            }

            bool operator==(const iterator& other) const { return node_ == other.node_; }
            bool operator!=(const iterator& other) const { return node_ != other.node_; }

            T* get() const { return node_; }

        private:
            T* node_;
        };

        // Const iterator
        class const_iterator {
        public:
            using iterator_category = std::bidirectional_iterator_tag;
            using value_type = const T;
            using difference_type = std::ptrdiff_t;
            using pointer = const T*;
            using reference = const T&;

            const_iterator() : node_(nullptr) {}
            explicit const_iterator(const T* node) : node_(node) {}
            const_iterator(const iterator& it) : node_(it.get()) {}

            reference operator*() const { return *node_; }
            pointer operator->() const { return node_; }

            const_iterator& operator++() {
                node_ = node_->next;
                return *this;
            }

            const_iterator operator++(int) {
                const_iterator tmp = *this;
                ++(*this);
                return tmp;
            }

            const_iterator& operator--() {
                node_ = node_->prev;
                return *this;
            }

            const_iterator operator--(int) {
                const_iterator tmp = *this;
                --(*this);
                return tmp;
            }

            bool operator==(const const_iterator& other) const { return node_ == other.node_; }
            bool operator!=(const const_iterator& other) const { return node_ != other.node_; }

            const T* get() const { return node_; }

        private:
            const T* node_;
        };

        IntrusiveList() : head_(nullptr), tail_(nullptr), size_(0) {}

        // Disable copy (nodes can only be in one list)
        IntrusiveList(const IntrusiveList&) = delete;
        IntrusiveList& operator=(const IntrusiveList&) = delete;

        // Enable move
        IntrusiveList(IntrusiveList&& other) noexcept
            : head_(other.head_), tail_(other.tail_), size_(other.size_) {
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }

        IntrusiveList& operator=(IntrusiveList&& other) noexcept {
            if (this != &other) {
                head_ = other.head_;
                tail_ = other.tail_;
                size_ = other.size_;
                other.head_ = nullptr;
                other.tail_ = nullptr;
                other.size_ = 0;
            }
            return *this;
        }

        // Iterators
        iterator begin() { return iterator(head_); }
        iterator end() { return iterator(nullptr); }
        const_iterator begin() const { return const_iterator(head_); }
        const_iterator end() const { return const_iterator(nullptr); }
        const_iterator cbegin() const { return const_iterator(head_); }
        const_iterator cend() const { return const_iterator(nullptr); }

        // Capacity
        bool empty() const { return size_ == 0; }
        size_t size() const { return size_; }

        // Element access
        T& front() { return *head_; }
        const T& front() const { return *head_; }
        T& back() { return *tail_; }
        const T& back() const { return *tail_; }

        // Modifiers
        void push_front(T* node) {
            node->prev = nullptr;
            node->next = head_;

            if (head_) {
                head_->prev = node;
            } else {
                tail_ = node;
            }
            head_ = node;
            ++size_;
        }

        void push_back(T* node) {
            node->prev = tail_;
            node->next = nullptr;

            if (tail_) {
                tail_->next = node;
            } else {
                head_ = node;
            }
            tail_ = node;
            ++size_;
        }

        void pop_front() {
            if (!head_) return;

            T* old_head = head_;
            head_ = head_->next;

            if (head_) {
                head_->prev = nullptr;
            } else {
                tail_ = nullptr;
            }

            old_head->prev = nullptr;
            old_head->next = nullptr;
            --size_;
        }

        void pop_back() {
            if (!tail_) return;

            T* old_tail = tail_;
            tail_ = tail_->prev;

            if (tail_) {
                tail_->next = nullptr;
            } else {
                head_ = nullptr;
            }

            old_tail->prev = nullptr;
            old_tail->next = nullptr;
            --size_;
        }

        // Remove a specific node - O(1) since node knows its neighbors
        void remove(T* node) {
            if (!node) return;

            if (node->prev) {
                node->prev->next = node->next;
            } else {
                head_ = node->next;
            }

            if (node->next) {
                node->next->prev = node->prev;
            } else {
                tail_ = node->prev;
            }

            node->prev = nullptr;
            node->next = nullptr;
            --size_;
        }

        // Insert after a given node
        void insert_after(T* pos, T* node) {
            if (!pos) {
                push_front(node);
                return;
            }

            node->prev = pos;
            node->next = pos->next;

            if (pos->next) {
                pos->next->prev = node;
            } else {
                tail_ = node;
            }
            pos->next = node;
            ++size_;
        }

        // Insert before a given node
        void insert_before(T* pos, T* node) {
            if (!pos) {
                push_back(node);
                return;
            }

            node->next = pos;
            node->prev = pos->prev;

            if (pos->prev) {
                pos->prev->next = node;
            } else {
                head_ = node;
            }
            pos->prev = node;
            ++size_;
        }

        // Clear the list (just resets pointers, doesn't delete nodes)
        void clear() {
            // Unlink all nodes
            T* current = head_;
            while (current) {
                T* next = current->next;
                current->prev = nullptr;
                current->next = nullptr;
                current = next;
            }
            head_ = nullptr;
            tail_ = nullptr;
            size_ = 0;
        }

    private:
        T* head_;
        T* tail_;
        size_t size_;
    };

}
