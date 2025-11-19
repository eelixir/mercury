#pragma once

#include "Order.h"
#include <map>
#include <list>
#include <iostream>

namespace Mercury {

    class OrderBook {
        private:
            // Bids: High prices are better -> Sort Descending (std::greater)
            // We use a list to store multiple orders at the same price (Time priority)
            std::map<double, std::list<Order>, std::greater<double>> bids;

            // Asks: Low prices are better -> Sort Ascending (std::less - default)
            std::map<double, std::list<Order>> asks;

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
            double getBestBid() const { return bids.begin()->first; }
            double getBestAsk() const { return asks.begin()->first; }
    };
}