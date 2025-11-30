#pragma once

#include "Order.h"
#include <map>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <limits>

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
            // Returns true if order was added, false if duplicate ID
            bool addOrder(const Order& order);
            
            // Returns true if order was removed, false if not found
            bool removeOrder(uint64_t orderId);

            // Get an order by ID
            std::optional<Order> getOrder(uint64_t orderId) const;

            // Update order quantity (for partial fills)
            // Returns true if successful, false if order not found or invalid quantity
            bool updateOrderQuantity(uint64_t orderId, uint64_t newQuantity);

            // Get orders at a specific price level (returns empty vector if none)
            std::vector<Order> getOrdersAtPrice(int64_t price, Side side) const;

            // Get all price levels (for FOK checking and iteration)
            const std::map<int64_t, std::vector<Order>, std::greater<int64_t>>& getBidLevels() const { return bids; }
            const std::map<int64_t, std::vector<Order>>& getAskLevels() const { return asks; }

            // Read-only views for testing/visualisation
            void printBook() const;

            // Safe getters for the matching engine (with empty book handling)
            bool hasBids() const { return !bids.empty(); }
            bool hasAsks() const { return !asks.empty(); }
            
            // Returns best bid price, or lowest int64_t if no bids
            int64_t getBestBid() const { 
                return bids.empty() ? std::numeric_limits<int64_t>::min() : bids.begin()->first; 
            }
            
            // Returns best ask price, or highest int64_t if no asks
            int64_t getBestAsk() const { 
                return asks.empty() ? std::numeric_limits<int64_t>::max() : asks.begin()->first; 
            }

            // Safe optional versions that don't return sentinel values
            std::optional<int64_t> tryGetBestBid() const {
                return bids.empty() ? std::nullopt : std::optional<int64_t>(bids.begin()->first);
            }
            
            std::optional<int64_t> tryGetBestAsk() const {
                return asks.empty() ? std::nullopt : std::optional<int64_t>(asks.begin()->first);
            }

            // Get total quantity at best bid/ask (returns 0 if empty)
            uint64_t getBestBidQuantity() const;
            uint64_t getBestAskQuantity() const;

            // Get total quantity at a price level
            uint64_t getQuantityAtPrice(int64_t price, Side side) const;

            // Get total number of orders
            size_t getOrderCount() const { return orderLookup.size(); }

            // Get number of price levels
            size_t getBidLevelCount() const { return bids.size(); }
            size_t getAskLevelCount() const { return asks.size(); }

            // Check if an order exists
            bool hasOrder(uint64_t orderId) const { 
                return orderLookup.find(orderId) != orderLookup.end(); 
            }

            // Get spread (returns 0 if either side is empty)
            int64_t getSpread() const {
                if (bids.empty() || asks.empty()) return 0;
                return asks.begin()->first - bids.begin()->first;
            }

            // Get mid price (returns 0 if either side is empty)
            int64_t getMidPrice() const {
                if (bids.empty() || asks.empty()) return 0;
                return (asks.begin()->first + bids.begin()->first) / 2;
            }

            // Clear the entire book
            void clear() {
                bids.clear();
                asks.clear();
                orderLookup.clear();
            }

            // Check if book is empty
            bool isEmpty() const { return orderLookup.empty(); }
    };
}