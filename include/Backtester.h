/**
 * @file Backtester.h
 * @brief Backtesting framework for validating trading strategies with simulated order flow
 * 
 * This file provides a comprehensive backtesting system that:
 * - Simulates realistic market conditions and order flow
 * - Runs multiple strategies in parallel
 * - Tracks detailed P&L and performance metrics
 * - Generates comprehensive reports and CSV output
 */

#pragma once

#include "Strategy.h"
#include "MarketMakingStrategy.h"
#include "MomentumStrategy.h"
#include "MatchingEngine.h"
#include "RiskManager.h"
#include "PnLTracker.h"
#include "StrategyManager.h"
#include <vector>
#include <memory>
#include <string>
#include <fstream>
#include <random>
#include <functional>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace Mercury {

    /**
     * Order Flow Pattern - simulates different market regimes
     */
    enum class OrderFlowPattern {
        Random,         // Random buy/sell with normal distribution
        Trending,       // Directional bias (trend)
        MeanReverting,  // Price oscillates around mean
        HighVolatility, // Large random price swings
        LowVolatility,  // Tight range-bound
        MomentumBurst,  // Sudden strong directional move
        Choppy          // Frequent reversals
    };

    /**
     * Order Flow Configuration
     */
    struct OrderFlowConfig {
        OrderFlowPattern pattern = OrderFlowPattern::Random;
        
        // Base parameters
        int64_t startPrice = 100;           // Starting price
        uint64_t ordersPerTick = 5;         // Orders to generate per tick
        double volatility = 0.02;           // Price volatility (2%)
        
        // Order sizing
        uint64_t minOrderSize = 10;         // Min order quantity
        uint64_t maxOrderSize = 200;        // Max order quantity
        double marketOrderRatio = 0.3;      // % of market orders (vs limit)
        
        // Pattern-specific
        double trendStrength = 0.001;       // Trend drift per tick (0.1%)
        double meanReversionSpeed = 0.05;   // Mean reversion force
        double burstProbability = 0.05;     // Probability of momentum burst
        double reversalProbability = 0.1;   // Probability of price reversal
        
        // Spread
        int64_t minSpread = 2;              // Min bid-ask spread
        int64_t maxSpread = 10;             // Max spread
        
        // Price bounds (prevent extreme drift)
        double minPricePct = 0.5;           // Min price as % of start (50%)
        double maxPricePct = 2.0;           // Max price as % of start (200%)
        
        // Multiple clients
        uint64_t numClients = 10;           // Number of external clients
        uint64_t clientIdStart = 9000;      // Starting client ID for external clients
        
        // Randomness
        uint32_t seed = 42;                 // RNG seed (0 = random)
    };

    /**
     * Backtest Configuration
     */
    struct BacktestConfig {
        // Time parameters
        uint64_t numTicks = 1000;           // Number of time steps
        uint64_t tickDurationMs = 100;      // Simulated time per tick (ms)
        uint64_t warmupTicks = 50;          // Warmup period before tracking metrics
        
        // Order flow
        OrderFlowConfig orderFlow;
        
        // Risk limits for all strategies
        RiskLimits riskLimits;
        
        // Output
        std::string outputDir = "backtest_results";
        bool writeTradeLog = true;
        bool writePnLLog = true;
        bool writeOrderLog = true;
        bool writeMetricsLog = true;
        bool verbose = false;
        
        BacktestConfig() {
            // Default risk limits
            riskLimits.maxPositionQuantity = 100000;
            riskLimits.maxGrossExposure = 10000000;
            riskLimits.maxNetExposure = 5000000;
            riskLimits.maxDailyLoss = -1000000;
            riskLimits.maxOrderValue = 1000000;
            riskLimits.maxOrderQuantity = 10000;
            riskLimits.maxOpenOrders = 1000;
        }
    };

    /**
     * Backtest Performance Metrics
     */
    struct BacktestMetrics {
        std::string strategyName;
        
        // P&L metrics
        int64_t totalPnL = 0;
        int64_t realizedPnL = 0;
        int64_t unrealizedPnL = 0;
        int64_t maxDrawdown = 0;
        int64_t peakPnL = 0;
        
        // Trade metrics
        uint64_t totalTrades = 0;
        uint64_t winningTrades = 0;
        uint64_t losingTrades = 0;
        uint64_t totalVolume = 0;
        double avgTradeSize = 0.0;
        double winRate = 0.0;
        
        // Position metrics
        int64_t maxPosition = 0;
        int64_t finalPosition = 0;
        double avgPosition = 0.0;
        
        // Order metrics
        uint64_t ordersSubmitted = 0;
        uint64_t ordersFilled = 0;
        uint64_t ordersRejected = 0;
        double fillRate = 0.0;
        
        // Risk metrics
        int64_t maxLoss = 0;
        double sharpeRatio = 0.0;
        double sortinoRatio = 0.0;
        double profitFactor = 0.0;
        
        // Timing
        uint64_t startTime = 0;
        uint64_t endTime = 0;
        uint64_t duration = 0;
        
        void calculate() {
            if (totalTrades > 0) {
                avgTradeSize = static_cast<double>(totalVolume) / totalTrades;
                winRate = static_cast<double>(winningTrades) / totalTrades;
            }
            if (ordersSubmitted > 0) {
                fillRate = static_cast<double>(ordersFilled) / ordersSubmitted;
            }
            duration = endTime - startTime;
        }
    };

    /**
     * Backtest Report - complete results
     */
    struct BacktestReport {
        BacktestConfig config;
        std::vector<BacktestMetrics> strategyMetrics;
        
        // Overall metrics
        uint64_t totalTicks = 0;
        uint64_t totalTrades = 0;
        uint64_t totalVolume = 0;
        int64_t totalPnL = 0;
        
        // Market statistics
        int64_t startPrice = 0;
        int64_t endPrice = 0;
        int64_t minPrice = 0;
        int64_t maxPrice = 0;
        double avgSpread = 0.0;
        
        // Timing
        double backtestDurationMs = 0.0;
        double throughputTicksPerSec = 0.0;
        
        void calculate() {
            if (totalTicks > 0 && backtestDurationMs > 0) {
                throughputTicksPerSec = (totalTicks * 1000.0) / backtestDurationMs;
            }
        }
    };

    /**
     * Order Flow Simulator - generates realistic order flow
     */
    class OrderFlowSimulator {
    public:
        explicit OrderFlowSimulator(const OrderFlowConfig& config)
            : config_(config)
            , currentPrice_(config.startPrice)
            , meanPrice_(config.startPrice)
            , rng_(config.seed == 0 ? std::random_device{}() : config.seed)
            , trendDirection_(1) {}

        /**
         * Generate orders for the current tick
         * @param tick Current time step
         * @param externalClientId Client ID to use for generated orders
         * @return Vector of simulated orders
         */
        std::vector<Order> generateOrders(uint64_t tick, uint64_t externalClientId = 0) {
            std::vector<Order> orders;
            
            // Update price based on pattern
            updatePrice(tick);
            
            // Generate the configured number of orders
            for (uint64_t i = 0; i < config_.ordersPerTick; ++i) {
                // Use specified client ID, or distribute among multiple clients
                uint64_t clientId = externalClientId;
                if (clientId == 0 && config_.numClients > 0) {
                    // Randomly select from pool of external clients
                    std::uniform_int_distribution<uint64_t> clientDist(0, config_.numClients - 1);
                    clientId = config_.clientIdStart + clientDist(rng_);
                } else if (clientId == 0) {
                    clientId = 9999;  // Default fallback
                }
                Order order = generateOrder(tick, clientId);
                orders.push_back(order);
            }
            
            return orders;
        }

        /**
         * Get current market state as a MarketTick
         */
        MarketTick getCurrentTick(uint64_t timestamp) const {
            MarketTick tick;
            tick.timestamp = timestamp;
            
            int64_t halfSpread = (config_.minSpread + config_.maxSpread) / 2;
            tick.bidPrice = currentPrice_ - halfSpread;
            tick.askPrice = currentPrice_ + halfSpread;
            tick.bidQuantity = 100;
            tick.askQuantity = 100;
            tick.lastTradePrice = currentPrice_;
            tick.lastTradeQuantity = 50;
            
            return tick;
        }

        int64_t getCurrentPrice() const { return currentPrice_; }
        int64_t getMeanPrice() const { return meanPrice_; }

    private:
        OrderFlowConfig config_;
        int64_t currentPrice_;
        int64_t meanPrice_;
        std::mt19937 rng_;
        int trendDirection_;
        uint64_t nextOrderId_ = 100000;
        
        std::normal_distribution<double> normalDist_{0.0, 1.0};
        std::uniform_real_distribution<double> uniformDist_{0.0, 1.0};

        void updatePrice(uint64_t tick) {
            switch (config_.pattern) {
                case OrderFlowPattern::Random:
                    updatePriceRandom();
                    break;
                case OrderFlowPattern::Trending:
                    updatePriceTrending();
                    break;
                case OrderFlowPattern::MeanReverting:
                    updatePriceMeanReverting();
                    break;
                case OrderFlowPattern::HighVolatility:
                    updatePriceHighVolatility();
                    break;
                case OrderFlowPattern::LowVolatility:
                    updatePriceLowVolatility();
                    break;
                case OrderFlowPattern::MomentumBurst:
                    updatePriceMomentumBurst(tick);
                    break;
                case OrderFlowPattern::Choppy:
                    updatePriceChoppy();
                    break;
            }
            
            // Enforce price bounds to prevent extreme drift
            int64_t minPrice = static_cast<int64_t>(config_.startPrice * config_.minPricePct);
            int64_t maxPrice = static_cast<int64_t>(config_.startPrice * config_.maxPricePct);
            currentPrice_ = std::max(currentPrice_, std::max(minPrice, int64_t(1)));
            currentPrice_ = std::min(currentPrice_, maxPrice);
        }

        void updatePriceRandom() {
            double change = normalDist_(rng_) * config_.volatility;
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + change));
        }

        void updatePriceTrending() {
            double drift = config_.trendStrength * trendDirection_;
            double noise = normalDist_(rng_) * config_.volatility * 0.5;
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + drift + noise));
            
            // Occasionally reverse trend
            if (uniformDist_(rng_) < 0.02) {
                trendDirection_ *= -1;
            }
        }

        void updatePriceMeanReverting() {
            double deviation = static_cast<double>(currentPrice_ - meanPrice_) / meanPrice_;
            double reversion = -deviation * config_.meanReversionSpeed;
            double noise = normalDist_(rng_) * config_.volatility * 0.3;
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + reversion + noise));
        }

        void updatePriceHighVolatility() {
            double change = normalDist_(rng_) * config_.volatility * 3.0;
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + change));
        }

        void updatePriceLowVolatility() {
            double change = normalDist_(rng_) * config_.volatility * 0.2;
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + change));
        }

        void updatePriceMomentumBurst(uint64_t tick) {
            // Random walks with occasional bursts
            if (uniformDist_(rng_) < config_.burstProbability) {
                // Burst!
                double burstSize = (uniformDist_(rng_) * 0.1 + 0.05) * trendDirection_;
                currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + burstSize));
            } else {
                updatePriceRandom();
            }
        }

        void updatePriceChoppy() {
            // Frequent reversals
            if (uniformDist_(rng_) < config_.reversalProbability) {
                trendDirection_ *= -1;
            }
            double drift = config_.trendStrength * trendDirection_ * 3.0;
            double noise = normalDist_(rng_) * config_.volatility;
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + drift + noise));
        }

        Order generateOrder(uint64_t tick, uint64_t clientId) {
            Order order;
            order.id = nextOrderId_++;
            order.clientId = clientId;
            order.timestamp = tick;
            
            // Determine order type
            bool isMarketOrder = uniformDist_(rng_) < config_.marketOrderRatio;
            order.orderType = isMarketOrder ? OrderType::Market : OrderType::Limit;
            order.tif = isMarketOrder ? TimeInForce::IOC : TimeInForce::GTC;
            
            // Determine side (50/50 with slight bias based on pattern)
            double sideBias = calculateSideBias();
            order.side = (uniformDist_(rng_) < 0.5 + sideBias) ? Side::Buy : Side::Sell;
            
            // Determine quantity
            std::uniform_int_distribution<uint64_t> qtyDist(config_.minOrderSize, config_.maxOrderSize);
            order.quantity = qtyDist(rng_);
            
            // Determine price (for limit orders)
            if (!isMarketOrder) {
                int64_t spread = config_.minSpread + 
                    static_cast<int64_t>(uniformDist_(rng_) * (config_.maxSpread - config_.minSpread));
                
                if (order.side == Side::Buy) {
                    // Buy limit slightly below current price
                    order.price = currentPrice_ - spread / 2;
                } else {
                    // Sell limit slightly above current price
                    order.price = currentPrice_ + spread / 2;
                }
                order.price = std::max(order.price, int64_t(1));
            } else {
                order.price = 0;  // Market order
            }
            
            return order;
        }

        double calculateSideBias() const {
            // Add directional bias based on pattern
            switch (config_.pattern) {
                case OrderFlowPattern::Trending:
                    return config_.trendStrength * trendDirection_ * 10.0;
                case OrderFlowPattern::MeanReverting:
                    // Bias against deviation
                    return -((currentPrice_ - meanPrice_) / static_cast<double>(meanPrice_)) * 0.2;
                default:
                    return 0.0;
            }
        }
    };

    /**
     * Backtester - main backtesting engine
     */
    class Backtester {
    public:
        explicit Backtester(const BacktestConfig& config)
            : config_(config)
            , engine_(std::make_unique<MatchingEngine>())
            , riskManager_(std::make_unique<RiskManager>(config.riskLimits))
            , pnlTracker_(std::make_unique<PnLTracker>())
            , strategyManager_(std::make_unique<StrategyManager>(*engine_, *riskManager_, *pnlTracker_))
            , orderFlowSim_(std::make_unique<OrderFlowSimulator>(config.orderFlow)) {
            
            setupOutputFiles();
        }

        /**
         * Add a strategy to backtest
         */
        void addStrategy(std::unique_ptr<Strategy> strategy) {
            std::string name = strategy->getName();
            strategyNames_.push_back(name);
            strategyManager_->addStrategy(std::move(strategy));
        }

        /**
         * Run the backtest
         * @return Complete backtest report
         */
        BacktestReport run() {
            if (config_.verbose) {
                std::cout << "========================================\n";
                std::cout << "         Backtesting Started\n";
                std::cout << "========================================\n";
                std::cout << "Strategies: " << strategyNames_.size() << "\n";
                std::cout << "Ticks: " << config_.numTicks << "\n";
                std::cout << "Pattern: " << orderFlowPatternToString(config_.orderFlow.pattern) << "\n";
                std::cout << "========================================\n\n";
            }

            auto startTime = std::chrono::high_resolution_clock::now();
            
            // Initialize report
            BacktestReport report;
            report.config = config_;
            report.startPrice = config_.orderFlow.startPrice;
            
            // Open P&L tracker
            if (config_.writePnLLog) {
                pnlTracker_->open();
            }

            // Run backtest ticks
            for (uint64_t tick = 1; tick <= config_.numTicks; ++tick) {
                runTick(tick);
                
                if (config_.verbose && tick % 100 == 0) {
                    std::cout << "Progress: " << tick << "/" << config_.numTicks << " ticks\n";
                }
            }

            // Close output files
            closeOutputFiles();

            // Calculate results
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            report.totalTicks = config_.numTicks;
            report.totalTrades = engine_->getTradeCount();
            report.totalVolume = engine_->getTotalVolume();
            report.endPrice = orderFlowSim_->getCurrentPrice();
            report.minPrice = minPrice_;
            report.maxPrice = maxPrice_;
            report.backtestDurationMs = duration.count();
            
            // Collect strategy metrics
            for (const auto& name : strategyNames_) {
                BacktestMetrics metrics = calculateMetrics(name);
                report.strategyMetrics.push_back(metrics);
                report.totalPnL += metrics.totalPnL;
            }
            
            report.calculate();
            
            if (config_.verbose) {
                printReport(report);
            }
            
            return report;
        }

        /**
         * Write report to file
         */
        bool writeReport(const BacktestReport& report, const std::string& filename) {
            std::ofstream file(filename);
            if (!file.is_open()) return false;
            
            // Header
            file << "=== Backtest Report ===\n\n";
            file << "Configuration:\n";
            file << "  Ticks: " << report.config.numTicks << "\n";
            file << "  Pattern: " << orderFlowPatternToString(report.config.orderFlow.pattern) << "\n";
            file << "  Volatility: " << (report.config.orderFlow.volatility * 100) << "%\n";
            file << "  Orders/Tick: " << report.config.orderFlow.ordersPerTick << "\n\n";
            
            // Market statistics
            file << "Market Statistics:\n";
            file << "  Start Price: " << report.startPrice << "\n";
            file << "  End Price: " << report.endPrice << "\n";
            file << "  Min Price: " << report.minPrice << "\n";
            file << "  Max Price: " << report.maxPrice << "\n";
            file << "  Total Trades: " << report.totalTrades << "\n";
            file << "  Total Volume: " << report.totalVolume << "\n\n";
            
            // Performance
            file << "Performance:\n";
            file << "  Duration: " << report.backtestDurationMs << " ms\n";
            file << "  Throughput: " << std::fixed << std::setprecision(2) 
                 << report.throughputTicksPerSec << " ticks/sec\n\n";
            
            // Strategy results
            for (const auto& metrics : report.strategyMetrics) {
                file << "Strategy: " << metrics.strategyName << "\n";
                file << "  Total P&L: " << metrics.totalPnL << "\n";
                file << "  Realized P&L: " << metrics.realizedPnL << "\n";
                file << "  Unrealized P&L: " << metrics.unrealizedPnL << "\n";
                file << "  Max Drawdown: " << metrics.maxDrawdown << "\n";
                file << "  Total Trades: " << metrics.totalTrades << "\n";
                file << "  Win Rate: " << std::fixed << std::setprecision(2) 
                     << (metrics.winRate * 100) << "%\n";
                file << "  Fill Rate: " << std::fixed << std::setprecision(2) 
                     << (metrics.fillRate * 100) << "%\n";
                file << "  Final Position: " << metrics.finalPosition << "\n";
                file << "  Max Position: " << metrics.maxPosition << "\n\n";
            }
            
            file.close();
            return true;
        }

    private:
        BacktestConfig config_;
        std::unique_ptr<MatchingEngine> engine_;
        std::unique_ptr<RiskManager> riskManager_;
        std::unique_ptr<PnLTracker> pnlTracker_;
        std::unique_ptr<StrategyManager> strategyManager_;
        std::unique_ptr<OrderFlowSimulator> orderFlowSim_;
        
        std::vector<std::string> strategyNames_;
        std::ofstream tradeLogFile_;
        std::ofstream orderLogFile_;
        
        int64_t minPrice_ = std::numeric_limits<int64_t>::max();
        int64_t maxPrice_ = std::numeric_limits<int64_t>::min();

        void setupOutputFiles() {
            if (config_.writeTradeLog) {
                tradeLogFile_.open(config_.outputDir + "/backtest_trades.csv");
                if (tradeLogFile_.is_open()) {
                    tradeLogFile_ << "trade_id,timestamp,buy_order_id,sell_order_id,price,quantity\n";
                }
            }
            
            if (config_.writeOrderLog) {
                orderLogFile_.open(config_.outputDir + "/backtest_orders.csv");
                if (orderLogFile_.is_open()) {
                    orderLogFile_ << "order_id,timestamp,type,side,price,quantity,status,filled_qty\n";
                }
            }
            
            // Set up trade callback for logging trades to file
            if (config_.writeTradeLog && tradeLogFile_.is_open()) {
                engine_->setTradeCallback([this](const Trade& trade) {
                    if (tradeLogFile_.is_open()) {
                        tradeLogFile_ << trade.tradeId << ","
                                     << trade.timestamp << ","
                                     << trade.buyOrderId << ","
                                     << trade.sellOrderId << ","
                                     << trade.price << ","
                                     << trade.quantity << "\n";
                    }
                });
            }
        }

        void closeOutputFiles() {
            if (tradeLogFile_.is_open()) {
                tradeLogFile_.close();
            }
            if (orderLogFile_.is_open()) {
                orderLogFile_.close();
            }
            if (pnlTracker_->isOpen()) {
                pnlTracker_->close();
            }
        }

        void runTick(uint64_t tick) {
            // Generate market tick
            MarketTick marketTick = orderFlowSim_->getCurrentTick(tick);
            
            // Track price range
            int64_t currentPrice = marketTick.midPrice();
            minPrice_ = std::min(minPrice_, currentPrice);
            maxPrice_ = std::max(maxPrice_, currentPrice);
            
            // Feed to strategies
            strategyManager_->onMarketTick(marketTick);
            
            // Generate and submit external order flow
            auto orders = orderFlowSim_->generateOrders(tick);
            for (const auto& order : orders) {
                auto result = engine_->submitOrder(order);
                
                if (config_.writeOrderLog && orderLogFile_.is_open()) {
                    const char* typeStr = order.orderType == OrderType::Market ? "market" : "limit";
                    const char* sideStr = order.side == Side::Buy ? "buy" : "sell";
                    const char* statusStr = result.status == ExecutionStatus::Filled ? "filled" :
                                          result.status == ExecutionStatus::PartialFill ? "partial" :
                                          result.status == ExecutionStatus::Resting ? "resting" : "other";
                    
                    orderLogFile_ << order.id << ","
                                 << tick << ","
                                 << typeStr << ","
                                 << sideStr << ","
                                 << order.price << ","
                                 << order.quantity << ","
                                 << statusStr << ","
                                 << result.filledQuantity << "\n";
                }
            }
        }

        BacktestMetrics calculateMetrics(const std::string& strategyName) {
            BacktestMetrics metrics;
            metrics.strategyName = strategyName;
            
            // Get strategy manager metrics
            auto smMetrics = strategyManager_->getMetrics(strategyName);
            metrics.totalTrades = smMetrics.totalTrades;
            metrics.totalVolume = smMetrics.totalVolume;
            metrics.ordersSubmitted = smMetrics.ordersSubmitted;
            metrics.ordersFilled = smMetrics.ordersFilled;
            metrics.ordersRejected = smMetrics.ordersRejected;
            metrics.finalPosition = smMetrics.netPosition;
            metrics.maxPosition = smMetrics.maxPosition;
            
            // Get P&L from tracker
            if (auto* strategy = strategyManager_->getStrategy(strategyName)) {
                uint64_t clientId = strategy->getConfig().clientId;
                auto pnl = pnlTracker_->getClientPnL(clientId);
                
                metrics.realizedPnL = pnl.realizedPnL;
                metrics.unrealizedPnL = pnl.unrealizedPnL;
                metrics.totalPnL = pnl.totalPnL;
                
                // Get actual win/loss counts from PnLTracker
                metrics.winningTrades = pnl.winningTrades;
                metrics.losingTrades = pnl.losingTrades;
            }
            
            metrics.calculate();
            return metrics;
        }

        void printReport(const BacktestReport& report) {
            std::cout << "\n========================================\n";
            std::cout << "       Backtest Results\n";
            std::cout << "========================================\n\n";
            
            std::cout << "Market Statistics:\n";
            std::cout << "  Price Range: " << report.minPrice << " - " << report.maxPrice << "\n";
            std::cout << "  Price Change: " << (report.endPrice - report.startPrice) 
                      << " (" << std::fixed << std::setprecision(2)
                      << (100.0 * (report.endPrice - report.startPrice) / report.startPrice) << "%)\n";
            std::cout << "  Total Trades: " << report.totalTrades << "\n";
            std::cout << "  Total Volume: " << report.totalVolume << "\n\n";
            
            std::cout << "Performance:\n";
            std::cout << "  Duration: " << report.backtestDurationMs << " ms\n";
            std::cout << "  Throughput: " << report.throughputTicksPerSec << " ticks/sec\n\n";
            
            std::cout << "Strategy Results:\n";
            for (const auto& metrics : report.strategyMetrics) {
                std::cout << "\n  " << metrics.strategyName << ":\n";
                std::cout << "    P&L: " << metrics.totalPnL 
                          << " (Realized: " << metrics.realizedPnL 
                          << ", Unrealized: " << metrics.unrealizedPnL << ")\n";
                std::cout << "    Trades: " << metrics.totalTrades 
                          << " (Win rate: " << (metrics.winRate * 100) << "%)\n";
                std::cout << "    Volume: " << metrics.totalVolume << "\n";
                std::cout << "    Position: " << metrics.finalPosition 
                          << " (Max: " << metrics.maxPosition << ")\n";
                std::cout << "    Fill Rate: " << (metrics.fillRate * 100) << "%\n";
            }
            
            std::cout << "\n========================================\n";
        }

        static std::string orderFlowPatternToString(OrderFlowPattern pattern) {
            switch (pattern) {
                case OrderFlowPattern::Random: return "Random";
                case OrderFlowPattern::Trending: return "Trending";
                case OrderFlowPattern::MeanReverting: return "MeanReverting";
                case OrderFlowPattern::HighVolatility: return "HighVolatility";
                case OrderFlowPattern::LowVolatility: return "LowVolatility";
                case OrderFlowPattern::MomentumBurst: return "MomentumBurst";
                case OrderFlowPattern::Choppy: return "Choppy";
                default: return "Unknown";
            }
        }
    };

}
