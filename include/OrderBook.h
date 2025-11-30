#pragma once

#include "Order.h"
#include <map>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <optional>

namespace Mercury {

    class OrderBook {
        private:
            struct OrderLocation {
                int64_t price;
                Side side;
            };

            // Bids: High prices are better -> Sort Descending (std::greater)
            // We use a vector for contiguous memory (better cache locality)
            std::map<int64_t, std::vector<Order>, std::greater<int64_t>> bids;

            // Asks: Low prices are better -> Sort Ascending (std::less - default)
            std::map<int64_t, std::vector<Order>> asks;

            // O(1) Lookup map: OrderID -> Location
            std::unordered_map<uint64_t, OrderLocation> orderLookup;

        public:
            // Constructor
            OrderBook() = default;

            // Core modifiers 
            void addOrder(const Order& order);
            void removeOrder(uint64_t orderId);

            // Get an order by ID
            std::optional<Order> getOrder(uint64_t orderId) const;

            // Update order quantity (for partial fills)
            bool updateOrderQuantity(uint64_t orderId, uint64_t newQuantity);

            // Get orders at a specific price level
            std::vector<Order> getOrdersAtPrice(int64_t price, Side side) const;

            // Get all price levels (for FOK checking and iteration)
            const std::map<int64_t, std::vector<Order>, std::greater<int64_t>>& getBidLevels() const { return bids; }
            const std::map<int64_t, std::vector<Order>>& getAskLevels() const { return asks; }

            // Read-only views for testing/visualisation
            void printBook() const;

            // Getters for the matching engine
            bool hasBids() const { return !bids.empty(); }
            bool hasAsks() const { return !asks.empty(); }
            int64_t getBestBid() const { return bids.begin()->first; }
            int64_t getBestAsk() const { return asks.begin()->first; }

            // Get total quantity at best bid/ask
            uint64_t getBestBidQuantity() const;
            uint64_t getBestAskQuantity() const;

            // Get total number of orders
            size_t getOrderCount() const { return orderLookup.size(); }

            // Get number of price levels
            size_t getBidLevelCount() const { return bids.size(); }
            size_t getAskLevelCount() const { return asks.size(); }
    };
}