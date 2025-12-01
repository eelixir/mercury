/**
 * @file benchmark_main.cpp
 * @brief Micro-benchmarks for Mercury Trading Engine using Google Benchmark
 * 
 * This file contains industry-standard micro-benchmarks to measure:
 * - Order insertion latency (critical for tick-to-trade)
 * - Matching engine throughput
 * - Order book operations
 * - Strategy signal generation
 * - Memory allocation patterns
 * 
 * Run with: ./mercury_benchmarks --benchmark_format=console
 */

#include <benchmark/benchmark.h>
#include "MatchingEngine.h"
#include "OrderBook.h"
#include "Order.h"
#include "StrategyManager.h"
#include "MarketMakingStrategy.h"
#include "MomentumStrategy.h"
#include <random>

using namespace Mercury;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static Order MakeLimitOrder(uint64_t id, Side side, int64_t price, uint64_t qty) {
    Order o{};
    o.id = id;
    o.orderType = OrderType::Limit;
    o.side = side;
    o.price = price;
    o.quantity = qty;
    o.tif = TimeInForce::GTC;
    return o;
}

static Order MakeMarketOrder(uint64_t id, Side side, uint64_t qty) {
    Order o{};
    o.id = id;
    o.orderType = OrderType::Market;
    o.side = side;
    o.quantity = qty;
    return o;
}

// ============================================================================
// ORDER BOOK BENCHMARKS
// ============================================================================

/**
 * Benchmark: Limit order insertion (no match)
 * This measures the raw latency to add an order to the book.
 * Critical metric for understanding baseline performance.
 */
static void BM_LimitOrderInsert(benchmark::State& state) {
    MatchingEngine engine;
    uint64_t orderId = 1;
    
    // Pre-populate with some orders on the opposite side
    for (int i = 0; i < 100; ++i) {
        engine.submitOrder(MakeLimitOrder(orderId++, Side::Sell, 10100 + i, 100));
    }
    
    for (auto _ : state) {
        // Insert a buy order that won't match (price below best ask)
        auto result = engine.submitOrder(MakeLimitOrder(orderId++, Side::Buy, 9900, 100));
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitOrderInsert)->Unit(benchmark::kNanosecond);

/**
 * Benchmark: Limit order with immediate match
 * Measures time to match and execute a trade.
 */
static void BM_LimitOrderMatch(benchmark::State& state) {
    const int64_t NUM_LEVELS = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        uint64_t orderId = 1;
        
        // Set up ask side with multiple levels
        for (int64_t i = 0; i < NUM_LEVELS; ++i) {
            engine.submitOrder(MakeLimitOrder(orderId++, Side::Sell, 10000 + i, 100));
        }
        state.ResumeTiming();
        
        // Aggressive buy that matches at top of book
        auto result = engine.submitOrder(MakeLimitOrder(orderId++, Side::Buy, 10000, 100));
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitOrderMatch)->Arg(10)->Arg(100)->Arg(1000)->Unit(benchmark::kNanosecond);

/**
 * Benchmark: Market order sweeping multiple levels
 * Simulates aggressive order that matches through multiple price levels.
 */
static void BM_MarketOrderSweep(benchmark::State& state) {
    const int64_t LEVELS_TO_SWEEP = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        uint64_t orderId = 1;
        
        // Set up ask side
        for (int64_t i = 0; i < LEVELS_TO_SWEEP; ++i) {
            engine.submitOrder(MakeLimitOrder(orderId++, Side::Sell, 10000 + i, 100));
        }
        state.ResumeTiming();
        
        // Market order sweeps all levels
        auto result = engine.submitOrder(MakeMarketOrder(orderId++, Side::Buy, LEVELS_TO_SWEEP * 100));
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.SetLabel("levels=" + std::to_string(LEVELS_TO_SWEEP));
}
BENCHMARK(BM_MarketOrderSweep)->Arg(1)->Arg(5)->Arg(10)->Arg(50)->Unit(benchmark::kMicrosecond);

/**
 * Benchmark: Order cancellation
 * Measures latency to cancel an order from the book.
 */
static void BM_OrderCancel(benchmark::State& state) {
    const int64_t BOOK_DEPTH = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        uint64_t orderId = 1;
        
        // Fill the book
        for (int64_t i = 0; i < BOOK_DEPTH; ++i) {
            engine.submitOrder(MakeLimitOrder(orderId++, Side::Buy, 10000 - i, 100));
        }
        
        // Pick an order from the middle to cancel
        uint64_t targetId = BOOK_DEPTH / 2;
        state.ResumeTiming();
        
        auto result = engine.cancelOrder(targetId);
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderCancel)->Arg(100)->Arg(1000)->Arg(10000)->Unit(benchmark::kNanosecond);

/**
 * Benchmark: Order modification
 * Measures latency to modify an existing order.
 */
static void BM_OrderModify(benchmark::State& state) {
    const int64_t BOOK_DEPTH = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        uint64_t orderId = 1;
        
        // Fill the book
        for (int64_t i = 0; i < BOOK_DEPTH; ++i) {
            engine.submitOrder(MakeLimitOrder(orderId++, Side::Buy, 10000 - i, 100));
        }
        
        uint64_t targetId = BOOK_DEPTH / 2;
        state.ResumeTiming();
        
        // Modify price and quantity
        auto result = engine.modifyOrder(targetId, 9500, 150);
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_OrderModify)->Arg(100)->Arg(1000)->Unit(benchmark::kNanosecond);

// ============================================================================
// THROUGHPUT BENCHMARKS
// ============================================================================

/**
 * Benchmark: Sustained order throughput
 * Measures how many orders per second the engine can process.
 */
static void BM_SustainedThroughput(benchmark::State& state) {
    MatchingEngine engine;
    uint64_t orderId = 1;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> priceDist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qtyDist(1, 100);
    
    for (auto _ : state) {
        Side side = (orderId % 2 == 0) ? Side::Buy : Side::Sell;
        Order o = MakeLimitOrder(orderId++, side, priceDist(rng), qtyDist(rng));
        auto result = engine.submitOrder(o);
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["trades"] = engine.getTradeCount();
    state.counters["volume"] = engine.getTotalVolume();
}
BENCHMARK(BM_SustainedThroughput)->Unit(benchmark::kMicrosecond)->Iterations(100000);

/**
 * Benchmark: Realistic trading scenario
 * Mix of limit orders, market orders, and cancels.
 */
static void BM_RealisticMix(benchmark::State& state) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> opDist(0, 9);  // 70% limit, 20% market, 10% cancel
    std::uniform_int_distribution<int64_t> priceDist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qtyDist(10, 100);
    
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        uint64_t orderId = 1;
        state.ResumeTiming();
        
        for (int i = 0; i < 1000; ++i) {
            int op = opDist(rng);
            
            if (op < 7) {
                // 70%: Limit order
                Side side = (orderId % 2 == 0) ? Side::Buy : Side::Sell;
                auto result = engine.submitOrder(
                    MakeLimitOrder(orderId++, side, priceDist(rng), qtyDist(rng)));
                benchmark::DoNotOptimize(result);
            } else if (op < 9) {
                // 20%: Market order
                Side side = (orderId % 2 == 0) ? Side::Buy : Side::Sell;
                auto result = engine.submitOrder(MakeMarketOrder(orderId++, side, qtyDist(rng)));
                benchmark::DoNotOptimize(result);
            } else {
                // 10%: Cancel
                if (orderId > 10) {
                    std::uniform_int_distribution<uint64_t> cancelDist(1, orderId - 1);
                    auto result = engine.cancelOrder(cancelDist(rng));
                    benchmark::DoNotOptimize(result);
                }
            }
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_RealisticMix)->Unit(benchmark::kMillisecond);

// ============================================================================
// LATENCY PERCENTILE BENCHMARKS
// ============================================================================

/**
 * Benchmark: Latency distribution for order insertion
 * Important for understanding tail latency (p99, p999).
 */
static void BM_InsertLatencyDistribution(benchmark::State& state) {
    MatchingEngine engine;
    uint64_t orderId = 1;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int64_t> priceDist(9000, 9500);  // Non-matching range
    
    for (auto _ : state) {
        Side side = (orderId % 2 == 0) ? Side::Buy : Side::Sell;
        auto result = engine.submitOrder(MakeLimitOrder(orderId++, side, priceDist(rng), 100));
        benchmark::DoNotOptimize(result);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_InsertLatencyDistribution)
    ->Unit(benchmark::kNanosecond)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true);

// ============================================================================
// MEMORY ACCESS PATTERN BENCHMARKS
// ============================================================================

/**
 * Benchmark: Sequential vs random order ID access
 * Demonstrates importance of cache-friendly data structures.
 */
static void BM_SequentialCancel(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        const int N = 10000;
        
        // Insert N orders
        for (int i = 1; i <= N; ++i) {
            engine.submitOrder(MakeLimitOrder(i, Side::Buy, 10000, 100));
        }
        state.ResumeTiming();
        
        // Cancel in sequential order (cache-friendly)
        for (int i = 1; i <= N; ++i) {
            auto result = engine.cancelOrder(i);
            benchmark::DoNotOptimize(result);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_SequentialCancel)->Unit(benchmark::kMillisecond);

static void BM_RandomCancel(benchmark::State& state) {
    std::mt19937 rng(42);
    
    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        const int N = 10000;
        
        // Insert N orders
        for (int i = 1; i <= N; ++i) {
            engine.submitOrder(MakeLimitOrder(i, Side::Buy, 10000, 100));
        }
        
        // Prepare random order for cancellation
        std::vector<int> ids(N);
        std::iota(ids.begin(), ids.end(), 1);
        std::shuffle(ids.begin(), ids.end(), rng);
        state.ResumeTiming();
        
        // Cancel in random order (cache-unfriendly)
        for (int id : ids) {
            auto result = engine.cancelOrder(id);
            benchmark::DoNotOptimize(result);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_RandomCancel)->Unit(benchmark::kMillisecond);

// ============================================================================
// STRATEGY BENCHMARKS
// ============================================================================

/**
 * Benchmark: Market making strategy signal generation
 * Measures latency to process a tick and generate quote signals.
 */
static void BM_MarketMakingSignal(benchmark::State& state) {
    MatchingEngine engine;
    
    MarketMakingConfig mmConfig;
    mmConfig.quoteQuantity = 50;
    mmConfig.minSpread = 2;
    mmConfig.maxSpread = 10;
    mmConfig.maxInventory = 500;
    
    MarketMakingStrategy strategy(mmConfig);
    
    MarketTick tick;
    tick.bidPrice = 99;
    tick.askPrice = 101;
    tick.bidQuantity = 500;
    tick.askQuantity = 500;
    tick.lastTradePrice = 100;
    tick.timestamp = 1;
    
    for (auto _ : state) {
        tick.timestamp++;
        auto signals = strategy.onMarketTick(tick);
        benchmark::DoNotOptimize(signals);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketMakingSignal)->Unit(benchmark::kNanosecond);

/**
 * Benchmark: Momentum strategy signal generation (with price history)
 * Measures latency after warmup period.
 */
static void BM_MomentumSignal(benchmark::State& state) {
    MomentumConfig momConfig;
    momConfig.shortPeriod = 5;
    momConfig.longPeriod = 20;
    momConfig.baseQuantity = 30;
    momConfig.maxPosition = 100;
    
    MomentumStrategy strategy(momConfig);
    
    // Warmup with price history
    int64_t price = 100;
    for (uint64_t i = 0; i < momConfig.longPeriod + 10; ++i) {
        MarketTick tick;
        tick.bidPrice = price - 1;
        tick.askPrice = price + 1;
        tick.lastTradePrice = price;
        tick.lastTradeQuantity = 100;
        tick.timestamp = i + 1;
        strategy.onMarketTick(tick);
        price += (i % 3 == 0) ? 1 : -1;  // Small oscillation
    }
    
    for (auto _ : state) {
        MarketTick tick;
        tick.bidPrice = price - 1;
        tick.askPrice = price + 1;
        tick.lastTradePrice = price;
        tick.lastTradeQuantity = 100;
        tick.timestamp++;
        
        auto signals = strategy.onMarketTick(tick);
        benchmark::DoNotOptimize(signals);
        
        price += (tick.timestamp % 3 == 0) ? 1 : -1;
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MomentumSignal)->Unit(benchmark::kNanosecond);

/**
 * Benchmark: Strategy manager with multiple strategies
 * Measures latency to process tick through all strategies.
 */
static void BM_StrategyManagerTick(benchmark::State& state) {
    MatchingEngine engine;
    StrategyManager manager(engine);
    
    // Disable logging for benchmark
    StrategyManagerConfig config;
    config.logExecutions = false;
    config.logSignals = false;
    manager.setConfig(config);
    
    MarketMakingConfig mmConfig;
    mmConfig.name = "MM";
    mmConfig.quoteQuantity = 50;
    manager.addStrategy(std::make_unique<MarketMakingStrategy>(mmConfig));
    
    MomentumConfig momConfig;
    momConfig.name = "Mom";
    momConfig.shortPeriod = 5;
    momConfig.longPeriod = 20;
    manager.addStrategy(std::make_unique<MomentumStrategy>(momConfig));
    
    // Warmup
    int64_t price = 100;
    for (int i = 0; i < 50; ++i) {
        MarketTick tick;
        tick.bidPrice = price - 1;
        tick.askPrice = price + 1;
        tick.lastTradePrice = price;
        tick.bidQuantity = 500;
        tick.askQuantity = 500;
        tick.lastTradeQuantity = 100;
        tick.timestamp = i + 1;
        manager.onMarketTick(tick);
        price += (i % 3 == 0) ? 1 : -1;
    }
    
    uint64_t ts = 51;
    for (auto _ : state) {
        MarketTick tick;
        tick.bidPrice = price - 1;
        tick.askPrice = price + 1;
        tick.lastTradePrice = price;
        tick.bidQuantity = 500;
        tick.askQuantity = 500;
        tick.lastTradeQuantity = 100;
        tick.timestamp = ts++;
        
        manager.onMarketTick(tick);
        benchmark::DoNotOptimize(manager.getTotalOrders());
        
        price += (ts % 3 == 0) ? 1 : -1;
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StrategyManagerTick)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
