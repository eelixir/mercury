/**
 * @file BacktestDemo.h
 * @brief Demo functions for backtesting trading strategies
 */

#pragma once

#include "Backtester.h"
#include "MarketMakingStrategy.h"
#include "MomentumStrategy.h"
#include <iostream>
#include <iomanip>

namespace Mercury {

    /**
     * Run a simple backtest with market making strategy
     */
    inline void runMarketMakingBacktest() {
        std::cout << "\n========================================\n";
        std::cout << "   Market Making Backtest\n";
        std::cout << "========================================\n\n";

        // Configure backtest
        BacktestConfig config;
        config.numTicks = 500;
        config.warmupTicks = 50;
        config.verbose = true;
        config.outputDir = "build";
        
        // Configure order flow - mean reverting (good for market making)
        config.orderFlow.pattern = OrderFlowPattern::MeanReverting;
        config.orderFlow.startPrice = 100;
        config.orderFlow.ordersPerTick = 8;
        config.orderFlow.volatility = 0.01;  // 1% volatility
        config.orderFlow.minOrderSize = 20;
        config.orderFlow.maxOrderSize = 100;
        config.orderFlow.marketOrderRatio = 0.4;  // 40% market orders
        config.orderFlow.meanReversionSpeed = 0.1;
        config.orderFlow.seed = 12345;

        // Create backtester
        Backtester backtester(config);

        // Configure market making strategy
        MarketMakingConfig mmConfig;
        mmConfig.name = "MarketMaker";
        mmConfig.minSpread = 2;
        mmConfig.maxSpread = 8;
        mmConfig.quoteQuantity = 50;
        mmConfig.maxInventory = 500;
        mmConfig.inventorySkew = 0.15;
        mmConfig.fadeWhenFilled = true;
        mmConfig.fadeDuration = 3000;
        mmConfig.requoteInterval = 500;
        
        backtester.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

        // Run backtest
        auto report = backtester.run();

        // Write detailed report
        backtester.writeReport(report, "build/market_making_backtest_report.txt");
        
        std::cout << "\nBacktest complete. Results saved to build/\n";
        std::cout << "  - market_making_backtest_report.txt\n";
        std::cout << "  - backtest_trades.csv\n";
        std::cout << "  - backtest_orders.csv\n";
        std::cout << "  - pnl.csv\n";
    }

    /**
     * Run a backtest with momentum strategy
     */
    inline void runMomentumBacktest() {
        std::cout << "\n========================================\n";
        std::cout << "   Momentum Strategy Backtest\n";
        std::cout << "========================================\n\n";

        // Configure backtest
        BacktestConfig config;
        config.numTicks = 1000;
        config.warmupTicks = 100;
        config.verbose = true;
        config.outputDir = "build";
        
        // Configure order flow - trending (good for momentum)
        config.orderFlow.pattern = OrderFlowPattern::Trending;
        config.orderFlow.startPrice = 100;
        config.orderFlow.ordersPerTick = 10;
        config.orderFlow.volatility = 0.015;  // 1.5% volatility
        config.orderFlow.minOrderSize = 30;
        config.orderFlow.maxOrderSize = 150;
        config.orderFlow.marketOrderRatio = 0.3;
        config.orderFlow.trendStrength = 0.002;  // 0.2% drift per tick
        config.orderFlow.seed = 67890;

        // Create backtester
        Backtester backtester(config);

        // Configure momentum strategy
        MomentumConfig momConfig;
        momConfig.name = "Momentum";
        momConfig.shortPeriod = 10;
        momConfig.longPeriod = 30;
        momConfig.entryThreshold = 0.015;      // 1.5% momentum to enter
        momConfig.exitThreshold = 0.005;       // 0.5% to exit
        momConfig.baseQuantity = 50;
        momConfig.stopLossPct = 0.03;          // 3% stop loss
        momConfig.takeProfitPct = 0.06;        // 6% take profit
        momConfig.confirmationBars = 3;
        momConfig.requireVolumeConfirm = false;
        momConfig.useTrendFilter = true;
        momConfig.useMarketOrders = true;
        
        backtester.addStrategy(std::make_unique<MomentumStrategy>(momConfig));

        // Run backtest
        auto report = backtester.run();

        // Write detailed report
        backtester.writeReport(report, "build/momentum_backtest_report.txt");
        
        std::cout << "\nBacktest complete. Results saved to build/\n";
    }

    /**
     * Run backtest with multiple strategies competing
     */
    inline void runMultiStrategyBacktest() {
        std::cout << "\n========================================\n";
        std::cout << "   Multi-Strategy Backtest\n";
        std::cout << "========================================\n\n";

        // Configure backtest
        BacktestConfig config;
        config.numTicks = 800;
        config.warmupTicks = 80;
        config.verbose = true;
        config.outputDir = "build";
        
        // Configure order flow - choppy market (challenging for both strategies)
        config.orderFlow.pattern = OrderFlowPattern::Choppy;
        config.orderFlow.startPrice = 100;
        config.orderFlow.ordersPerTick = 12;
        config.orderFlow.volatility = 0.02;
        config.orderFlow.minOrderSize = 20;
        config.orderFlow.maxOrderSize = 120;
        config.orderFlow.marketOrderRatio = 0.35;
        config.orderFlow.reversalProbability = 0.15;
        config.orderFlow.trendStrength = 0.003;
        config.orderFlow.seed = 11111;

        // Create backtester
        Backtester backtester(config);

        // Add market making strategy
        MarketMakingConfig mmConfig;
        mmConfig.name = "MM-Adaptive";
        mmConfig.minSpread = 3;
        mmConfig.maxSpread = 10;
        mmConfig.quoteQuantity = 40;
        mmConfig.maxInventory = 400;
        mmConfig.inventorySkew = 0.2;
        mmConfig.fadeWhenFilled = true;
        backtester.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

        // Add momentum strategy
        MomentumConfig momConfig;
        momConfig.name = "Momentum-Fast";
        momConfig.shortPeriod = 5;
        momConfig.longPeriod = 15;
        momConfig.entryThreshold = 0.02;
        momConfig.exitThreshold = 0.008;
        momConfig.baseQuantity = 30;
        momConfig.stopLossPct = 0.025;
        momConfig.takeProfitPct = 0.05;
        momConfig.confirmationBars = 2;
        momConfig.requireVolumeConfirm = false;
        momConfig.useTrendFilter = false;  // Disabled for choppy market
        momConfig.useMarketOrders = true;
        backtester.addStrategy(std::make_unique<MomentumStrategy>(momConfig));

        // Run backtest
        auto report = backtester.run();

        // Write detailed report
        backtester.writeReport(report, "build/multi_strategy_backtest_report.txt");
        
        // Print comparison
        std::cout << "\n========================================\n";
        std::cout << "        Strategy Comparison\n";
        std::cout << "========================================\n";
        
        for (const auto& metrics : report.strategyMetrics) {
            std::cout << "\n" << metrics.strategyName << ":\n";
            std::cout << "  Total P&L:    " << std::setw(10) << metrics.totalPnL << "\n";
            std::cout << "  Trades:       " << std::setw(10) << metrics.totalTrades << "\n";
            std::cout << "  Win Rate:     " << std::setw(9) << std::fixed 
                      << std::setprecision(1) << (metrics.winRate * 100) << "%\n";
            std::cout << "  Fill Rate:    " << std::setw(9) << std::fixed 
                      << std::setprecision(1) << (metrics.fillRate * 100) << "%\n";
            std::cout << "  Final Pos:    " << std::setw(10) << metrics.finalPosition << "\n";
        }
        std::cout << "========================================\n";
        
        std::cout << "\nBacktest complete. Results saved to build/\n";
    }

    /**
     * Run backtests with different market conditions
     */
    inline void runMarketConditionComparison() {
        std::cout << "\n========================================\n";
        std::cout << "   Market Condition Comparison\n";
        std::cout << "========================================\n\n";

        struct TestCase {
            std::string name;
            OrderFlowPattern pattern;
            double volatility;
            double trendStrength;
        };

        std::vector<TestCase> testCases = {
            {"Low Volatility", OrderFlowPattern::LowVolatility, 0.005, 0.0},
            {"High Volatility", OrderFlowPattern::HighVolatility, 0.04, 0.0},
            {"Trending Up", OrderFlowPattern::Trending, 0.015, 0.002},
            {"Mean Reverting", OrderFlowPattern::MeanReverting, 0.01, 0.0},
            {"Choppy", OrderFlowPattern::Choppy, 0.02, 0.003}
        };

        std::cout << "Testing Market Making strategy across different market conditions...\n\n";
        std::cout << std::left << std::setw(20) << "Condition"
                  << std::setw(15) << "P&L"
                  << std::setw(15) << "Trades"
                  << std::setw(15) << "Win Rate"
                  << std::setw(15) << "Position\n";
        std::cout << std::string(75, '-') << "\n";

        for (const auto& testCase : testCases) {
            // Configure backtest
            BacktestConfig config;
            config.numTicks = 500;
            config.warmupTicks = 50;
            config.verbose = false;
            config.outputDir = "build";
            config.writeTradeLog = false;
            config.writeOrderLog = false;
            config.writePnLLog = false;
            
            config.orderFlow.pattern = testCase.pattern;
            config.orderFlow.startPrice = 100;
            config.orderFlow.ordersPerTick = 8;
            config.orderFlow.volatility = testCase.volatility;
            config.orderFlow.trendStrength = testCase.trendStrength;
            config.orderFlow.minOrderSize = 20;
            config.orderFlow.maxOrderSize = 100;
            config.orderFlow.marketOrderRatio = 0.4;

            // Create backtester
            Backtester backtester(config);

            // Add strategy
            MarketMakingConfig mmConfig;
            mmConfig.name = "MM";
            mmConfig.minSpread = 2;
            mmConfig.maxSpread = 8;
            mmConfig.quoteQuantity = 50;
            mmConfig.maxInventory = 500;
            backtester.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

            // Run backtest
            auto report = backtester.run();

            // Print results
            if (!report.strategyMetrics.empty()) {
                const auto& m = report.strategyMetrics[0];
                std::cout << std::left << std::setw(20) << testCase.name
                          << std::setw(15) << m.totalPnL
                          << std::setw(15) << m.totalTrades
                          << std::setw(14) << std::fixed << std::setprecision(1) 
                          << (m.winRate * 100) << "%"
                          << std::setw(15) << m.finalPosition << "\n";
            }
        }

        std::cout << std::string(75, '-') << "\n";
        std::cout << "\nComparison complete!\n";
    }

    /**
     * Run a stress test backtest
     */
    inline void runStressBacktest() {
        std::cout << "\n========================================\n";
        std::cout << "   Stress Test Backtest\n";
        std::cout << "========================================\n\n";

        // Configure backtest - long duration, high volatility
        BacktestConfig config;
        config.numTicks = 2000;  // Long test
        config.warmupTicks = 100;
        config.verbose = true;
        config.outputDir = "build";
        
        // High volatility with momentum bursts
        config.orderFlow.pattern = OrderFlowPattern::MomentumBurst;
        config.orderFlow.startPrice = 100;
        config.orderFlow.ordersPerTick = 15;  // High order flow
        config.orderFlow.volatility = 0.025;   // 2.5% volatility
        config.orderFlow.minOrderSize = 10;
        config.orderFlow.maxOrderSize = 200;
        config.orderFlow.marketOrderRatio = 0.45;
        config.orderFlow.burstProbability = 0.08;  // 8% chance of burst
        config.orderFlow.seed = 99999;

        // Create backtester
        Backtester backtester(config);

        // Add both strategies
        MarketMakingConfig mmConfig;
        mmConfig.name = "MM-Stress";
        mmConfig.minSpread = 4;
        mmConfig.maxSpread = 12;
        mmConfig.quoteQuantity = 60;
        mmConfig.maxInventory = 600;
        mmConfig.fadeWhenFilled = true;
        backtester.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));

        MomentumConfig momConfig;
        momConfig.name = "Momentum-Stress";
        momConfig.shortPeriod = 8;
        momConfig.longPeriod = 25;
        momConfig.entryThreshold = 0.02;
        momConfig.exitThreshold = 0.01;
        momConfig.baseQuantity = 40;
        momConfig.stopLossPct = 0.04;
        momConfig.takeProfitPct = 0.08;
        momConfig.useMarketOrders = true;
        backtester.addStrategy(std::make_unique<MomentumStrategy>(momConfig));

        // Run backtest
        auto report = backtester.run();

        // Write detailed report
        backtester.writeReport(report, "build/stress_backtest_report.txt");
        
        std::cout << "\nStress test complete. Results saved to build/\n";
    }

    /**
     * Run all backtest demos
     */
    inline void runAllBacktestDemos() {
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════╗\n";
        std::cout << "║  Mercury Backtesting Demo Suite       ║\n";
        std::cout << "╚════════════════════════════════════════╝\n";

        // Run each demo
        runMarketMakingBacktest();
        std::cout << "\n\nPress Enter to continue to next test...\n";
        
        runMomentumBacktest();
        std::cout << "\n\nPress Enter to continue to next test...\n";
        
        runMultiStrategyBacktest();
        std::cout << "\n\nPress Enter to continue to next test...\n";
        
        runMarketConditionComparison();
        std::cout << "\n\nPress Enter to continue to stress test...\n";
        
        runStressBacktest();

        std::cout << "\n\n";
        std::cout << "╔════════════════════════════════════════╗\n";
        std::cout << "║  All Backtests Complete!               ║\n";
        std::cout << "╚════════════════════════════════════════╝\n";
        std::cout << "\nResults saved to build/ directory:\n";
        std::cout << "  - *_backtest_report.txt (detailed reports)\n";
        std::cout << "  - backtest_trades.csv (all trades)\n";
        std::cout << "  - backtest_orders.csv (all orders)\n";
        std::cout << "  - pnl.csv (P&L tracking)\n";
    }

}
