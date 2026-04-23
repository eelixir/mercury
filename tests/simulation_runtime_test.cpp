#include <gtest/gtest.h>

#include "MarketRuntime.h"

#include <chrono>
#include <functional>
#include <limits>
#include <mutex>
#include <thread>

using namespace Mercury;

namespace {

class RecordingRuntimeSink : public MarketDataSink {
public:
    void onBookDelta(const BookDelta& delta) override {
        std::lock_guard<std::mutex> lock(mutex);
        deltas.push_back(delta);
    }

    void onTradeEvent(const TradeEvent& trade) override {
        std::lock_guard<std::mutex> lock(mutex);
        trades.push_back(trade);
    }

    void onStatsEvent(const StatsEvent& stats) override {
        std::lock_guard<std::mutex> lock(mutex);
        statsEvents.push_back(stats);
    }

    void onPnLEvent(const PnLEvent& pnl) override {
        std::lock_guard<std::mutex> lock(mutex);
        pnlEvents.push_back(pnl);
    }

    void onSimulationState(const SimulationStateEvent& state) override {
        std::lock_guard<std::mutex> lock(mutex);
        simStates.push_back(state);
    }

    std::mutex mutex;
    std::vector<BookDelta> deltas;
    std::vector<TradeEvent> trades;
    std::vector<StatsEvent> statsEvents;
    std::vector<PnLEvent> pnlEvents;
    std::vector<SimulationStateEvent> simStates;
};

SimulationConfig makeSimConfig() {
    SimulationConfig config;
    config.enabled = true;
    config.clockMode = SimulationClockMode::Accelerated;
    config.speed = 25.0;
    config.seed = 7;
    config.marketMakerCount = 2;
    config.momentumCount = 2;
    config.meanReversionCount = 2;
    config.stepMs = 50;
    config.publishIntervalMs = 100;
    return config;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

}  // namespace

TEST(SimulationRuntimeTest, GeneratesActivityAndFanoutEvents) {
    MarketRuntime runtime("SIM", makeSimConfig());
    RecordingRuntimeSink sink;
    runtime.addSubscriber(&sink);
    runtime.start();

    const bool active = waitUntil([&]() {
        const auto state = runtime.getState();
        return state.tradeCount > 0 && state.orderCount > 0 && state.simulationTimestamp > 0;
    });

    runtime.stop();

    EXPECT_TRUE(active);
    EXPECT_FALSE(sink.deltas.empty());
    EXPECT_FALSE(sink.statsEvents.empty());
    EXPECT_FALSE(sink.simStates.empty());
}

TEST(SimulationRuntimeTest, ManualOrdersRouteIntoLiveSimulation) {
    MarketRuntime runtime("SIM", makeSimConfig());
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        auto snapshot = runtime.getSnapshot(5);
        return snapshot.bestAsk.has_value() && snapshot.bestBid.has_value();
    }));

    auto snapshot = runtime.getSnapshot(5);
    ASSERT_TRUE(snapshot.bestAsk.has_value());

    Order order{};
    order.id = runtime.allocateOrderId();
    order.orderType = OrderType::Limit;
    order.side = Side::Buy;
    order.price = *snapshot.bestAsk;
    order.quantity = 20;
    order.clientId = 1;

    auto result = runtime.submitOrder(order);
    runtime.stop();

    EXPECT_TRUE(result.status == ExecutionStatus::Filled ||
                result.status == ExecutionStatus::PartialFill ||
                result.status == ExecutionStatus::Resting);
    EXPECT_GT(result.orderId, 0u);
}

TEST(SimulationRuntimeTest, PauseResumeAndVolatilityControlsAffectState) {
    MarketRuntime runtime("SIM", makeSimConfig());
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        return runtime.getState().simulationTimestamp > 0;
    }));

    const auto beforePause = runtime.getState().simulationTimestamp;
    EXPECT_TRUE(runtime.applyControl({"pause", ""}));
    std::this_thread::sleep_for(std::chrono::milliseconds(160));
    const auto pausedTime = runtime.getState().simulationTimestamp;

    EXPECT_LE(pausedTime, beforePause + 50);

    EXPECT_TRUE(runtime.applyControl({"set_volatility", "high"}));
    EXPECT_EQ(runtime.getState().volatilityPreset, "high");

    EXPECT_TRUE(runtime.applyControl({"resume", ""}));
    ASSERT_TRUE(waitUntil([&]() {
        return runtime.getState().simulationTimestamp > pausedTime;
    }));

    runtime.stop();
}

TEST(SimulationRuntimeTest, MaintainsTwoSidedBookAndKeepsTradingOverLongRun) {
    auto config = makeSimConfig();
    config.speed = 500.0;
    config.stepMs = 250;

    MarketRuntime runtime("SIM", config);
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        return runtime.getState().simulationTimestamp >= 15000;
    }, 4000));
    const auto midRunState = runtime.getState();

    ASSERT_TRUE(waitUntil([&]() {
        return runtime.getState().simulationTimestamp >= 60000;
    }, 7000));
    const auto lateState = runtime.getState();
    const auto snapshot = runtime.getSnapshot(5);

    runtime.stop();

    EXPECT_TRUE(snapshot.bestBid.has_value());
    EXPECT_TRUE(snapshot.bestAsk.has_value());
    EXPECT_GT(snapshot.midPrice, 0);
    EXPECT_GT(lateState.tradeCount, midRunState.tradeCount);
}

TEST(SimulationRuntimeTest, MarketMakersMaintainDeeperQuotedLevels) {
    auto config = makeSimConfig();
    config.momentumCount = 0;
    config.meanReversionCount = 0;
    config.noiseTraderCount = 0;
    config.speed = 100.0;

    MarketRuntime runtime("SIM", config);
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        const auto snapshot = runtime.getSnapshot(10);
        return snapshot.bids.size() >= 2 && snapshot.asks.size() >= 2;
    }, 3000));

    const auto snapshot = runtime.getSnapshot(10);
    runtime.stop();

    EXPECT_GE(snapshot.bids.size(), 2u);
    EXPECT_GE(snapshot.asks.size(), 2u);
    EXPECT_GT(snapshot.bids.front().quantity, snapshot.bids[1].quantity);
    EXPECT_GT(snapshot.asks.front().quantity, snapshot.asks[1].quantity);
}

TEST(SimulationRuntimeTest, PublishesBoundedToxicityScore) {
    MarketRuntime runtime("SIM", makeSimConfig());
    RecordingRuntimeSink sink;
    runtime.addSubscriber(&sink);
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(sink.mutex);
        return !sink.simStates.empty();
    }));

    runtime.stop();

    std::lock_guard<std::mutex> lock(sink.mutex);
    ASSERT_FALSE(sink.simStates.empty());
    for (const auto& state : sink.simStates) {
        EXPECT_GE(state.toxicityScore, 0.0);
        EXPECT_LE(state.toxicityScore, 1.0);
    }
}

TEST(SimulationRuntimeTest, TradeCountAdvancesAcrossMostCheckpointWindows) {
    auto config = makeSimConfig();
    config.speed = 500.0;
    config.stepMs = 100;

    MarketRuntime runtime("SIM", config);
    runtime.start();

    uint64_t previousTradeCount = 0;
    int advancingWindows = 0;

    for (uint64_t targetMs : {3000ULL, 6000ULL, 9000ULL, 12000ULL, 15000ULL, 18000ULL}) {
        ASSERT_TRUE(waitUntil([&]() {
            return runtime.getState().simulationTimestamp >= targetMs;
        }, 4000));

        const auto state = runtime.getState();
        if (state.tradeCount > previousTradeCount) {
            ++advancingWindows;
        }
        previousTradeCount = state.tradeCount;
    }

    runtime.stop();

    EXPECT_GE(advancingWindows, 5);
}

TEST(SimulationRuntimeTest, VolatilityPresetsStayWithinReasonableExcursionBounds) {
    auto config = makeSimConfig();
    config.speed = 500.0;
    config.stepMs = 250;

    RecordingRuntimeSink sink;
    MarketRuntime runtime("SIM", config);
    runtime.addSubscriber(&sink);
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        return runtime.getState().simulationTimestamp >= 30000;
    }, 5000));

    runtime.stop();

    int64_t minMid = std::numeric_limits<int64_t>::max();
    int64_t maxMid = 0;
    {
        std::lock_guard<std::mutex> lock(sink.mutex);
        for (const auto& stats : sink.statsEvents) {
            if (stats.midPrice <= 0) {
                continue;
            }
            minMid = std::min(minMid, stats.midPrice);
            maxMid = std::max(maxMid, stats.midPrice);
        }
    }

    ASSERT_LT(minMid, std::numeric_limits<int64_t>::max());
    ASSERT_GT(maxMid, 0);
    EXPECT_LT(maxMid, minMid * 3);
}
