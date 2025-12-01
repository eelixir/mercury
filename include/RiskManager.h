#pragma once

#include "Order.h"
#include "HashMap.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <functional>
#include <atomic>
#include <limits>

namespace Mercury {
    /**
     * Safely convert uint64_t quantity to int64_t with overflow protection
     * Returns INT64_MAX if value would overflow
     */
    inline int64_t safeQuantityToInt64(uint64_t qty) {
        constexpr uint64_t MAX_SAFE = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        return (qty > MAX_SAFE) ? std::numeric_limits<int64_t>::max() : static_cast<int64_t>(qty);
    }
}

namespace Mercury {

    /**
     * Risk Event Types
     */
    enum class RiskEventType {
        PositionLimitBreached,      // Order would exceed max position for client
        GrossExposureLimitBreached, // Order would exceed gross exposure limit
        NetExposureLimitBreached,   // Order would exceed net exposure limit
        OrderValueLimitBreached,    // Single order value exceeds limit
        OrderQuantityLimitBreached, // Single order quantity exceeds limit
        DailyLossLimitBreached,     // Client's daily loss limit exceeded
        OrderRateExceeded,          // Too many orders in time window
        MaxOpenOrdersExceeded,      // Too many open orders for client
        Approved                    // Order passed all risk checks
    };

    inline const char* riskEventTypeToString(RiskEventType type) {
        switch (type) {
            case RiskEventType::PositionLimitBreached: return "POSITION_LIMIT_BREACHED";
            case RiskEventType::GrossExposureLimitBreached: return "GROSS_EXPOSURE_LIMIT_BREACHED";
            case RiskEventType::NetExposureLimitBreached: return "NET_EXPOSURE_LIMIT_BREACHED";
            case RiskEventType::OrderValueLimitBreached: return "ORDER_VALUE_LIMIT_BREACHED";
            case RiskEventType::OrderQuantityLimitBreached: return "ORDER_QUANTITY_LIMIT_BREACHED";
            case RiskEventType::DailyLossLimitBreached: return "DAILY_LOSS_LIMIT_BREACHED";
            case RiskEventType::OrderRateExceeded: return "ORDER_RATE_EXCEEDED";
            case RiskEventType::MaxOpenOrdersExceeded: return "MAX_OPEN_ORDERS_EXCEEDED";
            case RiskEventType::Approved: return "APPROVED";
            default: return "UNKNOWN";
        }
    }

    /**
     * Risk Event - represents a risk check result
     */
    struct RiskEvent {
        uint64_t eventId = 0;
        uint64_t timestamp = 0;
        uint64_t orderId = 0;
        uint64_t clientId = 0;
        RiskEventType eventType = RiskEventType::Approved;
        int64_t currentValue = 0;      // Current exposure/position/value
        int64_t limitValue = 0;        // The limit that was checked
        int64_t requestedValue = 0;    // Value that order would add
        std::string details;

        bool isApproved() const { return eventType == RiskEventType::Approved; }
        bool isRejected() const { return eventType != RiskEventType::Approved; }
    };

    /**
     * Client Position Tracking
     */
    struct ClientPosition {
        int64_t longPosition = 0;      // Total long quantity
        int64_t shortPosition = 0;     // Total short quantity
        int64_t realizedPnL = 0;       // Realized P&L
        int64_t unrealizedPnL = 0;     // Unrealized P&L
        uint64_t openOrderCount = 0;   // Number of open orders
        uint64_t dailyOrderCount = 0;  // Orders submitted today
        int64_t avgBuyPrice = 0;       // Average buy price
        int64_t avgSellPrice = 0;      // Average sell price

        int64_t netPosition() const { return longPosition - shortPosition; }
        int64_t grossPosition() const { return longPosition + shortPosition; }
    };

    /**
     * Risk Limits Configuration
     */
    struct RiskLimits {
        // Per-client limits
        int64_t maxPositionQuantity = 100000;      // Max net position per client
        int64_t maxGrossExposure = 1000000000;     // Max gross exposure (qty * price)
        int64_t maxNetExposure = 500000000;        // Max net exposure
        int64_t maxDailyLoss = -100000000;         // Max daily loss (negative)
        
        // Per-order limits
        int64_t maxOrderValue = 10000000;          // Max single order value
        uint64_t maxOrderQuantity = 10000;         // Max single order quantity
        
        // Rate limits
        uint64_t maxOrdersPerSecond = 100;         // Max orders per second
        uint64_t maxOpenOrders = 1000;             // Max open orders per client
        
        // Global limits
        int64_t globalMaxExposure = 10000000000;   // Global max exposure
    };

    /**
     * RiskManager - Pre-trade risk checking layer
     * 
     * Checks orders against configurable limits before they reach
     * the matching engine. Tracks positions and exposures per client.
     * 
     * Risk checks performed:
     * - Position limits per client
     * - Gross and net exposure limits
     * - Single order value and quantity limits
     * - Order rate limits
     * - Open order count limits
     * - Daily P&L limits
     */
    class RiskManager {
    public:
        using RiskCallback = std::function<void(const RiskEvent&)>;

        RiskManager();
        explicit RiskManager(const RiskLimits& limits);
        ~RiskManager() = default;

        // Prevent copying
        RiskManager(const RiskManager&) = delete;
        RiskManager& operator=(const RiskManager&) = delete;

        /**
         * Check if an order passes all risk checks
         * @param order The order to validate
         * @return RiskEvent with result (Approved or rejection reason)
         */
        RiskEvent checkOrder(const Order& order);

        /**
         * Update positions after a trade is executed
         * @param trade The executed trade
         * @param buyClientId Client ID of the buyer
         * @param sellClientId Client ID of the seller
         */
        void onTradeExecuted(const Trade& trade, uint64_t buyClientId, uint64_t sellClientId);

        /**
         * Update tracking when an order is added to the book
         * @param order The order added to book
         */
        void onOrderAdded(const Order& order);

        /**
         * Update tracking when an order is removed from the book
         * @param order The order removed from book
         */
        void onOrderRemoved(const Order& order);

        /**
         * Update tracking when an order is filled/partially filled
         * @param order The order that was filled
         * @param filledQuantity The quantity that was filled
         */
        void onOrderFilled(const Order& order, uint64_t filledQuantity);

        /**
         * Get position for a client
         * @param clientId The client ID
         * @return Current position (or default if not found)
         */
        ClientPosition getClientPosition(uint64_t clientId) const;

        /**
         * Set custom limits for a specific client
         * @param clientId The client ID
         * @param limits The limits to apply
         */
        void setClientLimits(uint64_t clientId, const RiskLimits& limits);

        /**
         * Get current risk limits (default or client-specific)
         * @param clientId The client ID (0 for default)
         * @return The applicable limits
         */
        const RiskLimits& getLimits(uint64_t clientId = 0) const;

        /**
         * Set default risk limits
         * @param limits The new default limits
         */
        void setDefaultLimits(const RiskLimits& limits);

        /**
         * Register callback for risk events
         */
        void setRiskCallback(RiskCallback callback) { riskCallback_ = std::move(callback); }

        /**
         * Reset all positions (e.g., start of trading day)
         */
        void resetPositions();

        /**
         * Reset daily counters
         */
        void resetDailyCounters();

        /**
         * Set last known market price (used for market order exposure calculation)
         * @param price The current market price
         */
        void setLastMarketPrice(int64_t price) { lastMarketPrice_ = price; }

        /**
         * Get last known market price
         * @return The last set market price (or default if never set)
         */
        int64_t getLastMarketPrice() const { return lastMarketPrice_; }

        /**
         * Get current timestamp
         */
        uint64_t getTimestamp() { return ++currentTimestamp_; }

        // Statistics
        uint64_t getApprovedCount() const { return approvedCount_; }
        uint64_t getRejectedCount() const { return rejectedCount_; }
        uint64_t getTotalChecks() const { return approvedCount_ + rejectedCount_; }
        size_t getClientCount() const { return clientPositions_.size(); }

    private:
        RiskLimits defaultLimits_;
        HashMap<uint64_t, RiskLimits> clientLimits_;
        HashMap<uint64_t, ClientPosition> clientPositions_;

        std::atomic<uint64_t> eventIdCounter_{0};
        std::atomic<uint64_t> currentTimestamp_{0};
        int64_t lastMarketPrice_ = 10000;  // Default fallback price
        
        uint64_t approvedCount_ = 0;
        uint64_t rejectedCount_ = 0;

        RiskCallback riskCallback_;

        /**
         * Generate a unique event ID
         */
        uint64_t generateEventId() { return ++eventIdCounter_; }

        /**
         * Get or create client position
         */
        ClientPosition& getOrCreatePosition(uint64_t clientId);

        /**
         * Check position limits
         */
        RiskEvent checkPositionLimits(const Order& order, const ClientPosition& position, 
                                       const RiskLimits& limits);

        /**
         * Check exposure limits
         */
        RiskEvent checkExposureLimits(const Order& order, const ClientPosition& position,
                                       const RiskLimits& limits);

        /**
         * Check single order limits
         */
        RiskEvent checkOrderLimits(const Order& order, const RiskLimits& limits);

        /**
         * Check open order limits
         */
        RiskEvent checkOpenOrderLimits(const Order& order, const ClientPosition& position,
                                        const RiskLimits& limits);

        /**
         * Notify risk callback
         */
        void notifyRiskEvent(const RiskEvent& event);
    };

    /**
     * RiskEventWriter - Writes risk events to CSV
     * 
     * Output CSV format:
     * event_id,timestamp,order_id,client_id,event_type,current_value,limit_value,requested_value,details
     */
    class RiskEventWriter {
    public:
        explicit RiskEventWriter(const std::string& filepath);
        ~RiskEventWriter();

        // Prevent copying
        RiskEventWriter(const RiskEventWriter&) = delete;
        RiskEventWriter& operator=(const RiskEventWriter&) = delete;

        /**
         * Open the file for writing
         */
        bool open();

        /**
         * Close the file
         */
        void close();

        /**
         * Check if file is open
         */
        bool isOpen() const { return file_.is_open(); }

        /**
         * Write a risk event to the file
         * @param event The event to write
         * @return true if write was successful
         */
        bool writeEvent(const RiskEvent& event);

        /**
         * Flush buffered writes
         */
        void flush();

        /**
         * Get count of events written
         */
        size_t getEventCount() const { return eventsWritten_; }

        /**
         * Get the file path
         */
        const std::string& getFilePath() const { return filepath_; }

    private:
        std::string filepath_;
        std::ofstream file_;
        size_t eventsWritten_ = 0;

        void writeHeader();
    };

}
