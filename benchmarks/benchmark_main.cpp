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
#include <absl/container/btree_map.h>
#include "BenchTiming.h"
#include "LegacyMatchingEngine.h"
#include "MatchingEngine.h"
#include "OrderBook.h"
#include "Order.h"
#include "StrategyManager.h"
#include "MarketMakingStrategy.h"
#include "MomentumStrategy.h"
#include "LegacyHashMap.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <random>
#include <vector>

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

static std::vector<int64_t> MakeDeterministicPrices(size_t count) {
    std::vector<int64_t> prices;
    prices.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        prices.push_back(100000 + static_cast<int64_t>(i * 2));
    }

    std::mt19937_64 rng(42);
    std::shuffle(prices.begin(), prices.end(), rng);
    return prices;
}

static std::vector<uint64_t> MakeSequentialIds(size_t count) {
    std::vector<uint64_t> ids(count);
    std::iota(ids.begin(), ids.end(), uint64_t{1});
    return ids;
}

static std::vector<uint64_t> MakeShuffledIds(size_t count) {
    auto ids = MakeSequentialIds(count);
    std::mt19937_64 rng(42);
    std::shuffle(ids.begin(), ids.end(), rng);
    return ids;
}

template<typename Ladder>
static void PopulatePriceLadder(Ladder& ladder, const std::vector<int64_t>& prices) {
    for (int64_t price : prices) {
        ladder.emplace(price, static_cast<uint64_t>(price));
    }
}

template<typename MapType>
static void RunPriceLadderInsert(benchmark::State& state, const char* label) {
    const auto prices = MakeDeterministicPrices(static_cast<size_t>(state.range(0)));

    for (auto _ : state) {
        MapType ladder;
        for (int64_t price : prices) {
            ladder.emplace(price, static_cast<uint64_t>(price));
        }
        benchmark::DoNotOptimize(ladder.size());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
    state.SetLabel(label);
}

template<typename MapType>
static void RunPriceLadderTopN(benchmark::State& state, const char* label) {
    const auto prices = MakeDeterministicPrices(static_cast<size_t>(state.range(0)));
    MapType ladder;
    PopulatePriceLadder(ladder, prices);

    const size_t depth = static_cast<size_t>(state.range(1));

    for (auto _ : state) {
        uint64_t sum = 0;
        size_t seen = 0;
        for (auto it = ladder.begin(); it != ladder.end() && seen < depth; ++it, ++seen) {
            sum += it->second;
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(depth));
    state.SetLabel(label);
}

template<typename HashMapType>
static void RunHashMapInsert(benchmark::State& state, const char* label) {
    const size_t count = static_cast<size_t>(state.range(0));
    const auto ids = MakeSequentialIds(count);

    for (auto _ : state) {
        HashMapType map(count);
        for (uint64_t id : ids) {
            map.insert(id, id);
        }
        benchmark::DoNotOptimize(map.size());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
    state.SetLabel(label);
}

template<typename HashMapType>
static void RunHashMapRandomFind(benchmark::State& state, const char* label) {
    const size_t count = static_cast<size_t>(state.range(0));
    const auto ids = MakeSequentialIds(count);
    const auto shuffledIds = MakeShuffledIds(count);

    HashMapType map(count);
    for (uint64_t id : ids) {
        map.insert(id, id);
    }

    for (auto _ : state) {
        uint64_t sum = 0;
        for (uint64_t id : shuffledIds) {
            if (const auto* value = map.find(id)) {
                sum += *value;
            }
        }
        benchmark::DoNotOptimize(sum);
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
    state.SetLabel(label);
}

template<typename HashMapType>
static void RunHashMapEraseInsert(benchmark::State& state, const char* label) {
    const size_t count = static_cast<size_t>(state.range(0));
    const size_t opsPerIteration = 1024;
    const auto ids = MakeSequentialIds(count);

    for (auto _ : state) {
        state.PauseTiming();
        HashMapType map(count + opsPerIteration);
        for (uint64_t id : ids) {
            map.insert(id, id);
        }
        uint64_t nextId = static_cast<uint64_t>(count + 1);
        state.ResumeTiming();

        for (size_t i = 0; i < opsPerIteration; ++i) {
            const uint64_t removeId = static_cast<uint64_t>(i + 1);
            benchmark::DoNotOptimize(map.erase(removeId));
            map.insert(nextId, nextId);
            ++nextId;
        }

        benchmark::DoNotOptimize(map.size());
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(opsPerIteration));
    state.SetLabel(label);
}

using StdPriceLadder = std::map<int64_t, uint64_t, std::greater<int64_t>>;
using BtreePriceLadder = absl::btree_map<int64_t, uint64_t, std::greater<int64_t>>;
using LegacyOrderLookup =
    MercuryBenchmarks::LegacyHashMap<uint64_t, uint64_t, MercuryBenchmarks::LegacyOrderIdHash>;
using CurrentOrderLookup = Mercury::HashMap<uint64_t, uint64_t, Mercury::OrderIdHash>;

enum class EngineOpKind {
    Limit,
    Market,
    Cancel
};

struct EngineOp {
    EngineOpKind kind;
    Side side;
    int64_t price;
    uint64_t quantity;
    uint64_t targetId;
};

static std::vector<EngineOp> MakeRealisticEngineOps(size_t count) {
    std::vector<EngineOp> ops;
    ops.reserve(count);

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> opDist(0, 9);
    std::uniform_int_distribution<int64_t> priceDist(9900, 10100);
    std::uniform_int_distribution<uint64_t> qtyDist(10, 100);

    uint64_t nextSyntheticId = 1;
    for (size_t i = 0; i < count; ++i) {
        const int op = opDist(rng);
        if (op < 7) {
            ops.push_back(EngineOp{
                EngineOpKind::Limit,
                (nextSyntheticId % 2 == 0) ? Side::Buy : Side::Sell,
                priceDist(rng),
                qtyDist(rng),
                0});
            ++nextSyntheticId;
        } else if (op < 9) {
            ops.push_back(EngineOp{
                EngineOpKind::Market,
                (nextSyntheticId % 2 == 0) ? Side::Buy : Side::Sell,
                0,
                qtyDist(rng),
                0});
            ++nextSyntheticId;
        } else {
            const uint64_t upper = nextSyntheticId > 1 ? nextSyntheticId - 1 : 1;
            std::uniform_int_distribution<uint64_t> cancelDist(1, upper);
            ops.push_back(EngineOp{
                EngineOpKind::Cancel,
                Side::Buy,
                0,
                0,
                cancelDist(rng)});
        }
    }

    return ops;
}

template<typename EngineType>
static void RunEngineSustainedThroughput(benchmark::State& state, const char* label) {
    EngineType engine;
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
    state.SetLabel(label);
}

template<typename EngineType>
static void RunEngineRealisticMix(benchmark::State& state, const char* label) {
    constexpr size_t kOpsPerIteration = 1000;
    const auto ops = MakeRealisticEngineOps(kOpsPerIteration);

    for (auto _ : state) {
        state.PauseTiming();
        EngineType engine;
        uint64_t orderId = 1;
        state.ResumeTiming();

        for (const EngineOp& op : ops) {
            switch (op.kind) {
            case EngineOpKind::Limit: {
                auto result = engine.submitOrder(
                    MakeLimitOrder(orderId++, op.side, op.price, op.quantity));
                benchmark::DoNotOptimize(result);
                break;
            }
            case EngineOpKind::Market: {
                auto result = engine.submitOrder(
                    MakeMarketOrder(orderId++, op.side, op.quantity));
                benchmark::DoNotOptimize(result);
                break;
            }
            case EngineOpKind::Cancel: {
                auto result = engine.cancelOrder(op.targetId);
                benchmark::DoNotOptimize(result);
                break;
            }
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kOpsPerIteration));
    state.SetLabel(label);
}

template<typename EngineType>
static void RunEngineDeepBookChurn(benchmark::State& state, const char* label) {
    constexpr uint64_t kOrdersPerSide = 20000;
    constexpr uint64_t kTimedOps = 4000;

    for (auto _ : state) {
        state.PauseTiming();
        EngineType engine;

        for (uint64_t i = 0; i < kOrdersPerSide; ++i) {
            engine.submitOrder(MakeLimitOrder(
                i + 1, Side::Buy, 100000 - static_cast<int64_t>(i) - 1, 100));
            engine.submitOrder(MakeLimitOrder(
                kOrdersPerSide + i + 1, Side::Sell, 100001 + static_cast<int64_t>(i), 100));
        }

        Mercury::BenchTiming::reset();
        uint64_t nextBuyId = (kOrdersPerSide * 2) + 1;
        uint64_t nextSellId = nextBuyId + kTimedOps;
        state.ResumeTiming();

        for (uint64_t i = 0; i < kTimedOps; ++i) {
            const uint64_t existingBuyId = i + 1;
            const uint64_t existingSellId = kOrdersPerSide + i + 1;

            benchmark::DoNotOptimize(engine.cancelOrder(existingBuyId));
            benchmark::DoNotOptimize(engine.submitOrder(MakeLimitOrder(
                nextBuyId++, Side::Buy, 90000 - static_cast<int64_t>(i), 100)));

            benchmark::DoNotOptimize(engine.cancelOrder(existingSellId));
            benchmark::DoNotOptimize(engine.submitOrder(MakeLimitOrder(
                nextSellId++, Side::Sell, 110000 + static_cast<int64_t>(i), 100)));

            if ((i % 64) == 0) {
                benchmark::DoNotOptimize(engine.submitOrder(MakeMarketOrder(
                    nextSellId++, Side::Buy, 50)));
                benchmark::DoNotOptimize(engine.submitOrder(MakeMarketOrder(
                    nextSellId++, Side::Sell, 50)));
            }
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kTimedOps * 4));
    state.SetLabel(label);
}

// ============================================================================
// CONTAINER A/B BENCHMARKS
// ============================================================================

static void BM_PriceLadderInsertStdMap(benchmark::State& state) {
    RunPriceLadderInsert<StdPriceLadder>(state, "std::map");
}
BENCHMARK(BM_PriceLadderInsertStdMap)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMicrosecond);

static void BM_PriceLadderInsertBtreeMap(benchmark::State& state) {
    RunPriceLadderInsert<BtreePriceLadder>(state, "absl::btree_map");
}
BENCHMARK(BM_PriceLadderInsertBtreeMap)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(10000)
    ->Unit(benchmark::kMicrosecond);

static void BM_PriceLadderTopNStdMap(benchmark::State& state) {
    RunPriceLadderTopN<StdPriceLadder>(state, "std::map");
}
BENCHMARK(BM_PriceLadderTopNStdMap)
    ->Args({1000, 20})
    ->Args({10000, 20})
    ->Args({10000, 50})
    ->Unit(benchmark::kNanosecond);

static void BM_PriceLadderTopNBtreeMap(benchmark::State& state) {
    RunPriceLadderTopN<BtreePriceLadder>(state, "absl::btree_map");
}
BENCHMARK(BM_PriceLadderTopNBtreeMap)
    ->Args({1000, 20})
    ->Args({10000, 20})
    ->Args({10000, 50})
    ->Unit(benchmark::kNanosecond);

static void BM_HashMapInsertLegacy(benchmark::State& state) {
    RunHashMapInsert<LegacyOrderLookup>(state, "legacy HashMap");
}
BENCHMARK(BM_HashMapInsertLegacy)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_HashMapInsertAbsl(benchmark::State& state) {
    RunHashMapInsert<CurrentOrderLookup>(state, "absl-backed HashMap");
}
BENCHMARK(BM_HashMapInsertAbsl)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_HashMapRandomFindLegacy(benchmark::State& state) {
    RunHashMapRandomFind<LegacyOrderLookup>(state, "legacy HashMap");
}
BENCHMARK(BM_HashMapRandomFindLegacy)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_HashMapRandomFindAbsl(benchmark::State& state) {
    RunHashMapRandomFind<CurrentOrderLookup>(state, "absl-backed HashMap");
}
BENCHMARK(BM_HashMapRandomFindAbsl)
    ->Arg(1000)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_HashMapEraseInsertLegacy(benchmark::State& state) {
    RunHashMapEraseInsert<LegacyOrderLookup>(state, "legacy HashMap");
}
BENCHMARK(BM_HashMapEraseInsertLegacy)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

static void BM_HashMapEraseInsertAbsl(benchmark::State& state) {
    RunHashMapEraseInsert<CurrentOrderLookup>(state, "absl-backed HashMap");
}
BENCHMARK(BM_HashMapEraseInsertAbsl)
    ->Arg(10000)
    ->Arg(100000)
    ->Unit(benchmark::kMicrosecond);

// ============================================================================
// ENGINE A/B BENCHMARKS
// ============================================================================

static void BM_EngineSustainedThroughputLegacy(benchmark::State& state) {
    RunEngineSustainedThroughput<MercuryBenchmarks::LegacyMatchingEngine>(state, "legacy engine");
}
BENCHMARK(BM_EngineSustainedThroughputLegacy)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100000);

static void BM_EngineSustainedThroughputCurrent(benchmark::State& state) {
    RunEngineSustainedThroughput<MatchingEngine>(state, "current engine");
}
BENCHMARK(BM_EngineSustainedThroughputCurrent)
    ->Unit(benchmark::kMicrosecond)
    ->Iterations(100000);

static void BM_EngineRealisticMixLegacy(benchmark::State& state) {
    RunEngineRealisticMix<MercuryBenchmarks::LegacyMatchingEngine>(state, "legacy engine");
}
BENCHMARK(BM_EngineRealisticMixLegacy)->Unit(benchmark::kMillisecond);

static void BM_EngineRealisticMixCurrent(benchmark::State& state) {
    RunEngineRealisticMix<MatchingEngine>(state, "current engine");
}
BENCHMARK(BM_EngineRealisticMixCurrent)->Unit(benchmark::kMillisecond);

static void BM_EngineDeepBookChurnLegacy(benchmark::State& state) {
    RunEngineDeepBookChurn<MercuryBenchmarks::LegacyMatchingEngine>(state, "legacy engine");
}
BENCHMARK(BM_EngineDeepBookChurnLegacy)->Unit(benchmark::kMillisecond);

static void BM_EngineDeepBookChurnCurrent(benchmark::State& state) {
    RunEngineDeepBookChurn<MatchingEngine>(state, "current engine");
}
BENCHMARK(BM_EngineDeepBookChurnCurrent)->Unit(benchmark::kMillisecond);

static void BM_EngineDeepBookChurnCurrentBreakdown(benchmark::State& state) {
    using Clock = std::chrono::steady_clock;

    uint64_t totalWallNs = 0;
    uint64_t totalPriceIterNs = 0;
    uint64_t totalObjectPoolNs = 0;
    uint64_t totalCallbackResultNs = 0;
    uint64_t totalLadderMapNs = 0;
    uint64_t totalHashLookupNs = 0;
    uint64_t totalIntrusiveListNs = 0;

    for (auto _ : state) {
        Mercury::BenchTiming::reset();

        state.PauseTiming();
        MatchingEngine engine;

        constexpr uint64_t kOrdersPerSide = 20000;
        constexpr uint64_t kTimedOps = 4000;

        for (uint64_t i = 0; i < kOrdersPerSide; ++i) {
            engine.submitOrder(MakeLimitOrder(
                i + 1, Side::Buy, 100000 - static_cast<int64_t>(i) - 1, 100));
            engine.submitOrder(MakeLimitOrder(
                kOrdersPerSide + i + 1, Side::Sell, 100001 + static_cast<int64_t>(i), 100));
        }

        uint64_t nextBuyId = (kOrdersPerSide * 2) + 1;
        uint64_t nextSellId = nextBuyId + kTimedOps;
        state.ResumeTiming();

        const auto wallStart = Clock::now();
        for (uint64_t i = 0; i < kTimedOps; ++i) {
            const uint64_t existingBuyId = i + 1;
            const uint64_t existingSellId = kOrdersPerSide + i + 1;

            benchmark::DoNotOptimize(engine.cancelOrder(existingBuyId));
            benchmark::DoNotOptimize(engine.submitOrder(MakeLimitOrder(
                nextBuyId++, Side::Buy, 90000 - static_cast<int64_t>(i), 100)));

            benchmark::DoNotOptimize(engine.cancelOrder(existingSellId));
            benchmark::DoNotOptimize(engine.submitOrder(MakeLimitOrder(
                nextSellId++, Side::Sell, 110000 + static_cast<int64_t>(i), 100)));

            if ((i % 64) == 0) {
                benchmark::DoNotOptimize(engine.submitOrder(MakeMarketOrder(
                    nextSellId++, Side::Buy, 50)));
                benchmark::DoNotOptimize(engine.submitOrder(MakeMarketOrder(
                    nextSellId++, Side::Sell, 50)));
            }
        }
        const auto wallEnd = Clock::now();

        const auto snapshot = Mercury::BenchTiming::snapshot();
        totalWallNs += static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(wallEnd - wallStart).count());
        totalPriceIterNs += snapshot.priceLevelIterationNs;
        totalObjectPoolNs += snapshot.objectPoolNs;
        totalCallbackResultNs += snapshot.callbackResultNs;
        totalLadderMapNs += snapshot.ladderMapNs;
        totalHashLookupNs += snapshot.hashLookupNs;
        totalIntrusiveListNs += snapshot.intrusiveListNs;
    }

    const double iterations = static_cast<double>(state.iterations());
    const double avgWallNs = totalWallNs / iterations;
    const double avgPriceIterNs = totalPriceIterNs / iterations;
    const double avgObjectPoolNs = totalObjectPoolNs / iterations;
    const double avgCallbackResultNs = totalCallbackResultNs / iterations;
    const double avgLadderMapNs = totalLadderMapNs / iterations;
    const double avgHashLookupNs = totalHashLookupNs / iterations;
    const double avgIntrusiveListNs = totalIntrusiveListNs / iterations;
    const double trackedNs = avgPriceIterNs + avgObjectPoolNs + avgCallbackResultNs +
        avgLadderMapNs + avgHashLookupNs + avgIntrusiveListNs;
    const double otherNs = avgWallNs > trackedNs ? (avgWallNs - trackedNs) : 0.0;

    state.counters["wall_ms"] = avgWallNs / 1'000'000.0;
    state.counters["price_iter_ms"] = avgPriceIterNs / 1'000'000.0;
    state.counters["object_pool_ms"] = avgObjectPoolNs / 1'000'000.0;
    state.counters["callback_result_ms"] = avgCallbackResultNs / 1'000'000.0;
    state.counters["ladder_map_ms"] = avgLadderMapNs / 1'000'000.0;
    state.counters["hash_lookup_ms"] = avgHashLookupNs / 1'000'000.0;
    state.counters["intrusive_list_ms"] = avgIntrusiveListNs / 1'000'000.0;
    state.counters["other_ms"] = otherNs / 1'000'000.0;

    if (avgWallNs > 0.0) {
        state.counters["price_iter_pct"] = (avgPriceIterNs / avgWallNs) * 100.0;
        state.counters["object_pool_pct"] = (avgObjectPoolNs / avgWallNs) * 100.0;
        state.counters["callback_result_pct"] = (avgCallbackResultNs / avgWallNs) * 100.0;
        state.counters["ladder_map_pct"] = (avgLadderMapNs / avgWallNs) * 100.0;
        state.counters["hash_lookup_pct"] = (avgHashLookupNs / avgWallNs) * 100.0;
        state.counters["intrusive_list_pct"] = (avgIntrusiveListNs / avgWallNs) * 100.0;
        state.counters["other_pct"] = (otherNs / avgWallNs) * 100.0;
    }
}
BENCHMARK(BM_EngineDeepBookChurnCurrentBreakdown)
    ->Iterations(3)
    ->Unit(benchmark::kMillisecond);

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
