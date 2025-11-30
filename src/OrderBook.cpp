#include "OrderBook.h"
#include <iostream>
#include <algorithm> // For std::remove_if

namespace Mercury {

    bool OrderBook::addOrder(const Order& order) {
        // Check for duplicate order ID
        if (orderLookup.find(order.id) != orderLookup.end()) {
            return false;  // Duplicate ID
        }

        // Validate order has required fields
        if (order.id == 0 || order.quantity == 0) {
            return false;  // Invalid order
        }

        // Store location for O(1) lookup
        orderLookup[order.id] = { order.price, order.side };

        if (order.side == Side::Buy) {
            // Add to Bids map
            bids[order.price].push_back(order);
        } else {
            // Add to Asks map
            asks[order.price].push_back(order);
        }

        return true;
    }

    bool OrderBook::removeOrder(uint64_t orderId) {
        // Edge case: invalid ID
        if (orderId == 0) {
            return false;
        }

        // 1. Find the order location in O(1)
        auto it = orderLookup.find(orderId);
        if (it == orderLookup.end()) {
            return false; // Order not found
        }

        const auto& location = it->second;

        // 2. Get the correct map and vector
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
        return true;
    }

    std::optional<Order> OrderBook::getOrder(uint64_t orderId) const {
        // Edge case: invalid ID
        if (orderId == 0) {
            return std::nullopt;
        }

        auto it = orderLookup.find(orderId);
        if (it == orderLookup.end()) {
            return std::nullopt;
        }

        const auto& location = it->second;

        if (location.side == Side::Buy) {
            auto bidIt = bids.find(location.price);
            if (bidIt != bids.end()) {
                for (const auto& order : bidIt->second) {
                    if (order.id == orderId) {
                        return order;
                    }
                }
            }
        } else {
            auto askIt = asks.find(location.price);
            if (askIt != asks.end()) {
                for (const auto& order : askIt->second) {
                    if (order.id == orderId) {
                        return order;
                    }
                }
            }
        }

        return std::nullopt;
    }

    bool OrderBook::updateOrderQuantity(uint64_t orderId, uint64_t newQuantity) {
        // Edge case: invalid ID
        if (orderId == 0) {
            return false;
        }

        auto it = orderLookup.find(orderId);
        if (it == orderLookup.end()) {
            return false;
        }

        const auto& location = it->second;

        std::vector<Order>* orders = nullptr;
        if (location.side == Side::Buy) {
            auto bidIt = bids.find(location.price);
            if (bidIt != bids.end()) orders = &bidIt->second;
        } else {
            auto askIt = asks.find(location.price);
            if (askIt != asks.end()) orders = &askIt->second;
        }

        if (orders) {
            for (auto& order : *orders) {
                if (order.id == orderId) {
                    if (newQuantity == 0) {
                        // Remove the order if quantity is zero
                        removeOrder(orderId);
                    } else {
                        order.quantity = newQuantity;
                    }
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<Order> OrderBook::getOrdersAtPrice(int64_t price, Side side) const {
        if (side == Side::Buy) {
            auto it = bids.find(price);
            if (it != bids.end()) {
                return it->second;
            }
        } else {
            auto it = asks.find(price);
            if (it != asks.end()) {
                return it->second;
            }
        }
        return {};  // Return empty vector if no orders at this price
    }

    uint64_t OrderBook::getBestBidQuantity() const {
        if (bids.empty()) return 0;
        
        uint64_t total = 0;
        for (const auto& order : bids.begin()->second) {
            // Overflow protection
            if (total > std::numeric_limits<uint64_t>::max() - order.quantity) {
                return std::numeric_limits<uint64_t>::max();
            }
            total += order.quantity;
        }
        return total;
    }

    uint64_t OrderBook::getBestAskQuantity() const {
        if (asks.empty()) return 0;
        
        uint64_t total = 0;
        for (const auto& order : asks.begin()->second) {
            // Overflow protection
            if (total > std::numeric_limits<uint64_t>::max() - order.quantity) {
                return std::numeric_limits<uint64_t>::max();
            }
            total += order.quantity;
        }
        return total;
    }

    uint64_t OrderBook::getQuantityAtPrice(int64_t price, Side side) const {
        const std::vector<Order>* orders = nullptr;
        
        if (side == Side::Buy) {
            auto it = bids.find(price);
            if (it != bids.end()) orders = &it->second;
        } else {
            auto it = asks.find(price);
            if (it != asks.end()) orders = &it->second;
        }

        if (!orders) return 0;

        uint64_t total = 0;
        for (const auto& order : *orders) {
            // Overflow protection
            if (total > std::numeric_limits<uint64_t>::max() - order.quantity) {
                return std::numeric_limits<uint64_t>::max();
            }
            total += order.quantity;
        }
        return total;
    }

    void OrderBook::printBook() const {
        std::cout << "--- ASK SIDE (Sellers) ---\n";
        if (asks.empty()) {
            std::cout << "(empty)\n";
        } else {
            // Iterate in reverse to show highest asks at top
            for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
                uint64_t totalQty = 0;
                for (const auto& o : it->second) totalQty += o.quantity;
                std::cout << "Price: " << it->first << " | Total Qty: " << totalQty 
                          << " | Orders: " << it->second.size() << "\n";
            }
        }

        std::cout << "--------------------------\n";
        
        if (hasBids() && hasAsks()) {
            std::cout << "Spread: " << getSpread() << " | Mid: " << getMidPrice() << "\n";
            std::cout << "--------------------------\n";
        }

        std::cout << "--- BID SIDE (Buyers) ---\n";
        if (bids.empty()) {
            std::cout << "(empty)\n";
        } else {
            for (const auto& [price, orderList] : bids) {
                uint64_t totalQty = 0;
                for (const auto& o : orderList) totalQty += o.quantity;
                std::cout << "Price: " << price << " | Total Qty: " << totalQty 
                          << " | Orders: " << orderList.size() << "\n";
            }
        }
        std::cout << "\n";
    }
}