#include <gtest/gtest.h>

#include "MarketRuntime.h"
#include "RegimeManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <random>
#include <thread>

using namespace Mercury;

namespace {

SimulationConfig makeConfig() {
    SimulationConfig config;
    config.enabled = true;
    config.clockMode = SimulationClockMode::Accelerated;
    config.speed = 50.0;
    config.seed = 11;
    config.marketMakerCount = 1;
    config.momentumCount = 1;
    config.meanReversionCount = 1;
    config.noiseTraderCount = 1;
    config.stepMs = 50;
    config.publishIntervalMs = 100;
    return config;
}

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
}

}  // namespace

TEST(RegimeManagerTest, StressedRegimeDoublesCancelAndMarketableHalvesLimits) {
    RegimeManager normal(SimulationVolatilityPreset::Normal);
    const auto base = normal.intensity();

    RegimeManager stressed(SimulationVolatilityPreset::Normal);
    stressed.forceRegime(MarketRegime::Stressed);
    const auto stressedI = stressed.intensity();

    EXPECT_NEAR(stressedI.cancelLambda,     base.cancelLambda * 2.0,     1e-9);
    EXPECT_NEAR(stressedI.marketableLambda, base.marketableLambda * 2.0, 1e-9);
    EXPECT_NEAR(stressedI.limitLambda,      base.limitLambda * 0.5,      1e-9);
}

TEST(RegimeManagerTest, CalmRegimeSlowsCrossingsAndKeepsLimitsFlowing) {
    RegimeManager normal(SimulationVolatilityPreset::Normal);
    const auto base = normal.intensity();

    RegimeManager calm(SimulationVolatilityPreset::Normal);
    calm.forceRegime(MarketRegime::Calm);
    const auto calmI = calm.intensity();

    EXPECT_LT(calmI.marketableLambda, base.marketableLambda);
    EXPECT_LT(calmI.cancelLambda,     base.cancelLambda);
    EXPECT_GT(calmI.limitLambda,      base.limitLambda * 0.9);
}

TEST(RegimeManagerTest, ObserveSwitchesIntoStressedOnHighVolatility) {
    RegimeManager manager(SimulationVolatilityPreset::Normal);
    EXPECT_EQ(manager.regime(), MarketRegime::Normal);

    // High realised vol over a long enough window must escalate to Stressed.
    for (int i = 0; i < 20; ++i) {
        manager.observe(/*vol*/ 200.0, /*burst*/ false, /*spread*/ 5.0, /*step*/ 100);
    }
    EXPECT_EQ(manager.regime(), MarketRegime::Stressed);
}

TEST(RegimeManagerTest, ObserveRelaxesToCalmOnQuietTape) {
    RegimeManager manager(SimulationVolatilityPreset::Normal);
    for (int i = 0; i < 30; ++i) {
        manager.observe(/*vol*/ 2.0, /*burst*/ false, /*spread*/ 2.0, /*step*/ 100);
    }
    EXPECT_EQ(manager.regime(), MarketRegime::Calm);
}

TEST(RegimeManagerTest, PowerLawSizesProduceWhalesAndRetail) {
    RegimeManager manager(SimulationVolatilityPreset::Normal);
    std::mt19937 rng(42);

    uint64_t small = 0;
    uint64_t large = 0;
    uint64_t maxObserved = 0;
    for (int i = 0; i < 20000; ++i) {
        const auto size = manager.sampleOrderSize(rng);
        if (size <= 5)   ++small;
        if (size >= 20)  ++large;
        maxObserved = std::max(maxObserved, size);
    }

    // Pareto: most samples near the floor, a thin tail of big prints.
    // For (x_m=2, alpha=2): P(X>=20) = (2/20)^2 = 0.01 -> ~200 in 20k.
    EXPECT_GT(small, 5000u);
    EXPECT_GT(large, 50u);
    EXPECT_LT(large, small);  // tail is thinner than the body
    EXPECT_GT(maxObserved, 60u);
}

TEST(RegimeManagerTest, PoissonCountScalesWithLambdaAndInterval) {
    std::mt19937 rng(7);
    uint64_t total = 0;
    constexpr int trials = 2000;
    for (int i = 0; i < trials; ++i) {
        total += RegimeManager::samplePoissonCount(0.05, 100, rng);  // mean = 5
    }
    const double empiricalMean = static_cast<double>(total) / trials;
    EXPECT_NEAR(empiricalMean, 5.0, 0.3);
}

TEST(RegimeManagerTest, ForcedRegimeSurvivesShortObservations) {
    RegimeManager manager(SimulationVolatilityPreset::Normal);
    manager.forceRegime(MarketRegime::Stressed);

    // Even a few quiet observations should not immediately flip us back:
    for (int i = 0; i < 5; ++i) {
        manager.observe(1.0, false, 1.0, 50);
    }
    EXPECT_EQ(manager.regime(), MarketRegime::Stressed);
}

TEST(RegimeIntegrationTest, RuntimeSurfacesRegimeAndLambdasInState) {
    MarketRuntime runtime("SIM", makeConfig());
    runtime.start();

    ASSERT_TRUE(waitUntil([&]() {
        const auto state = runtime.getState();
        return state.simulationTimestamp > 0 &&
               state.tradeCount > 0 &&
               (state.limitLambda > 0.0 || state.marketableLambda > 0.0);
    }));

    const auto state = runtime.getState();
    runtime.stop();

    EXPECT_FALSE(state.regime.empty());
    EXPECT_GT(state.limitLambda + state.cancelLambda + state.marketableLambda, 0.0);
    EXPECT_EQ(state.noiseTraderCount, 1u);
}

TEST(RegimeIntegrationTest, PoissonFlowAgentAddsOrderTraffic) {
    auto withNoise = makeConfig();
    withNoise.noiseTraderCount = 2;

    auto withoutNoise = makeConfig();
    withoutNoise.noiseTraderCount = 0;
    withoutNoise.marketMakerCount = 1;
    withoutNoise.momentumCount = 1;
    withoutNoise.meanReversionCount = 1;

    MarketRuntime withRuntime("SIM", withNoise);
    MarketRuntime withoutRuntime("SIM", withoutNoise);
    withRuntime.start();
    withoutRuntime.start();

    auto reachTs = [](MarketRuntime& rt, uint64_t target) {
        return waitUntil([&]() { return rt.getState().simulationTimestamp >= target; }, 5000);
    };

    ASSERT_TRUE(reachTs(withRuntime,    5000));
    ASSERT_TRUE(reachTs(withoutRuntime, 5000));

    const auto withState = withRuntime.getState();
    const auto withoutState = withoutRuntime.getState();
    withRuntime.stop();
    withoutRuntime.stop();

    EXPECT_GT(withState.totalVolume, withoutState.totalVolume);
}
