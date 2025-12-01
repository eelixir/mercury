/**
 * @file OrderBook.h
 * @brief High-performance order book using custom data structures
 * 
 * Optimizations implemented:
 * 1. Custom HashMap for O(1) order ID lookups (Robin Hood hashing)
 * 2. IntrusiveList for O(1) order insertion/removal at price levels
 * 3. ObjectPool for allocation-free order management
 * 4. std::map for sorted price levels (future: replace with skip list)
 * 
 * Complexity:
 * - addOrder: O(log P) where P = number of price levels
 * - removeOrder: O(1) average (HashMap lookup + IntrusiveList remove)
 * - getOrder: O(1) average (HashMap lookup)
 * - Best bid/ask: O(1) (cached or map begin)
 */

#pragma once

#include "Order.h"
#include "OrderNode.h"
#include "PriceLevel.h"
#include "HashMap.h"
#include "ObjectPool.h"
#include <map>
#include <iostream>
#include <optional>
#include <limits>
#include <memory>

namespace Mercury {

    class OrderBook {
    public:
        // Configuration constants
        static constexpr size_t DEFAULT_ORDER_POOL_SIZE = 10000;

        /**
         * Construct order book with optional initial pool size
         */
        explicit OrderBook(size_t initialPoolSize = DEFAULT_ORDER_POOL_SIZE)
            : orderPool_(initialPoolSize, true)  // Allow growth
            , orderLookup_(initialPoolSize) {}

        // Disable copy (pool ownership)
        OrderBook(const OrderBook&) = delete;
        OrderBook& operator=(const OrderBook&) = delete;

        // Enable move
        OrderBook(OrderBook&&) = default;
        OrderBook& operator=(OrderBook&&) = default;

        /**
         * Add an order to the book
         * @return true if added, false if duplicate ID or invalid
         */
        bool addOrder(const Order& order) {
            // Check for duplicate order ID
            if (orderLookup_.contains(order.id)) {
                return false;
            }

            // Validate order
            if (order.id == 0 || order.quantity == 0) {
                return false;
            }

            // Acquire node from pool
            OrderNode* node = orderPool_.acquire();
            if (!node) {
                return false;  // Pool exhausted (shouldn't happen with growth)
            }

            // Initialize node from order
            node->fromOrder(order);

            // Get or create price level
            PriceLevel* level = nullptr;
            if (order.side == Side::Buy) {
                auto& levelRef = bids_[order.price];
                levelRef.price = order.price;
                level = &levelRef;
            } else {
                auto& levelRef = asks_[order.price];
                levelRef.price = order.price;
                level = &levelRef;
            }

            // Add to price level
            level->addOrder(node);

            // Add to lookup map
            orderLookup_.insert(order.id, OrderLocation{node, order.price, order.side});

            return true;
        }

        /**
         * Remove an order from the book
         * @return true if removed, false if not found
         */
        bool removeOrder(uint64_t orderId) {
            if (orderId == 0) return false;

            // Find in lookup map
            OrderLocation* loc = orderLookup_.find(orderId);
            if (!loc) {
                return false;
            }

            OrderNode* node = loc->node;
            int64_t price = loc->price;
            Side side = loc->side;

            // Remove from price level
            if (side == Side::Buy) {
                auto it = bids_.find(price);
                if (it != bids_.end()) {
                    it->second.removeOrder(node);
                    // Remove empty price level
                    if (it->second.empty()) {
                        bids_.erase(it);
                    }
                }
            } else {
                auto it = asks_.find(price);
                if (it != asks_.end()) {
                    it->second.removeOrder(node);
                    if (it->second.empty()) {
                        asks_.erase(it);
                    }
                }
            }

            // Remove from lookup
            orderLookup_.erase(orderId);

            // Release node back to pool
            node->reset();
            orderPool_.release(node);

            return true;
        }

        /**
         * Get an order by ID
         * @return Order if found, nullopt otherwise
         */
        std::optional<Order> getOrder(uint64_t orderId) const {
            if (orderId == 0) return std::nullopt;

            const OrderLocation* loc = orderLookup_.find(orderId);
            if (!loc) {
                return std::nullopt;
            }

            return loc->node->toOrder();
        }

        /**
         * Get pointer to OrderNode (for internal use by MatchingEngine)
         */
        OrderNode* getOrderNode(uint64_t orderId) {
            OrderLocation* loc = orderLookup_.find(orderId);
            return loc ? loc->node : nullptr;
        }

        /**
         * Update order quantity
         * @return true if updated, false if not found or invalid
         */
        bool updateOrderQuantity(uint64_t orderId, uint64_t newQuantity) {
            if (orderId == 0) return false;

            OrderLocation* loc = orderLookup_.find(orderId);
            if (!loc) return false;

            if (newQuantity == 0) {
                return removeOrder(orderId);
            }

            OrderNode* node = loc->node;
            int64_t price = loc->price;
            Side side = loc->side;

            // Update in price level
            if (side == Side::Buy) {
                auto it = bids_.find(price);
                if (it != bids_.end()) {
                    it->second.updateOrderQuantity(node, newQuantity);
                }
            } else {
                auto it = asks_.find(price);
                if (it != asks_.end()) {
                    it->second.updateOrderQuantity(node, newQuantity);
                }
            }

            return true;
        }

        /**
         * Get orders at a specific price level
         * Returns a vector copy for API compatibility
         * 
         * Note: This function copies orders for safety. For performance-critical
         * internal use, prefer getBidLevel()/getAskLevel() which return direct
         * pointers to iterate without copying.
         */
        std::vector<Order> getOrdersAtPrice(int64_t price, Side side) const {
            std::vector<Order> result;

            if (side == Side::Buy) {
                auto it = bids_.find(price);
                if (it != bids_.end()) {
                    for (const auto& node : it->second) {
                        result.push_back(node.toOrder());
                    }
                }
            } else {
                auto it = asks_.find(price);
                if (it != asks_.end()) {
                    for (const auto& node : it->second) {
                        result.push_back(node.toOrder());
                    }
                }
            }

            return result;
        }

        // =====================================================================
        // Price Level Access (for MatchingEngine iteration)
        // =====================================================================

        /**
         * Get price level at a specific price
         * @return Pointer to PriceLevel or nullptr
         */
        PriceLevel* getBidLevel(int64_t price) {
            auto it = bids_.find(price);
            return (it != bids_.end()) ? &it->second : nullptr;
        }

        PriceLevel* getAskLevel(int64_t price) {
            auto it = asks_.find(price);
            return (it != asks_.end()) ? &it->second : nullptr;
        }

        const PriceLevel* getBidLevel(int64_t price) const {
            auto it = bids_.find(price);
            return (it != bids_.end()) ? &it->second : nullptr;
        }

        const PriceLevel* getAskLevel(int64_t price) const {
            auto it = asks_.find(price);
            return (it != asks_.end()) ? &it->second : nullptr;
        }

        /**
         * Get the best bid price level
         */
        PriceLevel* getBestBidLevel() {
            return bids_.empty() ? nullptr : &bids_.begin()->second;
        }

        PriceLevel* getBestAskLevel() {
            return asks_.empty() ? nullptr : &asks_.begin()->second;
        }

        /**
         * Remove an empty price level
         */
        void removeBidLevel(int64_t price) {
            bids_.erase(price);
        }

        void removeAskLevel(int64_t price) {
            asks_.erase(price);
        }

        // =====================================================================
        // Legacy API (for compatibility with existing tests/MatchingEngine)
        // =====================================================================

        const std::map<int64_t, PriceLevel, std::greater<int64_t>>& getBidLevels() const { 
            return bids_; 
        }
        
        const std::map<int64_t, PriceLevel>& getAskLevels() const { 
            return asks_; 
        }

        // =====================================================================
        // Book State Queries
        // =====================================================================

        [[nodiscard]] bool hasBids() const noexcept { return !bids_.empty(); }
        [[nodiscard]] bool hasAsks() const noexcept { return !asks_.empty(); }

        [[nodiscard]] int64_t getBestBid() const noexcept {
            return bids_.empty() ? std::numeric_limits<int64_t>::min() : bids_.begin()->first;
        }

        [[nodiscard]] int64_t getBestAsk() const noexcept {
            return asks_.empty() ? std::numeric_limits<int64_t>::max() : asks_.begin()->first;
        }

        [[nodiscard]] std::optional<int64_t> tryGetBestBid() const {
            return bids_.empty() ? std::nullopt : std::optional<int64_t>(bids_.begin()->first);
        }

        [[nodiscard]] std::optional<int64_t> tryGetBestAsk() const {
            return asks_.empty() ? std::nullopt : std::optional<int64_t>(asks_.begin()->first);
        }

        [[nodiscard]] uint64_t getBestBidQuantity() const noexcept {
            return bids_.empty() ? 0 : bids_.begin()->second.quantity();
        }

        [[nodiscard]] uint64_t getBestAskQuantity() const noexcept {
            return asks_.empty() ? 0 : asks_.begin()->second.quantity();
        }

        [[nodiscard]] uint64_t getQuantityAtPrice(int64_t price, Side side) const {
            if (side == Side::Buy) {
                auto it = bids_.find(price);
                return (it != bids_.end()) ? it->second.quantity() : 0;
            } else {
                auto it = asks_.find(price);
                return (it != asks_.end()) ? it->second.quantity() : 0;
            }
        }

        [[nodiscard]] size_t getOrderCount() const noexcept { return orderLookup_.size(); }
        [[nodiscard]] size_t getBidLevelCount() const noexcept { return bids_.size(); }
        [[nodiscard]] size_t getAskLevelCount() const noexcept { return asks_.size(); }

        [[nodiscard]] bool hasOrder(uint64_t orderId) const {
            return orderLookup_.contains(orderId);
        }

        [[nodiscard]] int64_t getSpread() const noexcept {
            if (bids_.empty() || asks_.empty()) return 0;
            return asks_.begin()->first - bids_.begin()->first;
        }

        [[nodiscard]] int64_t getMidPrice() const noexcept {
            if (bids_.empty() || asks_.empty()) return 0;
            return (asks_.begin()->first + bids_.begin()->first) / 2;
        }

        void clear() {
            // Release all nodes back to pool
            for (auto& [price, level] : bids_) {
                while (!level.empty()) {
                    OrderNode* node = level.popFront();
                    if (node) {
                        node->reset();
                        orderPool_.release(node);
                    }
                }
            }
            for (auto& [price, level] : asks_) {
                while (!level.empty()) {
                    OrderNode* node = level.popFront();
                    if (node) {
                        node->reset();
                        orderPool_.release(node);
                    }
                }
            }

            bids_.clear();
            asks_.clear();
            orderLookup_.clear();
        }

        bool isEmpty() const { return orderLookup_.empty(); }

        // =====================================================================
        // Debugging / Visualization
        // =====================================================================

        void printBook() const {
            std::cout << "--- ASK SIDE (Sellers) ---\n";
            if (asks_.empty()) {
                std::cout << "(empty)\n";
            } else {
                for (auto it = asks_.rbegin(); it != asks_.rend(); ++it) {
                    std::cout << "Price: " << it->first 
                              << " | Total Qty: " << it->second.quantity()
                              << " | Orders: " << it->second.size() << "\n";
                }
            }

            std::cout << "--------------------------\n";

            if (hasBids() && hasAsks()) {
                std::cout << "Spread: " << getSpread() << " | Mid: " << getMidPrice() << "\n";
                std::cout << "--------------------------\n";
            }

            std::cout << "--- BID SIDE (Buyers) ---\n";
            if (bids_.empty()) {
                std::cout << "(empty)\n";
            } else {
                for (const auto& [price, level] : bids_) {
                    std::cout << "Price: " << price 
                              << " | Total Qty: " << level.quantity()
                              << " | Orders: " << level.size() << "\n";
                }
            }
            std::cout << "\n";
        }

        // Pool statistics
        size_t getPoolActiveCount() const { return orderPool_.activeCount(); }
        size_t getPoolAllocatedCount() const { return orderPool_.allocatedCount(); }

    private:
        /**
         * OrderLocation - Tracks where an order is in the book
         */
        struct OrderLocation {
            OrderNode* node;
            int64_t price;
            Side side;
        };

        // Bids: sorted by price descending (best bid = highest price first)
        std::map<int64_t, PriceLevel, std::greater<int64_t>> bids_;

        // Asks: sorted by price ascending (best ask = lowest price first)
        std::map<int64_t, PriceLevel> asks_;

        // O(1) order lookup by ID
        HashMap<uint64_t, OrderLocation, OrderIdHash> orderLookup_;

        // Object pool for OrderNodes
        mutable ObjectPool<OrderNode> orderPool_;
    };

}