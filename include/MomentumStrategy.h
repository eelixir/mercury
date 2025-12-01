#pragma once

#include "Strategy.h"
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Mercury {

    /**
     * Momentum Strategy Configuration
     */
    struct MomentumConfig : public StrategyConfig {
        // Lookback periods for momentum calculation
        uint64_t shortPeriod = 5;          // Short-term lookback (fast signal)
        uint64_t longPeriod = 20;          // Long-term lookback (slow signal)
        uint64_t signalPeriod = 9;         // Signal line smoothing
        
        // Entry thresholds
        double entryThreshold = 0.02;      // Min momentum % for entry (2%)
        double exitThreshold = 0.005;      // Exit when momentum falls below (0.5%)
        double strongSignal = 0.05;        // Strong momentum threshold (5%)
        
        // Position sizing
        uint64_t baseQuantity = 100;       // Base order size
        double momentumScale = 2.0;        // Scale position with momentum strength
        uint64_t maxPositionUnits = 5;     // Max position as multiple of base
        
        // Risk management
        double stopLossPct = 0.03;         // Stop loss percentage (3%)
        double takeProfitPct = 0.06;       // Take profit percentage (6%)
        bool useTrailingStop = true;       // Use trailing stop loss
        double trailingStopPct = 0.02;     // Trailing stop distance (2%)
        
        // Signal confirmation
        uint64_t confirmationBars = 2;     // Bars to confirm signal
        bool requireVolumeConfirm = true;  // Require volume spike for entry
        double volumeMultiple = 1.5;       // Volume must be this multiple of avg
        
        // Trend filter
        bool useTrendFilter = true;        // Only trade with trend
        uint64_t trendPeriod = 50;         // Trend lookback
        
        // Order type
        bool useMarketOrders = true;       // Use market orders for quick execution
        int64_t limitOffset = 2;           // Ticks away from market for limits
        
        MomentumConfig() {
            name = "Momentum";
            maxPosition = 200;        // Lower max position for risk control
            maxOrderQuantity = 100;   // Smaller orders
        }
    };

    /**
     * Price Bar - OHLCV data for momentum calculation
     */
    struct PriceBar {
        uint64_t timestamp = 0;
        int64_t open = 0;
        int64_t high = 0;
        int64_t low = 0;
        int64_t close = 0;
        uint64_t volume = 0;
        
        int64_t typical() const {
            return (high + low + close) / 3;
        }
        
        bool isValid() const {
            return close > 0;
        }
    };

    /**
     * Momentum Indicators
     */
    struct MomentumIndicators {
        double shortMA = 0.0;              // Short-term moving average
        double longMA = 0.0;               // Long-term moving average
        double momentum = 0.0;             // Raw momentum (short - long) / long
        double macd = 0.0;                 // MACD line
        double signal = 0.0;               // Signal line
        double histogram = 0.0;            // MACD histogram
        double rsi = 0.0;                  // RSI (0-100)
        double avgVolume = 0.0;            // Average volume
        double trendSlope = 0.0;           // Trend direction
        bool trendUp = false;              // Is trend bullish
        bool trendDown = false;            // Is trend bearish
    };

    /**
     * MomentumStrategy - Trend-following strategy based on price momentum
     * 
     * Core logic:
     * 1. Calculate momentum using moving average crossovers
     * 2. Generate buy signals when momentum turns positive
     * 3. Generate sell signals when momentum turns negative
     * 4. Use position sizing based on signal strength
     * 5. Apply trend filter to avoid counter-trend trades
     * 
     * Indicators used:
     * - Simple Moving Averages (SMA) for trend
     * - MACD for momentum
     * - RSI for overbought/oversold
     * - Volume for confirmation
     * 
     * Example:
     *   MomentumConfig config;
     *   config.shortPeriod = 10;
     *   config.longPeriod = 30;
     *   
     *   MomentumStrategy strategy(config);
     *   auto signals = strategy.onMarketTick(tick);
     */
    class MomentumStrategy : public Strategy {
    public:
        MomentumStrategy() : Strategy() {
            config_.name = "Momentum";
        }

        explicit MomentumStrategy(const MomentumConfig& config)
            : Strategy(config), momConfig_(config) {
            config_.name = "Momentum";
        }

        std::string getName() const override {
            return "Momentum";
        }

        /**
         * Process market tick and generate momentum signals
         */
        std::vector<StrategySignal> onMarketTick(const MarketTick& tick) override {
            std::vector<StrategySignal> signals;

            if (!config_.enabled || !tick.isValid()) {
                return signals;
            }

            // Store tick and update price bars
            lastTick_ = tick;
            currentTick_++;
            updatePriceBars(tick);

            // Need enough data for calculations
            if (prices_.size() < momConfig_.longPeriod) {
                return signals;
            }

            // Calculate indicators
            MomentumIndicators ind = calculateIndicators();
            lastIndicators_ = ind;

            // Check for entry signals
            if (state_.netPosition == 0) {
                // No position - look for entry
                signals = checkEntrySignals(ind, tick);
            } else {
                // Have position - check for exit or add
                signals = checkExitSignals(ind, tick);
            }

            return signals;
        }

        /**
         * Handle trade execution
         */
        void onTradeExecuted(const Trade& trade, bool wasOurOrder) override {
            if (wasOurOrder) {
                // Update entry price tracking for stops
                if (state_.netPosition != 0) {
                    // Update high water mark for trailing stop
                    if (state_.netPosition > 0) {
                        highWaterMark_ = std::max(highWaterMark_, trade.price);
                    } else {
                        lowWaterMark_ = std::min(lowWaterMark_, trade.price);
                    }
                }
            }
        }

        /**
         * Handle order fill notification
         */
        void onOrderFilled(const ExecutionResult& result) override {
            state_.totalTrades++;
            state_.totalVolume += result.filledQuantity;
        }

        /**
         * Update position tracking after fill
         * Note: Position (state_.netPosition) is already updated by StrategyManager
         * This method only handles strategy-specific tracking like entry price
         */
        void updatePosition(Side side, uint64_t filledQty, int64_t price) {
            // Track entry price for stop loss / take profit
            if (side == Side::Buy) {
                // Record entry for new long position
                if (state_.netPosition > 0 && entryPrice_ == 0) {
                    entryPrice_ = price;
                    highWaterMark_ = price;
                }
                // Update high water mark
                if (state_.netPosition > 0) {
                    highWaterMark_ = std::max(highWaterMark_, price);
                }
            } else {
                // Record entry for new short
                if (state_.netPosition < 0 && entryPrice_ == 0) {
                    entryPrice_ = price;
                    lowWaterMark_ = price;
                }
                // Update low water mark
                if (state_.netPosition < 0) {
                    if (lowWaterMark_ == 0) lowWaterMark_ = price;
                    else lowWaterMark_ = std::min(lowWaterMark_, price);
                }
            }

            // Reset entry tracking if position closed
            if (state_.netPosition == 0) {
                entryPrice_ = 0;
                highWaterMark_ = 0;
                lowWaterMark_ = 0;
            }
        }

        /**
         * Reset strategy state
         */
        void reset() override {
            Strategy::reset();
            prices_.clear();
            volumes_.clear();
            bars_.clear();
            entryPrice_ = 0;
            highWaterMark_ = 0;
            lowWaterMark_ = 0;
            entryTick_ = 0;
            currentTick_ = 0;
            entryHistogram_ = 0.0;
            lastIndicators_ = MomentumIndicators{};
            signalConfirmCount_ = 0;
            lastSignalSide_ = Side::Buy;
        }

        // Accessors
        const MomentumConfig& getMomConfig() const { return momConfig_; }
        void setMomConfig(const MomentumConfig& config) { 
            momConfig_ = config; 
            config_ = config;
        }

        const MomentumIndicators& getIndicators() const { return lastIndicators_; }
        int64_t getEntryPrice() const { return entryPrice_; }

    private:
        MomentumConfig momConfig_;
        
        // Price data
        std::deque<int64_t> prices_;
        std::deque<uint64_t> volumes_;
        std::deque<PriceBar> bars_;
        
        // Position tracking
        int64_t entryPrice_ = 0;
        int64_t highWaterMark_ = 0;
        int64_t lowWaterMark_ = 0;
        uint64_t entryTick_ = 0;           // Tick when position was opened
        uint64_t currentTick_ = 0;          // Current tick counter
        double entryHistogram_ = 0.0;       // MACD histogram at entry
        
        // Indicators
        MomentumIndicators lastIndicators_;
        
        // Signal confirmation
        uint64_t signalConfirmCount_ = 0;
        Side lastSignalSide_ = Side::Buy;

        /**
         * Update price data from tick
         */
        void updatePriceBars(const MarketTick& tick) {
            int64_t price = tick.midPrice();
            if (price == 0) price = tick.lastTradePrice;
            if (price == 0) return;

            prices_.push_back(price);
            volumes_.push_back(tick.lastTradeQuantity);

            // Create bar from tick (simplified - in practice would aggregate)
            PriceBar bar;
            bar.timestamp = tick.timestamp;
            bar.open = price;
            bar.high = price;
            bar.low = price;
            bar.close = price;
            bar.volume = tick.lastTradeQuantity;
            bars_.push_back(bar);

            // Keep limited history
            size_t maxHistory = std::max(momConfig_.trendPeriod, momConfig_.longPeriod) * 2;
            while (prices_.size() > maxHistory) {
                prices_.pop_front();
                volumes_.pop_front();
                bars_.pop_front();
            }
        }

        /**
         * Calculate all momentum indicators
         */
        MomentumIndicators calculateIndicators() const {
            MomentumIndicators ind;

            if (prices_.size() < momConfig_.longPeriod) {
                return ind;
            }

            // Calculate moving averages
            ind.shortMA = calculateSMA(momConfig_.shortPeriod);
            ind.longMA = calculateSMA(momConfig_.longPeriod);

            // Calculate momentum
            if (ind.longMA > 0) {
                ind.momentum = (ind.shortMA - ind.longMA) / ind.longMA;
            }

            // Calculate MACD
            double emaShort = calculateEMA(momConfig_.shortPeriod);
            double emaLong = calculateEMA(momConfig_.longPeriod);
            ind.macd = emaShort - emaLong;
            ind.signal = calculateSignalLine(ind.macd);
            ind.histogram = ind.macd - ind.signal;

            // Calculate RSI
            ind.rsi = calculateRSI(14);

            // Calculate average volume
            ind.avgVolume = calculateAvgVolume(20);

            // Calculate trend
            if (momConfig_.useTrendFilter && prices_.size() >= momConfig_.trendPeriod) {
                ind.trendSlope = calculateTrendSlope(momConfig_.trendPeriod);
                ind.trendUp = ind.trendSlope > 0.0001;    // Slight positive slope
                ind.trendDown = ind.trendSlope < -0.0001; // Slight negative slope
            }

            return ind;
        }

        /**
         * Calculate Simple Moving Average
         */
        double calculateSMA(uint64_t period) const {
            if (prices_.size() < period) return 0.0;
            
            double sum = 0.0;
            auto it = prices_.end() - period;
            for (uint64_t i = 0; i < period; ++i, ++it) {
                sum += static_cast<double>(*it);
            }
            return sum / static_cast<double>(period);
        }

        /**
         * Calculate Exponential Moving Average
         */
        double calculateEMA(uint64_t period) const {
            if (prices_.size() < period) return 0.0;
            
            double multiplier = 2.0 / (static_cast<double>(period) + 1.0);
            double ema = calculateSMA(period);  // Initialize with SMA
            
            auto it = prices_.end() - period;
            for (uint64_t i = 0; i < period; ++i, ++it) {
                ema = (static_cast<double>(*it) - ema) * multiplier + ema;
            }
            return ema;
        }

        /**
         * Calculate signal line (EMA of MACD)
         */
        double calculateSignalLine(double currentMACD) const {
            // Simplified - in practice would maintain MACD history
            static std::deque<double> macdHistory;
            macdHistory.push_back(currentMACD);
            
            if (macdHistory.size() > momConfig_.signalPeriod) {
                macdHistory.pop_front();
            }
            
            if (macdHistory.size() < momConfig_.signalPeriod) {
                return currentMACD;
            }
            
            double sum = std::accumulate(macdHistory.begin(), macdHistory.end(), 0.0);
            return sum / static_cast<double>(macdHistory.size());
        }

        /**
         * Calculate RSI (Relative Strength Index)
         */
        double calculateRSI(uint64_t period) const {
            if (prices_.size() < period + 1) return 50.0;  // Neutral

            double gains = 0.0;
            double losses = 0.0;
            
            auto it = prices_.end() - period - 1;
            for (uint64_t i = 0; i < period; ++i, ++it) {
                double change = static_cast<double>(*(it + 1)) - static_cast<double>(*it);
                if (change > 0) {
                    gains += change;
                } else {
                    losses -= change;  // Make positive
                }
            }

            if (losses == 0) return 100.0;
            
            double rs = gains / losses;
            return 100.0 - (100.0 / (1.0 + rs));
        }

        /**
         * Calculate average volume
         */
        double calculateAvgVolume(uint64_t period) const {
            if (volumes_.size() < period) return 0.0;
            
            double sum = 0.0;
            auto it = volumes_.end() - period;
            for (uint64_t i = 0; i < period; ++i, ++it) {
                sum += static_cast<double>(*it);
            }
            return sum / static_cast<double>(period);
        }

        /**
         * Calculate trend slope using linear regression
         */
        double calculateTrendSlope(uint64_t period) const {
            if (prices_.size() < period) return 0.0;

            // Simple linear regression
            double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0;
            auto it = prices_.end() - period;
            
            for (uint64_t i = 0; i < period; ++i, ++it) {
                double x = static_cast<double>(i);
                double y = static_cast<double>(*it);
                sumX += x;
                sumY += y;
                sumXY += x * y;
                sumX2 += x * x;
            }

            double n = static_cast<double>(period);
            double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
            
            // Normalize by average price
            double avgPrice = sumY / n;
            if (avgPrice > 0) {
                slope /= avgPrice;
            }

            return slope;
        }

        /**
         * Check for entry signals
         */
        std::vector<StrategySignal> checkEntrySignals(
            const MomentumIndicators& ind, const MarketTick& tick) {
            
            std::vector<StrategySignal> signals;

            // Check for bullish momentum
            if (shouldGoLong(ind, tick)) {
                // Confirm signal
                if (lastSignalSide_ == Side::Buy) {
                    signalConfirmCount_++;
                } else {
                    signalConfirmCount_ = 1;
                    lastSignalSide_ = Side::Buy;
                }

                if (signalConfirmCount_ >= momConfig_.confirmationBars) {
                    uint64_t qty = calculatePositionSize(ind.momentum);
                    
                    // Respect max position limit
                    int64_t maxPos = static_cast<int64_t>(config_.maxPosition);
                    if (state_.netPosition + static_cast<int64_t>(qty) > maxPos) {
                        qty = static_cast<uint64_t>(std::max(int64_t(0), maxPos - state_.netPosition));
                    }
                    
                    if (qty > 0) {
                        int64_t price = momConfig_.useMarketOrders ? 0 : 
                                        tick.askPrice + momConfig_.limitOffset;

                        StrategySignal signal;
                        signal.type = SignalType::Buy;
                        signal.price = price;
                        signal.quantity = qty;
                        signal.confidence = calculateConfidence(ind);
                        signal.reason = formatReason("Long entry", ind);
                        signals.push_back(signal);
                        
                        // Record entry state
                        entryTick_ = currentTick_;
                        entryHistogram_ = ind.histogram;
                    }
                }
            }
            // Check for bearish momentum
            else if (shouldGoShort(ind, tick)) {
                if (lastSignalSide_ == Side::Sell) {
                    signalConfirmCount_++;
                } else {
                    signalConfirmCount_ = 1;
                    lastSignalSide_ = Side::Sell;
                }

                if (signalConfirmCount_ >= momConfig_.confirmationBars) {
                    uint64_t qty = calculatePositionSize(std::abs(ind.momentum));
                    
                    // Respect max position limit for shorts
                    int64_t maxPos = static_cast<int64_t>(config_.maxPosition);
                    if (-state_.netPosition + static_cast<int64_t>(qty) > maxPos) {
                        qty = static_cast<uint64_t>(std::max(int64_t(0), maxPos + state_.netPosition));
                    }
                    
                    if (qty > 0) {
                        int64_t price = momConfig_.useMarketOrders ? 0 : 
                                        tick.bidPrice - momConfig_.limitOffset;

                        StrategySignal signal;
                        signal.type = SignalType::Sell;
                        signal.price = price;
                        signal.quantity = qty;
                        signal.confidence = calculateConfidence(ind);
                        signal.reason = formatReason("Short entry", ind);
                        signals.push_back(signal);
                        
                        // Record entry state
                        entryTick_ = currentTick_;
                        entryHistogram_ = ind.histogram;
                    }
                }
            } else {
                signalConfirmCount_ = 0;
            }

            return signals;
        }

        /**
         * Check for exit signals
         */
        std::vector<StrategySignal> checkExitSignals(
            const MomentumIndicators& ind, const MarketTick& tick) {
            
            std::vector<StrategySignal> signals;
            int64_t currentPrice = tick.midPrice();
            
            // Minimum hold period (10 ticks) to let trades develop
            uint64_t ticksHeld = currentTick_ - entryTick_;
            bool pastMinHold = ticksHeld >= 10;

            // Long position exit conditions
            if (state_.netPosition > 0) {
                bool shouldExit = false;
                std::string reason;

                // Check stop loss (always active)
                if (entryPrice_ > 0) {
                    double pnlPct = static_cast<double>(currentPrice - entryPrice_) / 
                                    static_cast<double>(entryPrice_);
                    
                    if (pnlPct <= -momConfig_.stopLossPct) {
                        shouldExit = true;
                        reason = "Stop loss triggered";
                    }

                    // Check trailing stop (only after min hold)
                    if (pastMinHold && momConfig_.useTrailingStop && highWaterMark_ > entryPrice_) {
                        double trailingDrop = static_cast<double>(highWaterMark_ - currentPrice) /
                                              static_cast<double>(highWaterMark_);
                        if (trailingDrop >= momConfig_.trailingStopPct) {
                            shouldExit = true;
                            reason = "Trailing stop triggered";
                        }
                    }

                    // Check take profit
                    if (pnlPct >= momConfig_.takeProfitPct) {
                        shouldExit = true;
                        reason = "Take profit target reached";
                    }
                }

                // Momentum-based exits only after minimum hold period
                if (pastMinHold) {
                    // Check strong momentum reversal (3x exit threshold)
                    if (ind.momentum < -momConfig_.exitThreshold * 3) {
                        shouldExit = true;
                        reason = "Strong momentum reversal";
                    }

                    // MACD histogram flip with magnitude (must flip significantly from entry)
                    if (entryHistogram_ > 0 && ind.histogram < -entryHistogram_ * 0.5) {
                        shouldExit = true;
                        reason = "MACD bearish crossover";
                    }
                }

                if (shouldExit) {
                    StrategySignal signal;
                    signal.type = SignalType::CloseLong;
                    signal.price = momConfig_.useMarketOrders ? 0 : 
                                   tick.bidPrice - momConfig_.limitOffset;
                    signal.quantity = static_cast<uint64_t>(state_.netPosition);
                    signal.confidence = 0.8;
                    signal.reason = reason;
                    signals.push_back(signal);
                }
            }
            // Short position exit conditions
            else if (state_.netPosition < 0) {
                bool shouldExit = false;
                std::string reason;

                // Check stop loss (always active)
                if (entryPrice_ > 0) {
                    double pnlPct = static_cast<double>(entryPrice_ - currentPrice) / 
                                    static_cast<double>(entryPrice_);
                    
                    if (pnlPct <= -momConfig_.stopLossPct) {
                        shouldExit = true;
                        reason = "Stop loss triggered";
                    }

                    // Check trailing stop for short (only after min hold)
                    if (pastMinHold && momConfig_.useTrailingStop && lowWaterMark_ > 0) {
                        double trailingRise = static_cast<double>(currentPrice - lowWaterMark_) /
                                              static_cast<double>(lowWaterMark_);
                        if (trailingRise >= momConfig_.trailingStopPct) {
                            shouldExit = true;
                            reason = "Trailing stop triggered";
                        }
                    }

                    // Check take profit
                    if (pnlPct >= momConfig_.takeProfitPct) {
                        shouldExit = true;
                        reason = "Take profit target reached";
                    }
                }

                // Momentum-based exits only after minimum hold period
                if (pastMinHold) {
                    // Check strong momentum reversal (3x exit threshold)
                    if (ind.momentum > momConfig_.exitThreshold * 3) {
                        shouldExit = true;
                        reason = "Strong momentum reversal";
                    }

                    // MACD histogram flip with magnitude (must flip significantly from entry)
                    if (entryHistogram_ < 0 && ind.histogram > -entryHistogram_ * 0.5) {
                        shouldExit = true;
                        reason = "MACD bullish crossover";
                    }
                }

                if (shouldExit) {
                    StrategySignal signal;
                    signal.type = SignalType::CloseShort;
                    signal.price = momConfig_.useMarketOrders ? 0 : 
                                   tick.askPrice + momConfig_.limitOffset;
                    signal.quantity = static_cast<uint64_t>(-state_.netPosition);
                    signal.confidence = 0.8;
                    signal.reason = reason;
                    signals.push_back(signal);
                }
            }

            return signals;
        }

        /**
         * Check if conditions favor going long
         */
        bool shouldGoLong(const MomentumIndicators& ind, const MarketTick& tick) const {
            // Momentum above threshold
            if (ind.momentum < momConfig_.entryThreshold) {
                return false;
            }

            // MACD histogram positive (bullish)
            if (ind.histogram <= 0) {
                return false;
            }

            // RSI not overbought
            if (ind.rsi > 70) {
                return false;
            }

            // Trend filter
            if (momConfig_.useTrendFilter && ind.trendDown) {
                return false;  // Don't go long in downtrend
            }

            // Volume confirmation
            if (momConfig_.requireVolumeConfirm && ind.avgVolume > 0) {
                if (static_cast<double>(tick.lastTradeQuantity) < 
                    ind.avgVolume * momConfig_.volumeMultiple) {
                    return false;
                }
            }

            return true;
        }

        /**
         * Check if conditions favor going short
         */
        bool shouldGoShort(const MomentumIndicators& ind, const MarketTick& tick) const {
            // Negative momentum below threshold
            if (ind.momentum > -momConfig_.entryThreshold) {
                return false;
            }

            // MACD histogram negative (bearish)
            if (ind.histogram >= 0) {
                return false;
            }

            // RSI not oversold
            if (ind.rsi < 30) {
                return false;
            }

            // Trend filter
            if (momConfig_.useTrendFilter && ind.trendUp) {
                return false;  // Don't short in uptrend
            }

            // Volume confirmation
            if (momConfig_.requireVolumeConfirm && ind.avgVolume > 0) {
                if (static_cast<double>(tick.lastTradeQuantity) < 
                    ind.avgVolume * momConfig_.volumeMultiple) {
                    return false;
                }
            }

            return true;
        }

        /**
         * Calculate position size based on momentum strength
         */
        uint64_t calculatePositionSize(double momentum) const {
            double absMotmn = std::abs(momentum);
            
            // Base quantity
            uint64_t qty = momConfig_.baseQuantity;

            // Scale with momentum
            if (absMotmn >= momConfig_.strongSignal) {
                qty = static_cast<uint64_t>(
                    qty * momConfig_.momentumScale * 
                    (absMotmn / momConfig_.entryThreshold));
            }

            // Cap at max position units
            uint64_t maxQty = momConfig_.baseQuantity * momConfig_.maxPositionUnits;
            qty = std::min(qty, maxQty);

            // Respect max order quantity
            qty = std::min(qty, config_.maxOrderQuantity);

            return qty;
        }

        /**
         * Calculate signal confidence
         */
        double calculateConfidence(const MomentumIndicators& ind) const {
            double confidence = 0.5;

            // Higher confidence with stronger momentum
            double momStrength = std::abs(ind.momentum) / momConfig_.strongSignal;
            confidence += std::min(0.2, momStrength * 0.2);

            // Higher confidence when trend aligns
            if ((ind.momentum > 0 && ind.trendUp) || 
                (ind.momentum < 0 && ind.trendDown)) {
                confidence += 0.15;
            }

            // Higher confidence with clear MACD signal
            if (std::abs(ind.histogram) > 0.01) {
                confidence += 0.1;
            }

            // Lower confidence at RSI extremes
            if (ind.rsi > 80 || ind.rsi < 20) {
                confidence -= 0.1;
            }

            return std::clamp(confidence, 0.1, 0.95);
        }

        /**
         * Format reason string with indicator values
         */
        std::string formatReason(const std::string& action, 
                                  const MomentumIndicators& ind) const {
            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                "%s: Mom=%.2f%%, MACD=%.4f, RSI=%.1f",
                action.c_str(),
                ind.momentum * 100.0,
                ind.macd,
                ind.rsi);
            return std::string(buffer);
        }
    };

}
