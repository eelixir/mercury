#pragma once

#include "Strategy.h"
#include "MarketMakingStrategy.h"
#include "MomentumStrategy.h"
#include "MatchingEngine.h"
#include "RiskManager.h"
#include "PnLTracker.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <functional>
#include <iostream>

namespace Mercury {

    /**
     * Strategy Manager Configuration
     */
    struct StrategyManagerConfig {
        bool enableRiskChecks = true;      // Run risk checks before submitting
        bool enablePnLTracking = true;     // Track P&L per strategy
        bool logSignals = false;           // Log generated signals
        bool logExecutions = true;         // Log order executions
        uint64_t baseOrderId = 1000000;    // Starting order ID for strategies
        uint64_t clientIdOffset = 100;     // Client ID offset per strategy
    };

    /**
     * Strategy Performance Metrics
     */
    struct StrategyMetrics {
        std::string strategyName;
        
        // Order statistics
        uint64_t ordersSubmitted = 0;
        uint64_t ordersFilled = 0;
        uint64_t ordersPartialFilled = 0;
        uint64_t ordersCancelled = 0;
        uint64_t ordersRejected = 0;
        
        // Trade statistics
        uint64_t totalTrades = 0;
        uint64_t totalVolume = 0;
        
        // P&L
        int64_t realizedPnL = 0;
        int64_t unrealizedPnL = 0;
        int64_t totalPnL = 0;
        
        // Position
        int64_t netPosition = 0;
        int64_t maxPosition = 0;
        
        // Timing
        uint64_t signalsGenerated = 0;
        uint64_t lastSignalTime = 0;
    };

    /**
     * StrategyManager - Orchestrates trading strategies
     * 
     * Responsibilities:
     * 1. Manage multiple strategies
     * 2. Feed market data to strategies
     * 3. Execute strategy signals through matching engine
     * 4. Track orders per strategy
     * 5. Handle fills and update strategy state
     * 6. Track performance metrics
     * 
     * Example:
     *   MatchingEngine engine;
     *   StrategyManager manager(engine);
     *   
     *   auto mm = std::make_unique<MarketMakingStrategy>(mmConfig);
     *   manager.addStrategy(std::move(mm));
     *   
     *   // Feed market data
     *   MarketTick tick = createTickFromOrderBook(engine.getOrderBook());
     *   manager.onMarketTick(tick);
     */
    class StrategyManager {
    public:
        using SignalCallback = std::function<void(const std::string&, const StrategySignal&)>;
        using ExecutionCallback = std::function<void(const std::string&, const ExecutionResult&)>;

        explicit StrategyManager(MatchingEngine& engine)
            : engine_(engine)
            , riskManager_(nullptr)
            , pnlTracker_(nullptr)
            , nextOrderId_(config_.baseOrderId) {
            setupCallbacks();
        }

        StrategyManager(MatchingEngine& engine, RiskManager& riskManager)
            : engine_(engine)
            , riskManager_(&riskManager)
            , pnlTracker_(nullptr)
            , nextOrderId_(config_.baseOrderId) {
            setupCallbacks();
        }

        StrategyManager(MatchingEngine& engine, RiskManager& riskManager, PnLTracker& pnlTracker)
            : engine_(engine)
            , riskManager_(&riskManager)
            , pnlTracker_(&pnlTracker)
            , nextOrderId_(config_.baseOrderId) {
            setupCallbacks();
        }

        ~StrategyManager() = default;

        // Prevent copying
        StrategyManager(const StrategyManager&) = delete;
        StrategyManager& operator=(const StrategyManager&) = delete;

        /**
         * Add a strategy to the manager
         * @param strategy Unique pointer to strategy
         * @return Strategy name
         */
        std::string addStrategy(std::unique_ptr<Strategy> strategy) {
            std::string name = strategy->getName();
            
            // Assign client ID based on strategy index
            uint64_t clientId = config_.clientIdOffset + strategies_.size();
            auto& config = strategy->getConfig();
            const_cast<StrategyConfig&>(config).clientId = clientId;
            
            // Assign order ID range
            strategy->setNextOrderId(nextOrderId_);
            nextOrderId_ += 1000000;  // Reserve 1M order IDs per strategy
            
            // Initialize metrics
            StrategyMetrics metrics;
            metrics.strategyName = name;
            strategyMetrics_[name] = metrics;
            
            strategies_[name] = std::move(strategy);
            return name;
        }

        /**
         * Remove a strategy
         */
        bool removeStrategy(const std::string& name) {
            auto it = strategies_.find(name);
            if (it == strategies_.end()) {
                return false;
            }
            
            // Cancel all open orders for this strategy
            cancelStrategyOrders(name);
            
            strategies_.erase(it);
            return true;
        }

        /**
         * Get a strategy by name
         */
        Strategy* getStrategy(const std::string& name) {
            auto it = strategies_.find(name);
            return (it != strategies_.end()) ? it->second.get() : nullptr;
        }

        /**
         * Enable/disable a strategy
         */
        void setStrategyEnabled(const std::string& name, bool enabled) {
            if (auto* strategy = getStrategy(name)) {
                strategy->setEnabled(enabled);
            }
        }

        /**
         * Process a market tick for all strategies
         * @param tick Current market data
         */
        void onMarketTick(const MarketTick& tick) {
            lastTick_ = tick;
            
            for (auto& [name, strategy] : strategies_) {
                if (!strategy->isEnabled()) continue;
                
                // Generate signals from strategy
                auto signals = strategy->onMarketTick(tick);
                
                // Process each signal
                for (const auto& signal : signals) {
                    if (config_.logSignals) {
                        logSignal(name, signal);
                    }
                    
                    if (signalCallback_) {
                        signalCallback_(name, signal);
                    }
                    
                    strategyMetrics_[name].signalsGenerated++;
                    strategyMetrics_[name].lastSignalTime = tick.timestamp;
                    
                    // Execute the signal
                    executeSignal(name, *strategy, signal, tick);
                }
            }
            
            tickCount_++;
        }

        /**
         * Create a MarketTick from current order book state
         */
        MarketTick createTickFromOrderBook() const {
            const auto& book = engine_.getOrderBook();
            
            MarketTick tick;
            tick.timestamp = tickCount_;
            
            if (book.hasBids()) {
                tick.bidPrice = book.getBestBid();
                tick.bidQuantity = book.getBestBidQuantity();
            }
            
            if (book.hasAsks()) {
                tick.askPrice = book.getBestAsk();
                tick.askQuantity = book.getBestAskQuantity();
            }
            
            tick.totalVolume = engine_.getTotalVolume();
            
            return tick;
        }

        /**
         * Create a MarketTick from a trade
         */
        MarketTick createTickFromTrade(const Trade& trade) const {
            MarketTick tick = createTickFromOrderBook();
            tick.lastTradePrice = trade.price;
            tick.lastTradeQuantity = trade.quantity;
            tick.timestamp = trade.timestamp;
            return tick;
        }

        /**
         * Cancel all orders for a strategy
         */
        void cancelStrategyOrders(const std::string& name) {
            auto it = strategyOrders_.find(name);
            if (it == strategyOrders_.end()) return;
            
            for (uint64_t orderId : it->second) {
                engine_.cancelOrder(orderId);
            }
            it->second.clear();
        }

        /**
         * Cancel all orders for all strategies
         */
        void cancelAllOrders() {
            for (auto& [name, _] : strategies_) {
                cancelStrategyOrders(name);
            }
        }

        /**
         * Reset all strategies
         */
        void reset() {
            cancelAllOrders();
            for (auto& [name, strategy] : strategies_) {
                strategy->reset();
            }
            for (auto& [name, metrics] : strategyMetrics_) {
                metrics = StrategyMetrics{};
                metrics.strategyName = name;
            }
            tickCount_ = 0;
        }

        // Configuration
        void setConfig(const StrategyManagerConfig& config) { config_ = config; }
        const StrategyManagerConfig& getConfig() const { return config_; }

        // Callbacks
        void setSignalCallback(SignalCallback callback) { signalCallback_ = std::move(callback); }
        void setExecutionCallback(ExecutionCallback callback) { executionCallback_ = std::move(callback); }

        // Metrics access
        const StrategyMetrics& getMetrics(const std::string& name) const {
            static StrategyMetrics empty;
            auto it = strategyMetrics_.find(name);
            return (it != strategyMetrics_.end()) ? it->second : empty;
        }

        std::vector<StrategyMetrics> getAllMetrics() const {
            std::vector<StrategyMetrics> result;
            for (const auto& [name, metrics] : strategyMetrics_) {
                result.push_back(metrics);
            }
            return result;
        }

        // Statistics
        size_t getStrategyCount() const { return strategies_.size(); }
        uint64_t getTickCount() const { return tickCount_; }
        uint64_t getTotalOrders() const {
            uint64_t total = 0;
            for (const auto& [name, metrics] : strategyMetrics_) {
                total += metrics.ordersSubmitted;
            }
            return total;
        }

        /**
         * Print summary of all strategies
         */
        void printSummary() const {
            std::cout << "\n=== Strategy Manager Summary ===\n";
            std::cout << "Strategies: " << strategies_.size() << "\n";
            std::cout << "Ticks processed: " << tickCount_ << "\n\n";
            
            for (const auto& [name, metrics] : strategyMetrics_) {
                std::cout << "--- " << name << " ---\n";
                std::cout << "  Orders: " << metrics.ordersSubmitted 
                          << " submitted, " << metrics.ordersFilled << " filled, "
                          << metrics.ordersRejected << " rejected\n";
                std::cout << "  Trades: " << metrics.totalTrades 
                          << ", Volume: " << metrics.totalVolume << "\n";
                std::cout << "  Position: " << metrics.netPosition 
                          << " (max: " << metrics.maxPosition << ")\n";
                std::cout << "  P&L: Realized=" << metrics.realizedPnL 
                          << ", Unrealized=" << metrics.unrealizedPnL
                          << ", Total=" << metrics.totalPnL << "\n";
                std::cout << "  Signals: " << metrics.signalsGenerated << "\n";
            }
            std::cout << "================================\n";
        }

    private:
        MatchingEngine& engine_;
        RiskManager* riskManager_;
        PnLTracker* pnlTracker_;
        
        StrategyManagerConfig config_;
        
        std::unordered_map<std::string, std::unique_ptr<Strategy>> strategies_;
        std::unordered_map<std::string, StrategyMetrics> strategyMetrics_;
        std::unordered_map<std::string, std::vector<uint64_t>> strategyOrders_;
        std::unordered_map<uint64_t, std::string> orderToStrategy_;
        
        uint64_t nextOrderId_;
        uint64_t tickCount_ = 0;
        MarketTick lastTick_;
        
        SignalCallback signalCallback_;
        ExecutionCallback executionCallback_;

        /**
         * Setup callbacks from matching engine
         */
        void setupCallbacks() {
            // Trade callback
            engine_.setTradeCallback([this](const Trade& trade) {
                onTradeExecuted(trade);
            });
            
            // Execution callback
            engine_.setExecutionCallback([this](const ExecutionResult& result) {
                onOrderExecuted(result);
            });
        }

        /**
         * Execute a strategy signal
         */
        void executeSignal(const std::string& strategyName, Strategy& strategy,
                          const StrategySignal& signal, const MarketTick& tick) {
            
            Order order;
            bool isClosingOrder = false;
            
            switch (signal.type) {
                case SignalType::Buy:
                    order = createOrderFromSignal(strategy, Side::Buy, signal);
                    break;
                    
                case SignalType::Sell:
                    order = createOrderFromSignal(strategy, Side::Sell, signal);
                    break;
                    
                case SignalType::CloseLong:
                    order = createOrderFromSignal(strategy, Side::Sell, signal);
                    isClosingOrder = true;
                    break;
                    
                case SignalType::CloseShort:
                    order = createOrderFromSignal(strategy, Side::Buy, signal);
                    isClosingOrder = true;
                    break;
                    
                case SignalType::CancelBids:
                    cancelSideOrders(strategyName, Side::Buy);
                    return;
                    
                case SignalType::CancelAsks:
                    cancelSideOrders(strategyName, Side::Sell);
                    return;
                    
                case SignalType::CancelAll:
                    cancelStrategyOrders(strategyName);
                    return;
                    
                case SignalType::None:
                default:
                    return;
            }

            // Risk check - bypass for closing orders (exits should always be allowed)
            if (config_.enableRiskChecks && riskManager_ && !isClosingOrder) {
                auto riskEvent = riskManager_->checkOrder(order);
                if (riskEvent.isRejected()) {
                    strategyMetrics_[strategyName].ordersRejected++;
                    if (config_.logExecutions) {
                        std::cout << "[" << strategyName << "] Order rejected: " 
                                  << riskEvent.details << "\n";
                    }
                    return;
                }
            }

            // Submit order
            orderToStrategy_[order.id] = strategyName;
            strategyOrders_[strategyName].push_back(order.id);
            strategyMetrics_[strategyName].ordersSubmitted++;
            
            auto result = engine_.submitOrder(order);
            
            // Process result
            processExecutionResult(strategyName, strategy, order, result, isClosingOrder);
        }

        /**
         * Create an order from a signal
         */
        Order createOrderFromSignal(Strategy& strategy, Side side, 
                                    const StrategySignal& signal) {
            Order order;
            order.id = strategy.getNextOrderId();
            order.side = side;
            order.quantity = signal.quantity;
            order.clientId = strategy.getConfig().clientId;
            
            if (signal.price > 0) {
                order.orderType = OrderType::Limit;
                order.price = signal.price;
                order.tif = TimeInForce::GTC;
            } else {
                order.orderType = OrderType::Market;
                order.tif = TimeInForce::IOC;
            }
            
            return order;
        }

        /**
         * Process execution result
         */
        void processExecutionResult(const std::string& strategyName, Strategy& strategy,
                                    const Order& order, const ExecutionResult& result,
                                    bool isClosingOrder) {
            auto& metrics = strategyMetrics_[strategyName];
            
            if (config_.logExecutions) {
                logExecution(strategyName, result);
            }
            
            if (executionCallback_) {
                executionCallback_(strategyName, result);
            }
            
            // Update metrics based on status
            switch (result.status) {
                case ExecutionStatus::Filled:
                    metrics.ordersFilled++;
                    break;
                case ExecutionStatus::PartialFill:
                    metrics.ordersPartialFilled++;
                    break;
                case ExecutionStatus::Cancelled:
                    metrics.ordersCancelled++;
                    break;
                case ExecutionStatus::Rejected:
                    metrics.ordersRejected++;
                    break;
                default:
                    break;
            }
            
            // Update strategy state for fills
            if (result.hasFills()) {
                for (const auto& trade : result.trades) {
                    metrics.totalTrades++;
                    metrics.totalVolume += trade.quantity;
                    
                    // Update strategy position
                    updateStrategyPosition(strategyName, strategy, order.side, 
                                          trade.quantity, trade.price);
                    
                    // Notify strategy
                    strategy.onTradeExecuted(trade, true);
                    
                    // Update P&L tracker
                    if (config_.enablePnLTracking && pnlTracker_) {
                        uint64_t buyClientId = (order.side == Side::Buy) ? order.clientId : 0;
                        uint64_t sellClientId = (order.side == Side::Sell) ? order.clientId : 0;
                        pnlTracker_->onTradeExecuted(trade, buyClientId, sellClientId, trade.price);
                    }
                }
            }
            
            // Notify strategy of execution
            strategy.onOrderFilled(result);
            
            // Remove filled/cancelled orders from tracking
            if (result.status == ExecutionStatus::Filled || 
                result.status == ExecutionStatus::Cancelled) {
                removeOrderFromTracking(strategyName, order.id);
            }
        }

        /**
         * Update strategy position after fill
         */
        void updateStrategyPosition(const std::string& name, Strategy& strategy,
                                    Side side, uint64_t qty, int64_t price) {
            auto& state = strategy.getState();
            auto& metrics = strategyMetrics_[name];
            
            if (side == Side::Buy) {
                state.netPosition += static_cast<int64_t>(qty);
                state.longPosition += static_cast<int64_t>(qty);
            } else {
                state.netPosition -= static_cast<int64_t>(qty);
                state.shortPosition += static_cast<int64_t>(qty);
            }
            
            // Update metrics
            metrics.netPosition = state.netPosition;
            metrics.maxPosition = std::max(metrics.maxPosition, 
                                           std::abs(state.netPosition));
            
            // Update P&L with current market price
            if (lastTick_.isValid()) {
                state.updateUnrealizedPnL(lastTick_.midPrice(), price);
                metrics.unrealizedPnL = state.unrealizedPnL;
                metrics.realizedPnL = state.realizedPnL;
                metrics.totalPnL = state.totalPnL;
            }
            
            // Call strategy-specific position update
            if (auto* mm = dynamic_cast<MarketMakingStrategy*>(&strategy)) {
                mm->updatePosition(side, qty, price);
            } else if (auto* mom = dynamic_cast<MomentumStrategy*>(&strategy)) {
                mom->updatePosition(side, qty, price);
            }
        }

        /**
         * Handle trade from matching engine
         */
        void onTradeExecuted(const Trade& trade) {
            // Find which strategy this trade belongs to
            auto buyIt = orderToStrategy_.find(trade.buyOrderId);
            auto sellIt = orderToStrategy_.find(trade.sellOrderId);
            
            // Update metrics and notify for buy side strategy
            if (buyIt != orderToStrategy_.end()) {
                const std::string& strategyName = buyIt->second;
                if (auto* strategy = getStrategy(strategyName)) {
                    // Update metrics
                    auto& metrics = strategyMetrics_[strategyName];
                    metrics.totalTrades++;
                    metrics.totalVolume += trade.quantity;
                    metrics.ordersFilled++;
                    
                    // Update strategy position (bought = long)
                    updateStrategyPosition(strategyName, *strategy, Side::Buy, 
                                          trade.quantity, trade.price);
                    
                    // Notify strategy
                    strategy->onTradeExecuted(trade, true);
                    
                    // Update P&L tracker
                    if (config_.enablePnLTracking && pnlTracker_) {
                        pnlTracker_->onTradeExecuted(trade, 
                            strategy->getConfig().clientId, 0, trade.price);
                    }
                }
            }
            
            // Update metrics and notify for sell side strategy
            if (sellIt != orderToStrategy_.end()) {
                const std::string& strategyName = sellIt->second;
                if (auto* strategy = getStrategy(strategyName)) {
                    // Update metrics
                    auto& metrics = strategyMetrics_[strategyName];
                    metrics.totalTrades++;
                    metrics.totalVolume += trade.quantity;
                    metrics.ordersFilled++;
                    
                    // Update strategy position (sold = short)
                    updateStrategyPosition(strategyName, *strategy, Side::Sell, 
                                          trade.quantity, trade.price);
                    
                    // Notify strategy
                    strategy->onTradeExecuted(trade, true);
                    
                    // Update P&L tracker
                    if (config_.enablePnLTracking && pnlTracker_) {
                        pnlTracker_->onTradeExecuted(trade, 
                            0, strategy->getConfig().clientId, trade.price);
                    }
                }
            }
        }

        /**
         * Handle execution from matching engine
         */
        void onOrderExecuted(const ExecutionResult& result) {
            // Order execution is handled in executeSignal/processExecutionResult
            // This callback is for external orders or additional processing
        }

        /**
         * Cancel orders on one side for a strategy
         */
        void cancelSideOrders(const std::string& name, Side side) {
            auto it = strategyOrders_.find(name);
            if (it == strategyOrders_.end()) return;
            
            std::vector<uint64_t> toCancel;
            for (uint64_t orderId : it->second) {
                auto orderOpt = engine_.getOrderBook().getOrder(orderId);
                if (orderOpt && orderOpt->side == side) {
                    toCancel.push_back(orderId);
                }
            }
            
            for (uint64_t orderId : toCancel) {
                engine_.cancelOrder(orderId);
            }
        }

        /**
         * Remove order from tracking
         */
        void removeOrderFromTracking(const std::string& strategyName, uint64_t orderId) {
            // Remove from strategy orders
            auto it = strategyOrders_.find(strategyName);
            if (it != strategyOrders_.end()) {
                auto& orders = it->second;
                orders.erase(std::remove(orders.begin(), orders.end(), orderId), orders.end());
            }
            
            // Remove from order mapping
            orderToStrategy_.erase(orderId);
        }

        /**
         * Log a signal
         */
        void logSignal(const std::string& strategyName, const StrategySignal& signal) {
            const char* typeStr = "";
            switch (signal.type) {
                case SignalType::Buy: typeStr = "BUY"; break;
                case SignalType::Sell: typeStr = "SELL"; break;
                case SignalType::CloseLong: typeStr = "CLOSE_LONG"; break;
                case SignalType::CloseShort: typeStr = "CLOSE_SHORT"; break;
                case SignalType::CancelBids: typeStr = "CANCEL_BIDS"; break;
                case SignalType::CancelAsks: typeStr = "CANCEL_ASKS"; break;
                case SignalType::CancelAll: typeStr = "CANCEL_ALL"; break;
                default: typeStr = "NONE"; break;
            }
            
            std::cout << "[" << strategyName << "] Signal: " << typeStr
                      << " Price=" << signal.price 
                      << " Qty=" << signal.quantity
                      << " Confidence=" << signal.confidence
                      << " (" << signal.reason << ")\n";
        }

        /**
         * Log an execution
         */
        void logExecution(const std::string& strategyName, const ExecutionResult& result) {
            const char* statusStr = "";
            switch (result.status) {
                case ExecutionStatus::Filled: statusStr = "FILLED"; break;
                case ExecutionStatus::PartialFill: statusStr = "PARTIAL"; break;
                case ExecutionStatus::Resting: statusStr = "RESTING"; break;
                case ExecutionStatus::Cancelled: statusStr = "CANCELLED"; break;
                case ExecutionStatus::Modified: statusStr = "MODIFIED"; break;
                case ExecutionStatus::Rejected: statusStr = "REJECTED"; break;
            }
            
            std::cout << "[" << strategyName << "] Order " << result.orderId
                      << ": " << statusStr
                      << " Filled=" << result.filledQuantity
                      << " Remaining=" << result.remainingQuantity
                      << " Trades=" << result.trades.size() << "\n";
        }
    };

}
