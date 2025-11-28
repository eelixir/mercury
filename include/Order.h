#pragma once

#include <cstdint>
#include <ctime>

namespace Mercury {
    // Order types as required by the specs
    enum class OrderType {
        Market,
        Limit,
        Cancel,
        Modify
    };

    // Side of the book
    enum class Side {
        Buy,
        Sell
    };

    struct Order {
        uint64_t id;            // unique order id
        uint64_t timestamp;     // for price-time priority
        OrderType orderType;
        Side side;
        int64_t price;
        uint64_t quantity;

        // Helper to quickly check if an order is valid
        bool isValid() const {
            // Market orders might have price 0, but quantity must be > 0
            return quantity > 0 && price >= 0;
        }
    };
}