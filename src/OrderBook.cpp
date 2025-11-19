#include "OrderBook.h"
#include <iostream>

namespace Mercury {

    void OrderBook::addOrder(const Order& order) {
        if (order.side == Side::Buy) {
            // Add to Bids map
            // [] operator creates the list if it doesn't exist, then we push back
            bids[order.price].push_back(order);
        } else {
            // Add to Asks map
            asks[order.price].push_back(order);
        }
    }

    void OrderBook::removeOrder(uint64_t orderId) {
        // In a real engine, you would use a hashmap (O(1)) to look up where the order is.
        // For current phase iterating is acceptable, but will implement hashmap in the future.

        // Helper lambda to remove from a map
        auto removeFromMap = [&](auto& bookMap) {
            for (auto it = bookMap.begin(); it != bookMap.end(); ) {
                auto& orders = it->second;
            
                // Remove order with matching ID from the list
                orders.remove_if([orderId](const Order& o) {
                    return o.id == orderId;
                });

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