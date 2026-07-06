#include <gtest/gtest.h>

#include "BacktestReport.h"

using namespace Mercury;

namespace {

StatsEvent makeStats(uint64_t sequence, int64_t midPrice) {
    StatsEvent stats;
    stats.sequence = sequence;
    stats.symbol = "SIM";
    stats.midPrice = midPrice;
    return stats;
}

}  // namespace

TEST(BacktestReportTest, SummaryComputesRunMetricsFromEvents) {
    SimulationConfig config;
    config.clockMode = SimulationClockMode::Instant;
    config.seed = 99;
    config.volatility = SimulationVolatilityPreset::High;
    config.marketMakerCount = 3;
    config.momentumCount = 4;
    config.meanReversionCount = 5;
    config.noiseTraderCount = 6;

    HeadlessSummary runtimeSummary;
    runtimeSummary.simulationTimestamp = 2000;
    runtimeSummary.tradeCount = 12;
    runtimeSummary.totalVolume = 345;
    runtimeSummary.orderCount = 67;
    runtimeSummary.lastMidPrice = 104;
    runtimeSummary.realizedVolatilityBps = 17.25;
    runtimeSummary.averageSpread = 2.5;

    BacktestEventLog events;
    events.stats.push_back(makeStats(1, 100));
    events.stats.push_back(makeStats(2, 110));
    events.stats.push_back(makeStats(3, 104));

    SimulationStateEvent state;
    state.regime = "stressed";
    state.toxicityScore = 0.42;
    events.simStates.push_back(state);

    AgentMetricsEvent metrics;
    metrics.symbol = "SIM";
    metrics.clientId = 1000;
    metrics.agentName = "PassiveMarketMaker";
    metrics.agentType = "market_maker";
    metrics.wakeCount = 5;
    metrics.submittedCount = 12;
    metrics.limitOrderCount = 10;
    metrics.cancelCount = 1;
    metrics.modifyCount = 1;
    metrics.fillCount = 2;
    metrics.filledQuantity = 20;
    metrics.liveOrderCount = 3;
    metrics.restingQuantity = 90;
    metrics.totalPnL = 7;
    metrics.averageQueuePosition = 1.5;
    metrics.averageQuantityAhead = 40.0;
    metrics.averageFillProbability = 0.45;
    metrics.averageTimeToFillMs = 120.0;
    events.agentMetrics.push_back(metrics);

    const auto summary = buildBacktestSummary(
        "high-vol",
        config,
        {"SIM", "AAPL"},
        runtimeSummary,
        events,
        2000,
        25);

    EXPECT_EQ(summary.name, "high-vol");
    EXPECT_EQ(summary.clockMode, "instant");
    EXPECT_EQ(summary.symbols.size(), 2u);
    EXPECT_EQ(summary.seed, 99u);
    EXPECT_EQ(summary.volatility, "high");
    EXPECT_DOUBLE_EQ(summary.effectiveSpeed, 80.0);
    EXPECT_EQ(summary.tradeCount, 12u);
    EXPECT_EQ(summary.totalVolume, 345u);
    EXPECT_EQ(summary.orderCount, 67u);
    EXPECT_EQ(summary.minMidPrice, 100);
    EXPECT_EQ(summary.maxMidPrice, 110);
    EXPECT_EQ(summary.maxDrawdownTicks, 6);
    EXPECT_NEAR(summary.maxDrawdownBps, 545.45, 0.01);
    EXPECT_EQ(summary.finalRegime, "stressed");
    EXPECT_DOUBLE_EQ(summary.finalToxicityScore, 0.42);
    EXPECT_EQ(summary.marketMakerCount, 3u);
    EXPECT_EQ(summary.finalTotalPnL, 7);
    ASSERT_EQ(summary.agents.size(), 1u);
    EXPECT_EQ(summary.agents.front().agentType, "market_maker");
    EXPECT_DOUBLE_EQ(summary.queueAnalytics.averageFillProbability, 0.45);

    const auto json = backtestSummaryToJson(summary);
    EXPECT_EQ(json.at("name"), "high-vol");
    EXPECT_EQ(json.at("agentCounts").at("noiseTraderCount").get<size_t>(), 6u);
    EXPECT_EQ(json.at("agents").at(0).at("clientId").get<uint64_t>(), 1000u);
}

TEST(BacktestReportTest, CsvEscapeQuotesOnlyWhenNeeded) {
    EXPECT_EQ(csvEscape("SIM"), "SIM");
    EXPECT_EQ(csvEscape("SIM,AAPL"), "\"SIM,AAPL\"");
    EXPECT_EQ(csvEscape("quote\"inside"), "\"quote\"\"inside\"");
}
