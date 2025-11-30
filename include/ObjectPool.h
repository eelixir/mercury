/**
 * @file ObjectPool.h
 * @brief Lock-free object pool for high-performance order allocation
 * 
 * Pre-allocates objects to avoid heap fragmentation and reduce allocation
 * latency in the critical path. Essential for trading systems where
 * allocation jitter can cause latency spikes.
 * 
 * Design:
 * - Fixed-size pool with optional growth
 * - Free list for O(1) allocation/deallocation
 * - Contiguous memory for cache-friendly iteration
 * - No locks (single-threaded version - lock-free version for MT later)
 * 
 * Usage:
 *   ObjectPool<OrderNode> pool(10000);  // Pre-allocate 10k orders
 *   OrderNode* node = pool.acquire();
 *   // use node...
 *   pool.release(node);
 */

#pragma once

#include <cstddef>
#include <vector>
#include <memory>
#include <cassert>

namespace Mercury {

    /**
     * ObjectPool - A simple object pool with free list
     * 
     * Template parameter T must be default-constructible.
     * Objects are stored contiguously in memory blocks for cache efficiency.
     */
    template<typename T>
    class ObjectPool {
    public:
        /**
         * Construct a pool with initial capacity
         * @param initialSize Number of objects to pre-allocate
         * @param allowGrowth If true, allocates more blocks when exhausted
         */
        explicit ObjectPool(size_t initialSize = 1024, bool allowGrowth = true)
            : allowGrowth_(allowGrowth)
            , freeList_(nullptr)
            , allocatedCount_(0)
            , activeCount_(0) {
            
            if (initialSize > 0) {
                allocateBlock(initialSize);
            }
        }

        ~ObjectPool() {
            // Blocks are managed by unique_ptr, automatic cleanup
        }

        // Disable copy
        ObjectPool(const ObjectPool&) = delete;
        ObjectPool& operator=(const ObjectPool&) = delete;

        // Enable move
        ObjectPool(ObjectPool&&) = default;
        ObjectPool& operator=(ObjectPool&&) = default;

        /**
         * Acquire an object from the pool
         * @return Pointer to an available object, or nullptr if pool exhausted
         */
        T* acquire() {
            // Try to get from free list
            if (freeList_) {
                FreeNode* node = freeList_;
                freeList_ = node->next;
                ++activeCount_;
                
                // Construct new object in place
                T* obj = reinterpret_cast<T*>(node);
                new (obj) T();
                return obj;
            }

            // Free list empty - try to grow
            if (allowGrowth_) {
                // Grow by doubling or 1024, whichever is larger
                size_t growSize = std::max(allocatedCount_, size_t(1024));
                allocateBlock(growSize);
                return acquire();  // Retry after growth
            }

            return nullptr;  // Pool exhausted
        }

        /**
         * Release an object back to the pool
         * @param obj Pointer to object to release
         */
        void release(T* obj) {
            if (!obj) return;

            // Call destructor
            obj->~T();

            // Add to free list
            FreeNode* node = reinterpret_cast<FreeNode*>(obj);
            node->next = freeList_;
            freeList_ = node;
            --activeCount_;
        }

        /**
         * Get the number of currently active (acquired) objects
         */
        size_t activeCount() const { return activeCount_; }

        /**
         * Get the total number of allocated objects (including free)
         */
        size_t allocatedCount() const { return allocatedCount_; }

        /**
         * Get the number of available objects in the free list
         */
        size_t freeCount() const { return allocatedCount_ - activeCount_; }

        /**
         * Check if the pool can grow automatically
         */
        bool canGrow() const { return allowGrowth_; }

        /**
         * Pre-allocate additional capacity
         * @param count Number of additional objects to allocate
         */
        void reserve(size_t count) {
            allocateBlock(count);
        }

        /**
         * Clear the pool - releases all objects
         * Note: Does NOT call destructors on active objects (caller's responsibility)
         */
        void clear() {
            // Reset free list by rebuilding from all blocks
            freeList_ = nullptr;
            
            for (auto& block : blocks_) {
                char* ptr = block.get();
                size_t blockSize = blockSizes_[&block - &blocks_[0]];
                
                for (size_t i = 0; i < blockSize; ++i) {
                    FreeNode* node = reinterpret_cast<FreeNode*>(ptr + i * sizeof(T));
                    node->next = freeList_;
                    freeList_ = node;
                }
            }
            
            activeCount_ = 0;
        }

    private:
        // Free list node - overlays object storage
        struct FreeNode {
            FreeNode* next;
        };

        static_assert(sizeof(T) >= sizeof(FreeNode), 
            "Object type must be at least as large as a pointer");

        std::vector<std::unique_ptr<char[]>> blocks_;  // Memory blocks
        std::vector<size_t> blockSizes_;               // Size of each block
        bool allowGrowth_;
        FreeNode* freeList_;
        size_t allocatedCount_;
        size_t activeCount_;

        void allocateBlock(size_t count) {
            // Allocate raw memory
            auto block = std::make_unique<char[]>(count * sizeof(T));
            char* ptr = block.get();

            // Build free list from this block (in reverse for locality)
            for (size_t i = count; i > 0; --i) {
                FreeNode* node = reinterpret_cast<FreeNode*>(ptr + (i - 1) * sizeof(T));
                node->next = freeList_;
                freeList_ = node;
            }

            blocks_.push_back(std::move(block));
            blockSizes_.push_back(count);
            allocatedCount_ += count;
        }
    };

}
