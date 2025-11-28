#include "OrderBook.h"
#include <iostream>
#include <algorithm> // For std::remove_if

namespace Mercury {

    void OrderBook::addOrder(const Order& order) {
        // Store location for O(1) lookup
        orderLookup[order.id] = { order.price, order.side };

        if (order.side == Side::Buy) {
            // Add to Bids map
            bids[order.price].push_back(order);
        } else {
            // Add to Asks map
            asks[order.price].push_back(order);
        }
    }

    void OrderBook::removeOrder(uint64_t orderId) {
        // 1. Find the order location in O(1)
        auto it = orderLookup.find(orderId);
        if (it == orderLookup.end()) {
            return; // Order not found
        }

        const auto& location = it->second;

        // 2. Get the correct map and vector
        // We use a pointer to the vector to avoid copying
        std::vector<Order>* orders = nullptr;
        
        if (location.side == Side::Buy) {
            auto bidIt = bids.find(location.price);
            if (bidIt != bids.end()) orders = &bidIt->second;
        } else {
            auto askIt = asks.find(location.price);
            if (askIt != asks.end()) orders = &askIt->second;
        }

        // 3. Remove from the vector
        if (orders) {
            auto newEnd = std::remove_if(orders->begin(), orders->end(), 
                [orderId](const Order& o) {
                    return o.id == orderId;
                });
            
            if (newEnd != orders->end()) {
                orders->erase(newEnd, orders->end());
            }

            // Clean up empty price levels
            if (orders->empty()) {
                if (location.side == Side::Buy) bids.erase(location.price);
                else asks.erase(location.price);
            }
        }

        // 4. Remove from lookup map
        orderLookup.erase(it);
    }

    void OrderBook::printBook() const {
        std::cout << "--- ASK SIDE (Sellers) ---\n";
        // Iterate in reverse to show highest asks at top (or standard for low-to-high)
        for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
            std::cout << "Price: " << it->first << " | Qty: ";
            for (const auto& o : it->second) std::cout << o.quantity << " ";
            std::cout << "\n";
        }

        std::cout << "--------------------------\n";

        std::cout << "--- BID SIDE (Buyers) ---\n";
        for (const auto& [price, orderList] : bids) {
            std::cout << "Price: " << price << " | Qty: ";
            for (const auto& o : orderList) std::cout << o.quantity << " ";
            std::cout << "\n";
        }
        std::cout << "\n";
    }
}