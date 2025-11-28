#pragma once

#include "Order.h"
#include <map>
#include <vector>
#include <unordered_map>
#include <iostream>

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

            // Read-only views for testing/visualisation
            void printBook() const;

            // Getters for the matching engine (later phases)
            bool hasBids() const { return !bids.empty(); }
            bool hasAsks() const { return !asks.empty(); }
            int64_t getBestBid() const { return bids.begin()->first; }
            int64_t getBestAsk() const { return asks.begin()->first; }
    };
}