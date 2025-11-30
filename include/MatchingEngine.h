#pragma once

#include "Order.h"
#include "OrderBook.h"
#include <functional>
#include <atomic>

namespace Mercury {

    /**
     * MatchingEngine - The core trading engine
     * 
     * Handles all order types:
     * - Limit Orders: Match against opposite side, rest in book
     * - Market Orders: Match immediately against best available prices
     * - Cancel Orders: Remove existing orders from the book
     * - Modify Orders: Change price/quantity of existing orders
     * 
     * Supports Time-in-Force:
     * - GTC (Good-til-canceled): Default, order rests in book
     * - IOC (Immediate-or-cancel): Fill what you can, cancel rest
     * - FOK (Fill-or-kill): Fill entirely or reject entirely
     */
    class MatchingEngine {
    public:
        // Callback types for trade notifications
        using TradeCallback = std::function<void(const Trade&)>;
        using ExecutionCallback = std::function<void(const ExecutionResult&)>;

        MatchingEngine();

        /**
         * Submit an order to the matching engine
         * This is the main entry point - handles all order types
         */
        ExecutionResult submitOrder(Order order);

        /**
         * Process a limit order
         * Matches against opposite side, rests remainder in book
         */
        ExecutionResult processLimitOrder(Order& order);

        /**
         * Process a market order
         * Matches immediately against best available prices
         */
        ExecutionResult processMarketOrder(Order& order);

        /**
         * Process a cancel order
         * Removes an existing order from the book
         */
        ExecutionResult processCancelOrder(const Order& order);

        /**
         * Process a modify order
         * Changes price/quantity of an existing order
         */
        ExecutionResult processModifyOrder(const Order& order);

        /**
         * Cancel an order by ID directly
         */
        ExecutionResult cancelOrder(uint64_t orderId);

        /**
         * Modify an existing order
         * @param orderId The order to modify
         * @param newPrice New price (0 = keep original)
         * @param newQuantity New quantity (0 = keep original)
         */
        ExecutionResult modifyOrder(uint64_t orderId, int64_t newPrice, uint64_t newQuantity);

        // Register callbacks for trade notifications
        void setTradeCallback(TradeCallback callback) { tradeCallback_ = std::move(callback); }
        void setExecutionCallback(ExecutionCallback callback) { executionCallback_ = std::move(callback); }

        // Accessors
        const OrderBook& getOrderBook() const { return orderBook_; }
        OrderBook& getOrderBook() { return orderBook_; }
        uint64_t getTradeCount() const { return tradeCount_; }
        uint64_t getTotalVolume() const { return totalVolume_; }

        // Get current timestamp (monotonically increasing)
        uint64_t getTimestamp() { return ++currentTimestamp_; }

    private:
        OrderBook orderBook_;
        
        // Counters for trade IDs and stats
        std::atomic<uint64_t> tradeIdCounter_{0};
        std::atomic<uint64_t> currentTimestamp_{0};
        uint64_t tradeCount_ = 0;
        uint64_t totalVolume_ = 0;

        // Callbacks
        TradeCallback tradeCallback_;
        ExecutionCallback executionCallback_;

        /**
         * Match an aggressive order against the book
         * @param order The incoming order (will be modified to reflect fills)
         * @param trades Output vector for generated trades
         * @return True if any fills occurred
         */
        bool matchOrder(Order& order, std::vector<Trade>& trades);

        /**
         * Try to match against a single price level
         * @return Quantity filled at this level
         */
        uint64_t matchAtPriceLevel(Order& order, int64_t priceLevel, 
                                   std::vector<Trade>& trades);

        /**
         * Check if a price is acceptable for matching
         * Buy orders: priceLevel <= order.price (or market order)
         * Sell orders: priceLevel >= order.price (or market order)
         */
        bool isPriceAcceptable(const Order& order, int64_t priceLevel) const;

        /**
         * Generate a unique trade ID
         */
        uint64_t generateTradeId() { return ++tradeIdCounter_; }

        /**
         * Notify callbacks about a trade
         */
        void notifyTrade(const Trade& trade);

        /**
         * Notify callbacks about an execution result
         */
        void notifyExecution(const ExecutionResult& result);

        /**
         * Check if an order can be completely filled (for FOK orders)
         */
        bool canFillCompletely(const Order& order) const;
    };

}
