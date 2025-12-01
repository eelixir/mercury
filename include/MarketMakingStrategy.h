#pragma once

#include "Strategy.h"
#include <deque>
#include <cmath>
#include <algorithm>

namespace Mercury {

    /**
     * Market Making Strategy Configuration
     */
    struct MarketMakingConfig : public StrategyConfig {
        // Spread parameters
        int64_t minSpread = 2;             // Minimum spread to quote (in price ticks)
        int64_t maxSpread = 20;            // Maximum spread to quote
        double spreadMultiplier = 1.5;     // Multiplier applied to market spread
        
        // Quoting parameters
        uint64_t quoteQuantity = 100;      // Default quantity per quote
        int64_t tickSize = 1;              // Minimum price increment
        int64_t priceOffset = 0;           // Additional offset from mid price
        
        // Inventory management
        double inventorySkew = 0.1;        // How much to skew quotes based on position
        int64_t targetInventory = 0;       // Target inventory level
        int64_t maxInventory = 500;        // Maximum absolute inventory
        
        // Quote behavior
        bool quoteOnBothSides = true;      // Always quote bid and ask
        bool fadeWhenFilled = true;        // Widen spread after fills
        uint64_t fadeDuration = 5000;      // How long to fade (ms)
        double fadeMultiplier = 1.5;       // Spread multiplier when fading
        
        // Update frequency
        uint64_t requoteInterval = 1000;   // Min ms between requotes
        uint64_t cancelStaleAfter = 10000; // Cancel orders older than this (ms)
        
        MarketMakingConfig() {
            name = "MarketMaking";
            maxPosition = 1000;
            maxOrderQuantity = 500;
        }
    };

    /**
     * MarketMakingStrategy - Provides liquidity by quoting bid and ask
     * 
     * Core logic:
     * 1. Calculate fair value (mid price)
     * 2. Apply spread based on market conditions
     * 3. Skew quotes based on inventory
     * 4. Manage open orders (cancel stale, update on market moves)
     * 
     * Example:
     *   MarketMakingConfig config;
     *   config.minSpread = 2;
     *   config.quoteQuantity = 100;
     *   
     *   MarketMakingStrategy strategy(config);
     *   auto signals = strategy.onMarketTick(tick);
     */
    class MarketMakingStrategy : public Strategy {
    public:
        MarketMakingStrategy() : Strategy() {
            config_.name = "MarketMaking";
        }

        explicit MarketMakingStrategy(const MarketMakingConfig& config)
            : Strategy(config), mmConfig_(config) {
            config_.name = "MarketMaking";
        }

        std::string getName() const override {
            return "MarketMaking";
        }

        /**
         * Process market tick and generate quote updates
         */
        std::vector<StrategySignal> onMarketTick(const MarketTick& tick) override {
            std::vector<StrategySignal> signals;

            if (!config_.enabled || !tick.isValid()) {
                return signals;
            }

            // Store tick for analysis
            lastTick_ = tick;
            updatePriceHistory(tick);

            // Calculate fair value and spread
            int64_t fairValue = calculateFairValue(tick);
            int64_t halfSpread = calculateHalfSpread(tick);

            // Apply inventory skew
            auto [bidOffset, askOffset] = calculateInventorySkew();

            // Calculate quote prices
            int64_t bidPrice = fairValue - halfSpread + bidOffset;
            int64_t askPrice = fairValue + halfSpread + askOffset;

            // Round to tick size
            bidPrice = roundToTick(bidPrice, mmConfig_.tickSize);
            askPrice = roundToTick(askPrice, mmConfig_.tickSize);

            // Ensure minimum spread
            if (askPrice - bidPrice < mmConfig_.minSpread) {
                bidPrice = fairValue - mmConfig_.minSpread / 2;
                askPrice = fairValue + (mmConfig_.minSpread + 1) / 2;
            }

            // Calculate quote quantities with inventory adjustment
            auto [bidQty, askQty] = calculateQuoteQuantities();

            // Generate signals for bid side
            if (mmConfig_.quoteOnBothSides || state_.netPosition < mmConfig_.targetInventory) {
                if (bidQty > 0 && checkRiskLimits(Side::Buy, bidPrice, bidQty)) {
                    // Check if we need to update existing bid
                    if (shouldUpdateQuote(Side::Buy, bidPrice, bidQty)) {
                        StrategySignal signal;
                        signal.type = SignalType::Buy;
                        signal.price = bidPrice;
                        signal.quantity = bidQty;
                        signal.confidence = calculateConfidence(tick);
                        signal.reason = "Market making bid quote";
                        signals.push_back(signal);
                    }
                }
            }

            // Generate signals for ask side
            if (mmConfig_.quoteOnBothSides || state_.netPosition > mmConfig_.targetInventory) {
                if (askQty > 0 && checkRiskLimits(Side::Sell, askPrice, askQty)) {
                    if (shouldUpdateQuote(Side::Sell, askPrice, askQty)) {
                        StrategySignal signal;
                        signal.type = SignalType::Sell;
                        signal.price = askPrice;
                        signal.quantity = askQty;
                        signal.confidence = calculateConfidence(tick);
                        signal.reason = "Market making ask quote";
                        signals.push_back(signal);
                    }
                }
            }

            // Check for stale orders to cancel
            signals = checkAndCancelStaleOrders(signals, tick.timestamp);

            lastUpdateTime_ = tick.timestamp;
            return signals;
        }

        /**
         * Handle trade execution
         */
        void onTradeExecuted(const Trade& trade, bool wasOurOrder) override {
            if (wasOurOrder) {
                lastFillTime_ = trade.timestamp;
                recentFills_.push_back(trade);
                
                // Limit fill history
                if (recentFills_.size() > 100) {
                    recentFills_.pop_front();
                }

                // Activate fade mode after fill
                if (mmConfig_.fadeWhenFilled) {
                    fadeUntil_ = trade.timestamp + mmConfig_.fadeDuration;
                }
            }
        }

        /**
         * Handle order fill notification
         */
        void onOrderFilled(const ExecutionResult& result) override {
            state_.totalTrades++;
            state_.totalVolume += result.filledQuantity;
            
            // Update position tracking
            // Note: Actual position updates should come from StrategyManager
            // based on whether it was a buy or sell fill
        }

        /**
         * Update position after a fill
         */
        void updatePosition(Side side, uint64_t filledQty, int64_t price) {
            if (side == Side::Buy) {
                state_.netPosition += static_cast<int64_t>(filledQty);
                state_.longPosition += static_cast<int64_t>(filledQty);
            } else {
                state_.netPosition -= static_cast<int64_t>(filledQty);
                state_.shortPosition += static_cast<int64_t>(filledQty);
            }
            
            // Track average entry for P&L
            updateAverageEntry(side, filledQty, price);
        }

        /**
         * Reset strategy state
         */
        void reset() override {
            Strategy::reset();
            priceHistory_.clear();
            recentFills_.clear();
            lastBidPrice_ = 0;
            lastAskPrice_ = 0;
            lastBidQty_ = 0;
            lastAskQty_ = 0;
            lastUpdateTime_ = 0;
            lastFillTime_ = 0;
            fadeUntil_ = 0;
            avgEntryPrice_ = 0;
            totalCost_ = 0;
        }

        // Accessors
        const MarketMakingConfig& getMMConfig() const { return mmConfig_; }
        void setMMConfig(const MarketMakingConfig& config) { 
            mmConfig_ = config; 
            config_ = config;
        }

        int64_t getLastBidPrice() const { return lastBidPrice_; }
        int64_t getLastAskPrice() const { return lastAskPrice_; }
        bool isFading() const { return lastTick_.timestamp < fadeUntil_; }

    private:
        MarketMakingConfig mmConfig_;
        
        // Price tracking
        std::deque<int64_t> priceHistory_;
        std::deque<Trade> recentFills_;
        
        // Quote state
        int64_t lastBidPrice_ = 0;
        int64_t lastAskPrice_ = 0;
        uint64_t lastBidQty_ = 0;
        uint64_t lastAskQty_ = 0;
        uint64_t lastUpdateTime_ = 0;
        uint64_t lastFillTime_ = 0;
        uint64_t fadeUntil_ = 0;

        // P&L tracking
        int64_t avgEntryPrice_ = 0;
        int64_t totalCost_ = 0;

        /**
         * Calculate fair value from market data
         */
        int64_t calculateFairValue(const MarketTick& tick) const {
            // Use mid price as basic fair value
            int64_t fairValue = tick.midPrice();

            // If mid price not available, use last trade
            if (fairValue == 0) {
                fairValue = tick.lastTradePrice;
            }

            // Could add more sophisticated fair value models here:
            // - VWAP of recent trades
            // - Weighted by order book depth
            // - Mean reversion adjustments

            return fairValue;
        }

        /**
         * Calculate half spread based on market conditions
         */
        int64_t calculateHalfSpread(const MarketTick& tick) const {
            // Start with market spread
            int64_t marketSpread = tick.spread();
            
            // Apply multiplier
            int64_t targetSpread = static_cast<int64_t>(
                marketSpread * mmConfig_.spreadMultiplier);
            
            // Add price offset
            targetSpread += mmConfig_.priceOffset * 2;

            // Apply fade multiplier if recently filled
            if (lastTick_.timestamp < fadeUntil_) {
                targetSpread = static_cast<int64_t>(targetSpread * mmConfig_.fadeMultiplier);
            }

            // Clamp to configured range
            targetSpread = std::clamp(targetSpread, mmConfig_.minSpread, mmConfig_.maxSpread);

            return (targetSpread + 1) / 2;  // Half spread, rounded up
        }

        /**
         * Calculate inventory skew for bid/ask offsets
         */
        std::pair<int64_t, int64_t> calculateInventorySkew() const {
            // Calculate position deviation from target
            int64_t positionDelta = state_.netPosition - mmConfig_.targetInventory;
            
            // Calculate skew as percentage of max inventory
            double skewPct = 0.0;
            if (mmConfig_.maxInventory > 0) {
                skewPct = static_cast<double>(positionDelta) / 
                          static_cast<double>(mmConfig_.maxInventory);
            }
            
            // Apply skew factor
            skewPct *= mmConfig_.inventorySkew;
            
            // Convert to price offset (negative skew = lower bid, higher ask)
            // When long, we want to sell more aggressively (lower ask, higher bid to reduce fills)
            int64_t skewTicks = static_cast<int64_t>(skewPct * mmConfig_.maxSpread);
            
            // Bid offset: negative when long (discourages more buying)
            // Ask offset: negative when long (encourages selling)
            int64_t bidOffset = -skewTicks;
            int64_t askOffset = -skewTicks;

            return {bidOffset, askOffset};
        }

        /**
         * Calculate quantities for each side
         */
        std::pair<uint64_t, uint64_t> calculateQuoteQuantities() const {
            uint64_t bidQty = mmConfig_.quoteQuantity;
            uint64_t askQty = mmConfig_.quoteQuantity;

            // Reduce bid qty when long (approaching max inventory)
            if (state_.netPosition > 0) {
                double positionRatio = static_cast<double>(state_.netPosition) / 
                                       static_cast<double>(mmConfig_.maxInventory);
                bidQty = static_cast<uint64_t>(bidQty * (1.0 - positionRatio * 0.5));
            }

            // Reduce ask qty when short
            if (state_.netPosition < 0) {
                double positionRatio = static_cast<double>(-state_.netPosition) / 
                                       static_cast<double>(mmConfig_.maxInventory);
                askQty = static_cast<uint64_t>(askQty * (1.0 - positionRatio * 0.5));
            }

            // At max inventory, only quote reducing side
            if (state_.netPosition >= mmConfig_.maxInventory) {
                bidQty = 0;
            }
            if (state_.netPosition <= -mmConfig_.maxInventory) {
                askQty = 0;
            }

            return {bidQty, askQty};
        }

        /**
         * Check if we should update an existing quote
         */
        bool shouldUpdateQuote(Side side, int64_t newPrice, uint64_t newQty) const {
            if (side == Side::Buy) {
                // Update if price or quantity changed significantly
                if (lastBidPrice_ == 0) return true;
                if (std::abs(newPrice - lastBidPrice_) >= mmConfig_.tickSize) return true;
                if (newQty != lastBidQty_) return true;
            } else {
                if (lastAskPrice_ == 0) return true;
                if (std::abs(newPrice - lastAskPrice_) >= mmConfig_.tickSize) return true;
                if (newQty != lastAskQty_) return true;
            }

            // Check requote interval
            if (lastTick_.timestamp - lastUpdateTime_ >= mmConfig_.requoteInterval) {
                return true;
            }

            return false;
        }

        /**
         * Check for stale orders and generate cancel signals
         */
        std::vector<StrategySignal> checkAndCancelStaleOrders(
            std::vector<StrategySignal>& signals, uint64_t currentTime) const {
            
            // In a real implementation, we would track order timestamps
            // and cancel orders that are too old
            // For now, this is handled by the StrategyManager
            
            return signals;
        }

        /**
         * Update price history for analysis
         */
        void updatePriceHistory(const MarketTick& tick) {
            if (tick.midPrice() > 0) {
                priceHistory_.push_back(tick.midPrice());
                
                // Keep last 100 prices
                if (priceHistory_.size() > 100) {
                    priceHistory_.pop_front();
                }
            }
        }

        /**
         * Calculate signal confidence based on market conditions
         */
        double calculateConfidence(const MarketTick& tick) const {
            double confidence = 0.5;  // Base confidence

            // Higher confidence with tighter spreads
            if (tick.spread() > 0 && tick.spread() <= mmConfig_.minSpread) {
                confidence += 0.2;
            }

            // Higher confidence with more stable prices
            if (priceHistory_.size() >= 10) {
                double volatility = calculateVolatility();
                if (volatility < 0.01) {
                    confidence += 0.2;
                }
            }

            // Lower confidence when near position limits
            double positionUtil = std::abs(static_cast<double>(state_.netPosition)) / 
                                  static_cast<double>(mmConfig_.maxInventory);
            confidence -= positionUtil * 0.2;

            return std::clamp(confidence, 0.1, 0.95);
        }

        /**
         * Calculate price volatility
         */
        double calculateVolatility() const {
            if (priceHistory_.size() < 2) return 0.0;

            double sum = 0.0;
            double sumSq = 0.0;
            for (const auto& price : priceHistory_) {
                sum += static_cast<double>(price);
                sumSq += static_cast<double>(price) * static_cast<double>(price);
            }

            double n = static_cast<double>(priceHistory_.size());
            double mean = sum / n;
            double variance = (sumSq / n) - (mean * mean);

            return std::sqrt(variance) / mean;  // Coefficient of variation
        }

        /**
         * Round price to nearest tick
         */
        int64_t roundToTick(int64_t price, int64_t tickSize) const {
            if (tickSize <= 1) return price;
            return (price / tickSize) * tickSize;
        }

        /**
         * Update average entry price after fill
         */
        void updateAverageEntry(Side side, uint64_t qty, int64_t price) {
            if (side == Side::Buy) {
                // Buying increases long position
                if (state_.longPosition > 0) {
                    totalCost_ += static_cast<int64_t>(qty) * price;
                    avgEntryPrice_ = totalCost_ / state_.longPosition;
                }
            } else {
                // Selling decreases position (or opens short)
                // For simplicity, use last price as avg for shorts
                avgEntryPrice_ = price;
            }
        }
    };

}
