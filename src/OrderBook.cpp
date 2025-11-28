#include "OrderBook.h"
#include <iostream>
#include <algorithm> // For std::remove_if

namespace Mercury {

    void OrderBook::addOrder(const Order& order) {
        if (order.side == Side::Buy) {
            // Add to Bids map
            bids[order.price].push_back(order);
        } else {
            // Add to Asks map
            asks[order.price].push_back(order);
        }
    }

    void OrderBook::removeOrder(uint64_t orderId) {
        // Helper lambda to remove from a map using Erase-Remove idiom for vectors
        auto removeFromMap = [&](auto& bookMap) {
            for (auto it = bookMap.begin(); it != bookMap.end(); ) {
                auto& orders = it->second;
            
                // std::remove_if moves non-matching elements to the front
                // and returns an iterator to the "new end"
                auto newEnd = std::remove_if(orders.begin(), orders.end(), 
                    [orderId](const Order& o) {
                        return o.id == orderId;
                    });

                // If we found and "removed" elements, actually erase them from the vector
                if (newEnd != orders.end()) {
                    orders.erase(newEnd, orders.end());
                }

                // If price level is empty, remove the map entry to save memory
                if (orders.empty()) {
                    it = bookMap.erase(it);
                } else {
                    ++it;
                }
            }
        };

        removeFromMap(bids);
        removeFromMap(asks);
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