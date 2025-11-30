#pragma once

#include <cstdint>
#include <ctime>
#include <vector>
#include <string>

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

    // Time-in-force for orders
    enum class TimeInForce {
        GTC,    // Good-til-canceled (default)
        IOC,    // Immediate-or-cancel (fill what you can, cancel rest)
        FOK     // Fill-or-kill (fill entirely or cancel entirely)
    };

    struct Order {
        uint64_t id;            // unique order id
        uint64_t timestamp;     // for price-time priority
        OrderType orderType;
        Side side;
        int64_t price;
        uint64_t quantity;
        TimeInForce tif = TimeInForce::GTC;

        // For Modify orders: the ID of the order to modify
        uint64_t targetOrderId = 0;
        
        // For Modify orders: new price (0 means keep original)
        int64_t newPrice = 0;
        
        // For Modify orders: new quantity (0 means keep original)
        uint64_t newQuantity = 0;

        // Helper to quickly check if an order is valid
        bool isValid() const {
            if (orderType == OrderType::Cancel) {
                return id > 0;  // Cancel just needs a valid ID
            }
            if (orderType == OrderType::Modify) {
                return targetOrderId > 0 && (newPrice > 0 || newQuantity > 0);
            }
            if (orderType == OrderType::Market) {
                return quantity > 0;  // Market orders don't need a price
            }
            // Limit orders need valid price and quantity
            return quantity > 0 && price >= 0;
        }
    };

    // Represents a single fill/execution
    struct Trade {
        uint64_t tradeId;
        uint64_t buyOrderId;
        uint64_t sellOrderId;
        int64_t price;
        uint64_t quantity;
        uint64_t timestamp;
    };

    // Result of submitting an order
    enum class ExecutionStatus {
        Filled,           // Order fully filled
        PartialFill,      // Order partially filled
        Resting,          // Order added to book (no match or partial)
        Cancelled,        // Cancel order processed successfully
        Modified,         // Modify order processed successfully
        Rejected          // Order rejected (invalid, not found, etc.)
    };

    struct ExecutionResult {
        ExecutionStatus status;
        uint64_t orderId;
        uint64_t filledQuantity = 0;
        uint64_t remainingQuantity = 0;
        std::vector<Trade> trades;
        std::string message;

        // Helper to check if any fills occurred
        bool hasFills() const { return !trades.empty(); }
    };
}