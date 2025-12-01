#pragma once

#include "Order.h"
#include "MatchingEngine.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <optional>
#include <chrono>

namespace Mercury {

    /**
     * Market Data Tick - represents a single market update
     */
    struct MarketTick {
        uint64_t timestamp = 0;
        int64_t bidPrice = 0;         // Best bid price
        int64_t askPrice = 0;         // Best ask price
        uint64_t bidQuantity = 0;     // Quantity at best bid
        uint64_t askQuantity = 0;     // Quantity at best ask
        int64_t lastTradePrice = 0;   // Last traded price
        uint64_t lastTradeQuantity = 0;
        uint64_t totalVolume = 0;     // Total traded volume
        
        // Calculated fields
        int64_t midPrice() const {
            return (bidPrice > 0 && askPrice > 0) ? (bidPrice + askPrice) / 2 : 0;
        }
        
        int64_t spread() const {
            return (bidPrice > 0 && askPrice > 0) ? askPrice - bidPrice : 0;
        }
        
        bool isValid() const {
            return bidPrice > 0 || askPrice > 0;
        }
    };

    /**
     * Strategy Signal - indicates desired trading action
     */
    enum class SignalType {
        None,           // No action
        Buy,            // Open/increase long position
        Sell,           // Open/increase short position
        CloseLong,      // Close long position
        CloseShort,     // Close short position
        CancelBids,     // Cancel all bid orders
        CancelAsks,     // Cancel all ask orders
        CancelAll       // Cancel all orders
    };

    /**
     * Strategy Signal with parameters
     */
    struct StrategySignal {
        SignalType type = SignalType::None;
        int64_t price = 0;            // Target price (0 for market order)
        uint64_t quantity = 0;        // Desired quantity
        double confidence = 0.0;      // Signal confidence (0.0 - 1.0)
        std::string reason;           // Human-readable reason
        
        bool hasSignal() const { return type != SignalType::None && quantity > 0; }
    };

    /**
     * Strategy State - internal state tracking
     */
    struct StrategyState {
        // Position tracking
        int64_t netPosition = 0;
        int64_t longPosition = 0;
        int64_t shortPosition = 0;
        
        // P&L tracking
        int64_t realizedPnL = 0;
        int64_t unrealizedPnL = 0;
        int64_t totalPnL = 0;
        
        // Order tracking
        uint64_t activeOrderCount = 0;
        std::vector<uint64_t> activeBidOrderIds;
        std::vector<uint64_t> activeAskOrderIds;
        
        // Trade statistics
        uint64_t totalTrades = 0;
        uint64_t totalVolume = 0;
        
        // Update P&L from market price
        void updateUnrealizedPnL(int64_t currentPrice, int64_t avgEntryPrice) {
            if (netPosition != 0 && avgEntryPrice > 0) {
                unrealizedPnL = (currentPrice - avgEntryPrice) * netPosition;
            } else {
                unrealizedPnL = 0;
            }
            totalPnL = realizedPnL + unrealizedPnL;
        }
    };

    /**
     * Strategy Configuration - base configuration
     */
    struct StrategyConfig {
        std::string name = "BaseStrategy";
        uint64_t clientId = 0;             // Client ID for orders
        bool enabled = true;               // Whether strategy is active
        
        // Risk limits
        int64_t maxPosition = 1000;        // Max net position
        int64_t maxOrderValue = 100000;    // Max single order value
        uint64_t maxOrderQuantity = 100;   // Max single order quantity
        int64_t maxLoss = -10000;          // Max loss before stopping
        
        // Timing
        uint64_t minOrderInterval = 100;   // Min ms between orders
    };

    /**
     * Strategy - Abstract base class for trading strategies
     * 
     * Strategies generate trading signals based on market data.
     * The StrategyManager executes these signals through the MatchingEngine.
     */
    class Strategy {
    public:
        using OrderCallback = std::function<ExecutionResult(Order)>;
        using CancelCallback = std::function<ExecutionResult(uint64_t)>;

        Strategy() = default;
        explicit Strategy(const StrategyConfig& config) : config_(config) {}
        virtual ~Strategy() = default;

        // Core interface - must implement
        
        /**
         * Process a market tick and generate signals
         * @param tick Current market data
         * @return Vector of signals to execute
         */
        virtual std::vector<StrategySignal> onMarketTick(const MarketTick& tick) = 0;

        /**
         * Handle trade execution notification
         * @param trade The executed trade
         * @param wasOurOrder True if this trade was from our order
         */
        virtual void onTradeExecuted(const Trade& trade, bool wasOurOrder) = 0;

        /**
         * Handle order fill notification
         * @param result The execution result
         */
        virtual void onOrderFilled(const ExecutionResult& result) = 0;

        /**
         * Get the strategy name
         */
        virtual std::string getName() const { return config_.name; }

        /**
         * Reset strategy state
         */
        virtual void reset() {
            state_ = StrategyState{};
        }

        // Configuration
        const StrategyConfig& getConfig() const { return config_; }
        void setConfig(const StrategyConfig& config) { config_ = config; }
        void setEnabled(bool enabled) { config_.enabled = enabled; }
        bool isEnabled() const { return config_.enabled; }

        // State access
        const StrategyState& getState() const { return state_; }
        StrategyState& getState() { return state_; }

        // Order ID management for strategy
        void setNextOrderId(uint64_t id) { nextOrderId_ = id; }
        uint64_t getNextOrderId() { return nextOrderId_++; }

    protected:
        StrategyConfig config_;
        StrategyState state_;
        uint64_t nextOrderId_ = 1;
        MarketTick lastTick_;
        
        // Helper to create a limit order
        Order createLimitOrder(Side side, int64_t price, uint64_t quantity) {
            Order order;
            order.id = getNextOrderId();
            order.orderType = OrderType::Limit;
            order.side = side;
            order.price = price;
            order.quantity = quantity;
            order.clientId = config_.clientId;
            order.tif = TimeInForce::GTC;
            return order;
        }

        // Helper to create a market order
        Order createMarketOrder(Side side, uint64_t quantity) {
            Order order;
            order.id = getNextOrderId();
            order.orderType = OrderType::Market;
            order.side = side;
            order.quantity = quantity;
            order.clientId = config_.clientId;
            order.tif = TimeInForce::IOC;
            return order;
        }

        // Check if order would exceed risk limits
        bool checkRiskLimits(Side side, int64_t price, uint64_t quantity) const {
            // Check max order quantity
            if (quantity > config_.maxOrderQuantity) {
                return false;
            }

            // Check max order value
            if (price > 0 && static_cast<int64_t>(quantity) * price > config_.maxOrderValue) {
                return false;
            }

            // Check position limits
            int64_t newPosition = state_.netPosition;
            if (side == Side::Buy) {
                newPosition += static_cast<int64_t>(quantity);
            } else {
                newPosition -= static_cast<int64_t>(quantity);
            }

            if (std::abs(newPosition) > config_.maxPosition) {
                return false;
            }

            // Check max loss
            if (state_.totalPnL < config_.maxLoss) {
                return false;
            }

            return true;
        }
    };

    /**
     * Strategy Factory Function Type
     */
    using StrategyFactory = std::function<std::unique_ptr<Strategy>(const StrategyConfig&)>;

}
