/**
 * @file StrategyDemo.h
 * @brief Demo functions showcasing the strategy layer
 * 
 * This file provides demonstration functions for:
 * - Market Making Strategy
 * - Momentum Strategy  
 * - Strategy Manager integration
 */

#pragma once

#include "MatchingEngine.h"
#include "RiskManager.h"
#include "PnLTracker.h"
#include "Strategy.h"
#include "MarketMakingStrategy.h"
#include "MomentumStrategy.h"
#include "StrategyManager.h"
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <random>

namespace Mercury {

    /**
     * Simulated market data generator
     */
    class MarketSimulator {
    public:
        MarketSimulator(int64_t startPrice = 100, double volatility = 0.02)
            : currentPrice_(startPrice)
            , volatility_(volatility)
            , rng_(std::random_device{}()) {}

        /**
         * Generate next price with random walk
         */
        int64_t nextPrice() {
            std::normal_distribution<double> dist(0.0, volatility_);
            double change = dist(rng_);
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + change));
            currentPrice_ = std::max(currentPrice_, int64_t(1));  // Floor at 1
            return currentPrice_;
        }

        /**
         * Generate a trending price movement
         */
        int64_t nextTrendingPrice(bool uptrend) {
            double trend = uptrend ? 0.001 : -0.001;
            std::normal_distribution<double> dist(trend, volatility_ / 2);
            double change = dist(rng_);
            currentPrice_ = static_cast<int64_t>(currentPrice_ * (1.0 + change));
            currentPrice_ = std::max(currentPrice_, int64_t(1));
            return currentPrice_;
        }

        int64_t getCurrentPrice() const { return currentPrice_; }
        void setPrice(int64_t price) { currentPrice_ = price; }

    private:
        int64_t currentPrice_;
        double volatility_;
        std::mt19937 rng_;
    };

    /**
     * Demo: Market Making Strategy
     * 
     * Shows a market maker providing liquidity by quoting bid and ask
     */
    inline void runMarketMakingDemo() {
        std::cout << "\n========================================\n";
        std::cout << "   Market Making Strategy Demo\n";
        std::cout << "========================================\n\n";

        // Create matching engine and risk manager
        MatchingEngine engine;
        RiskManager riskManager;
        PnLTracker pnlTracker;

        // Set up strategy manager
        StrategyManager manager(engine, riskManager, pnlTracker);
        
        // Configure market making strategy
        MarketMakingConfig mmConfig;
        mmConfig.name = "MM-Demo";
        mmConfig.minSpread = 2;
        mmConfig.maxSpread = 8;
        mmConfig.quoteQuantity = 100;
        mmConfig.maxInventory = 500;
        mmConfig.inventorySkew = 0.1;
        mmConfig.fadeWhenFilled = true;
        mmConfig.clientId = 1;

        auto mmStrategy = std::make_unique<MarketMakingStrategy>(mmConfig);
        manager.addStrategy(std::move(mmStrategy));

        // Configure logging
        StrategyManagerConfig mgrConfig;
        mgrConfig.logSignals = true;
        mgrConfig.logExecutions = true;
        manager.setConfig(mgrConfig);

        // Simulate market data
        MarketSimulator sim(100);
        
        std::cout << "--- Initial Order Book ---\n";
        std::cout << "(Empty - market maker will provide liquidity)\n\n";

        std::cout << "--- Simulating Market ---\n";
        for (int tick = 1; tick <= 10; tick++) {
            int64_t price = sim.nextPrice();
            
            // Create market tick
            MarketTick marketTick;
            marketTick.timestamp = tick;
            marketTick.bidPrice = price - 2;
            marketTick.askPrice = price + 2;
            marketTick.bidQuantity = 50;
            marketTick.askQuantity = 50;
            marketTick.lastTradePrice = price;
            marketTick.lastTradeQuantity = 25;

            std::cout << "\nTick " << tick << ": Mid=" << price 
                      << " Bid=" << marketTick.bidPrice 
                      << " Ask=" << marketTick.askPrice << "\n";

            // Process tick through strategy manager
            manager.onMarketTick(marketTick);

            // Simulate some random order flow against our quotes
            if (tick % 3 == 0 && engine.getOrderBook().hasAsks()) {
                Order takerBuy;
                takerBuy.id = 10000 + tick;
                takerBuy.orderType = OrderType::Market;
                takerBuy.side = Side::Buy;
                takerBuy.quantity = 30;
                takerBuy.clientId = 99;  // External taker
                
                std::cout << "  [External] Market buy 30 units\n";
                auto result = engine.submitOrder(takerBuy);
                if (result.hasFills()) {
                    std::cout << "  >> Filled at " << result.trades[0].price << "\n";
                }
            }

            if (tick % 4 == 0 && engine.getOrderBook().hasBids()) {
                Order takerSell;
                takerSell.id = 20000 + tick;
                takerSell.orderType = OrderType::Market;
                takerSell.side = Side::Sell;
                takerSell.quantity = 25;
                takerSell.clientId = 99;
                
                std::cout << "  [External] Market sell 25 units\n";
                auto result = engine.submitOrder(takerSell);
                if (result.hasFills()) {
                    std::cout << "  >> Filled at " << result.trades[0].price << "\n";
                }
            }
        }

        // Print summary
        std::cout << "\n--- Market Making Results ---\n";
        manager.printSummary();

        std::cout << "\nFinal Order Book:\n";
        engine.getOrderBook().printBook();
    }

    /**
     * Demo: Momentum Strategy
     * 
     * Shows a trend-following strategy that trades based on price momentum
     */
    inline void runMomentumDemo() {
        std::cout << "\n========================================\n";
        std::cout << "   Momentum Strategy Demo\n";
        std::cout << "========================================\n\n";

        // Create matching engine
        MatchingEngine engine;
        RiskManager riskManager;

        // Seed order book with liquidity
        std::cout << "--- Seeding Order Book ---\n";
        for (int i = 0; i < 10; i++) {
            Order sellOrder;
            sellOrder.id = 100 + i;
            sellOrder.orderType = OrderType::Limit;
            sellOrder.side = Side::Sell;
            sellOrder.price = 105 + i * 2;
            sellOrder.quantity = 500;
            sellOrder.clientId = 50;  // Liquidity provider
            engine.submitOrder(sellOrder);

            Order buyOrder;
            buyOrder.id = 200 + i;
            buyOrder.orderType = OrderType::Limit;
            buyOrder.side = Side::Buy;
            buyOrder.price = 95 - i * 2;
            buyOrder.quantity = 500;
            buyOrder.clientId = 50;
            engine.submitOrder(buyOrder);
        }
        engine.getOrderBook().printBook();

        // Set up strategy manager
        StrategyManager manager(engine, riskManager);

        // Configure momentum strategy
        MomentumConfig momConfig;
        momConfig.name = "MOM-Demo";
        momConfig.shortPeriod = 5;
        momConfig.longPeriod = 15;
        momConfig.entryThreshold = 0.015;
        momConfig.exitThreshold = 0.005;
        momConfig.baseQuantity = 50;
        momConfig.stopLossPct = 0.03;
        momConfig.takeProfitPct = 0.05;
        momConfig.confirmationBars = 2;
        momConfig.requireVolumeConfirm = false;
        momConfig.useTrendFilter = true;
        momConfig.useMarketOrders = true;
        momConfig.clientId = 2;

        auto momStrategy = std::make_unique<MomentumStrategy>(momConfig);
        manager.addStrategy(std::move(momStrategy));

        // Configure logging
        StrategyManagerConfig mgrConfig;
        mgrConfig.logSignals = true;
        mgrConfig.logExecutions = true;
        manager.setConfig(mgrConfig);

        // Simulate trending market
        std::cout << "\n--- Phase 1: Building Price History ---\n";
        MarketSimulator sim(100);
        
        // First, build up price history with slight uptrend
        for (int tick = 1; tick <= 20; tick++) {
            int64_t price = sim.nextTrendingPrice(true);
            
            MarketTick marketTick;
            marketTick.timestamp = tick;
            marketTick.bidPrice = price - 2;
            marketTick.askPrice = price + 2;
            marketTick.bidQuantity = 100;
            marketTick.askQuantity = 100;
            marketTick.lastTradePrice = price;
            marketTick.lastTradeQuantity = 50;

            if (tick % 5 == 0) {
                std::cout << "Tick " << tick << ": Price=" << price << "\n";
            }

            manager.onMarketTick(marketTick);
        }

        std::cout << "\n--- Phase 2: Strong Uptrend (Momentum Entry) ---\n";
        for (int tick = 21; tick <= 35; tick++) {
            // Simulate stronger uptrend
            int64_t price = sim.nextTrendingPrice(true);
            price += (tick - 20) * 2;  // Extra boost

            MarketTick marketTick;
            marketTick.timestamp = tick;
            marketTick.bidPrice = price - 2;
            marketTick.askPrice = price + 2;
            marketTick.bidQuantity = 150;
            marketTick.askQuantity = 100;
            marketTick.lastTradePrice = price;
            marketTick.lastTradeQuantity = 100;  // Higher volume

            std::cout << "Tick " << tick << ": Price=" << price << "\n";
            manager.onMarketTick(marketTick);
        }

        std::cout << "\n--- Phase 3: Momentum Reversal (Exit Signal) ---\n";
        for (int tick = 36; tick <= 45; tick++) {
            // Simulate reversal
            int64_t price = sim.nextTrendingPrice(false);
            price -= (tick - 35) * 3;  // Sharp drop

            MarketTick marketTick;
            marketTick.timestamp = tick;
            marketTick.bidPrice = price - 3;
            marketTick.askPrice = price + 3;
            marketTick.bidQuantity = 80;
            marketTick.askQuantity = 150;
            marketTick.lastTradePrice = price;
            marketTick.lastTradeQuantity = 75;

            std::cout << "Tick " << tick << ": Price=" << price << "\n";
            manager.onMarketTick(marketTick);
        }

        // Print summary
        std::cout << "\n--- Momentum Strategy Results ---\n";
        manager.printSummary();
    }

    /**
     * Demo: Combined Strategies
     * 
     * Shows multiple strategies running together
     */
    inline void runCombinedStrategiesDemo() {
        std::cout << "\n========================================\n";
        std::cout << "   Combined Strategies Demo\n";
        std::cout << "========================================\n\n";

        // Create matching engine with all components
        MatchingEngine engine;
        RiskManager riskManager;
        PnLTracker pnlTracker;

        // Set up strategy manager with all dependencies
        StrategyManager manager(engine, riskManager, pnlTracker);

        // Add market making strategy
        MarketMakingConfig mmConfig;
        mmConfig.name = "MarketMaking";
        mmConfig.quoteQuantity = 50;
        mmConfig.maxInventory = 300;
        mmConfig.minSpread = 2;
        mmConfig.maxSpread = 6;
        mmConfig.clientId = 1;
        manager.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

        // Add momentum strategy
        MomentumConfig momConfig;
        momConfig.name = "Momentum";
        momConfig.baseQuantity = 30;
        momConfig.shortPeriod = 3;
        momConfig.longPeriod = 8;
        momConfig.entryThreshold = 0.01;       // Lower threshold (1%)
        momConfig.exitThreshold = 0.003;
        momConfig.confirmationBars = 1;        // Faster confirmation
        momConfig.requireVolumeConfirm = false;
        momConfig.useTrendFilter = false;      // Disable trend filter
        momConfig.useMarketOrders = true;
        momConfig.clientId = 2;
        manager.addStrategy(std::make_unique<MomentumStrategy>(momConfig));

        std::cout << "Strategies registered: " << manager.getStrategyCount() << "\n";
        std::cout << " - MarketMaking (Client 1)\n";
        std::cout << " - Momentum (Client 2)\n\n";

        // Simulate market with multiple phases
        MarketSimulator sim(100);
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> orderDist(1, 100);
        uint64_t externalOrderId = 50000;
        
        std::cout << "--- Simulating 80 Market Ticks ---\n";
        
        bool uptrend = true;
        for (int tick = 1; tick <= 80; tick++) {
            // Switch trend every 20 ticks
            if (tick % 20 == 0) {
                uptrend = !uptrend;
                std::cout << "\n[Trend change: " << (uptrend ? "BULLISH" : "BEARISH") << "]\n";
            }

            int64_t price = sim.nextTrendingPrice(uptrend);
            
            MarketTick marketTick;
            marketTick.timestamp = tick;
            marketTick.bidPrice = price - 2;
            marketTick.askPrice = price + 2;
            marketTick.bidQuantity = 100;
            marketTick.askQuantity = 100;
            marketTick.lastTradePrice = price;
            marketTick.lastTradeQuantity = 40;

            if (tick % 10 == 0) {
                std::cout << "Tick " << tick << ": Price=" << price 
                          << " (Bid=" << marketTick.bidPrice 
                          << " Ask=" << marketTick.askPrice << ")\n";
            }

            manager.onMarketTick(marketTick);
            
            // Simulate external order flow hitting the market maker's quotes
            if (tick % 3 == 0 && engine.getOrderBook().hasAsks()) {
                Order takerBuy;
                takerBuy.id = externalOrderId++;
                takerBuy.orderType = OrderType::Market;
                takerBuy.side = Side::Buy;
                takerBuy.quantity = 20 + (orderDist(rng) % 30);
                takerBuy.clientId = 99;  // External taker
                
                auto result = engine.submitOrder(takerBuy);
                if (result.hasFills()) {
                    std::cout << "  [Taker] Buy " << result.filledQuantity 
                              << " @ " << result.trades[0].price << "\n";
                    // Update market tick with trade info
                    marketTick.lastTradePrice = result.trades[0].price;
                    marketTick.lastTradeQuantity = result.filledQuantity;
                }
            }
            
            if (tick % 4 == 0 && engine.getOrderBook().hasBids()) {
                Order takerSell;
                takerSell.id = externalOrderId++;
                takerSell.orderType = OrderType::Market;
                takerSell.side = Side::Sell;
                takerSell.quantity = 15 + (orderDist(rng) % 25);
                takerSell.clientId = 99;
                
                auto result = engine.submitOrder(takerSell);
                if (result.hasFills()) {
                    std::cout << "  [Taker] Sell " << result.filledQuantity 
                              << " @ " << result.trades[0].price << "\n";
                    marketTick.lastTradePrice = result.trades[0].price;
                    marketTick.lastTradeQuantity = result.filledQuantity;
                }
            }
        }

        // Print comprehensive summary
        std::cout << "\n";
        manager.printSummary();

        std::cout << "\n--- Individual Strategy Metrics ---\n";
        auto allMetrics = manager.getAllMetrics();
        for (const auto& m : allMetrics) {
            std::cout << "\n" << m.strategyName << ":\n";
            std::cout << "  Signals generated: " << m.signalsGenerated << "\n";
            std::cout << "  Orders submitted:  " << m.ordersSubmitted << "\n";
            std::cout << "  Orders filled:     " << m.ordersFilled << "\n";
            std::cout << "  Total trades:      " << m.totalTrades << "\n";
            std::cout << "  Total volume:      " << m.totalVolume << "\n";
            std::cout << "  Net position:      " << m.netPosition << "\n";
            std::cout << "  Max position:      " << m.maxPosition << "\n";
            std::cout << "  P&L:               " << m.totalPnL << "\n";
        }

        std::cout << "\n--- Final Order Book ---\n";
        engine.getOrderBook().printBook();

        std::cout << "\n--- Trading Statistics ---\n";
        std::cout << "Total Trades: " << engine.getTradeCount() << "\n";
        std::cout << "Total Volume: " << engine.getTotalVolume() << "\n";
    }

    /**
     * Run all strategy demos
     */
    inline void runAllStrategyDemos() {
        runMarketMakingDemo();
        std::cout << "\n\n";
        
        runMomentumDemo();
        std::cout << "\n\n";
        
        runCombinedStrategiesDemo();
    }

}
