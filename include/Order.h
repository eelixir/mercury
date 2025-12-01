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

    // Specific rejection reasons for better error handling
    enum class RejectReason {
        None,
        InvalidOrderId,         // Order ID is zero or invalid
        InvalidQuantity,        // Quantity is zero or negative
        InvalidPrice,           // Price is negative (for limit orders)
        InvalidSide,            // Side not specified
        InvalidOrderType,       // Unknown order type
        DuplicateOrderId,       // Order ID already exists in book
        OrderNotFound,          // Cancel/Modify target not found
        NoLiquidity,            // Market order with empty opposite side
        FOKCannotFill,          // Fill-or-kill cannot be completely filled
        SelfTradePrevention,    // Would trade against own order
        ModifyNoChanges,        // Modify with no actual changes
        PriceOutOfRange,        // Price exceeds reasonable bounds
        QuantityOverflow,       // Quantity would cause overflow
        BookEmpty,              // Attempted operation on empty book
        InternalError           // Unexpected internal error
    };

    // Convert RejectReason to string for logging
    inline const char* rejectReasonToString(RejectReason reason) {
        switch (reason) {
            case RejectReason::None: return "None";
            case RejectReason::InvalidOrderId: return "Invalid order ID";
            case RejectReason::InvalidQuantity: return "Invalid quantity";
            case RejectReason::InvalidPrice: return "Invalid price";
            case RejectReason::InvalidSide: return "Invalid side";
            case RejectReason::InvalidOrderType: return "Invalid order type";
            case RejectReason::DuplicateOrderId: return "Duplicate order ID";
            case RejectReason::OrderNotFound: return "Order not found";
            case RejectReason::NoLiquidity: return "No liquidity available";
            case RejectReason::FOKCannotFill: return "FOK order cannot be filled completely";
            case RejectReason::SelfTradePrevention: return "Self-trade prevention";
            case RejectReason::ModifyNoChanges: return "Modify has no changes";
            case RejectReason::PriceOutOfRange: return "Price out of acceptable range";
            case RejectReason::QuantityOverflow: return "Quantity overflow";
            case RejectReason::BookEmpty: return "Order book is empty";
            case RejectReason::InternalError: return "Internal error";
            default: return "Unknown rejection reason";
        }
    }

    struct Order {
        uint64_t id = 0;            // unique order id
        uint64_t timestamp = 0;     // for price-time priority
        OrderType orderType = OrderType::Limit;
        Side side = Side::Buy;
        int64_t price = 0;
        uint64_t quantity = 0;
        TimeInForce tif = TimeInForce::GTC;

        // For Modify orders: the ID of the order to modify
        uint64_t targetOrderId = 0;
        
        // For Modify orders: new price (0 means keep original)
        int64_t newPrice = 0;
        
        // For Modify orders: new quantity (0 means keep original)
        uint64_t newQuantity = 0;

        // Optional: client order ID for self-trade prevention
        uint64_t clientId = 0;

        // Validate order and return specific rejection reason
        RejectReason validate() const {
            // All orders need a valid ID
            if (id == 0) {
                return RejectReason::InvalidOrderId;
            }

            switch (orderType) {
                case OrderType::Cancel:
                    // Cancel just needs a valid ID (already checked)
                    return RejectReason::None;

                case OrderType::Modify:
                    if (targetOrderId == 0) {
                        return RejectReason::InvalidOrderId;
                    }
                    if (newPrice == 0 && newQuantity == 0) {
                        return RejectReason::ModifyNoChanges;
                    }
                    if (newPrice < 0) {
                        return RejectReason::InvalidPrice;
                    }
                    return RejectReason::None;

                case OrderType::Market:
                    if (quantity == 0) {
                        return RejectReason::InvalidQuantity;
                    }
                    return RejectReason::None;

                case OrderType::Limit:
                    if (quantity == 0) {
                        return RejectReason::InvalidQuantity;
                    }
                    if (price < 0) {
                        return RejectReason::InvalidPrice;
                    }
                    // Check for unreasonable prices (configurable limit)
                    {
                        constexpr int64_t MAX_PRICE = 1'000'000'000;  // 1 billion
                        if (price > MAX_PRICE) {
                            return RejectReason::PriceOutOfRange;
                        }
                    }
                    return RejectReason::None;

                default:
                    return RejectReason::InvalidOrderType;
            }
        }

        // Helper to quickly check if an order is valid
        [[nodiscard]] bool isValid() const noexcept {
            return validate() == RejectReason::None;
        }
    };

    // Represents a single fill/execution
    struct Trade {
        uint64_t tradeId = 0;
        uint64_t buyOrderId = 0;
        uint64_t sellOrderId = 0;
        int64_t price = 0;
        uint64_t quantity = 0;
        uint64_t timestamp = 0;

        [[nodiscard]] bool isValid() const noexcept {
            return tradeId > 0 && buyOrderId > 0 && sellOrderId > 0 && 
                   price > 0 && quantity > 0;
        }
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
        ExecutionStatus status = ExecutionStatus::Rejected;
        RejectReason rejectReason = RejectReason::None;
        uint64_t orderId = 0;
        uint64_t filledQuantity = 0;
        uint64_t remainingQuantity = 0;
        std::vector<Trade> trades;
        std::string message;

        // Helper to check if any fills occurred
        [[nodiscard]] bool hasFills() const noexcept { return !trades.empty(); }

        // Helper to check if rejected
        [[nodiscard]] bool isRejected() const noexcept { return status == ExecutionStatus::Rejected; }

        // Helper to create a rejection result
        static ExecutionResult makeRejection(uint64_t orderId, RejectReason reason) {
            ExecutionResult result;
            result.status = ExecutionStatus::Rejected;
            result.rejectReason = reason;
            result.orderId = orderId;
            result.message = rejectReasonToString(reason);
            return result;
        }
    };
}