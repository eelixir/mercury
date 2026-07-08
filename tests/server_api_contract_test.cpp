#include <gtest/gtest.h>

#include "ServerHelpers.h"

using namespace Mercury;

namespace {

struct FakeIdSource {
    uint64_t nextId = 9000;

    uint64_t allocateOrderId() {
        return nextId++;
    }
};

}  // namespace

TEST(ServerApiContractTest, StateJsonIncludesSimulationMicrostructureFields) {
    MarketRuntimeState state;
    state.running = true;
    state.symbol = "SIM";
    state.symbols = {"SIM", "AAPL"};
    state.sequence = 42;
    state.simulationEnabled = true;
    state.simulationRunning = true;
    state.clockMode = "accelerated";
    state.simulationSpeed = 25.0;
    state.volatilityPreset = "high";
    state.marketMakerCount = 2;
    state.momentumCount = 1;
    state.meanReversionCount = 1;
    state.noiseTraderCount = 3;
    state.realizedVolatilityBps = 55.5;
    state.averageSpread = 4.0;
    state.toxicityScore = 0.25;
    state.regime = "stressed";
    state.limitLambda = 0.03;
    state.cancelLambda = 0.08;
    state.marketableLambda = 0.06;
    state.marketMaker.levels = 4;
    state.marketMaker.quoteQuantity = 90;
    state.marketMaker.baseSpreadTicks = 5;

    AgentMetricsEvent metrics;
    metrics.symbol = "SIM";
    metrics.clientId = 1000;
    metrics.agentName = "PassiveMarketMaker";
    metrics.agentType = "market_maker";
    metrics.totalPnL = 12;
    metrics.averageFillProbability = 0.44;
    state.agentMetrics.push_back(metrics);

    const auto body = helpers::stateToJson(state, 7);
    const auto& simulation = body.at("simulation");

    EXPECT_EQ(body.at("connections").get<uint64_t>(), 7u);
    EXPECT_EQ(simulation.at("noiseTraderCount").get<size_t>(), 3u);
    EXPECT_EQ(simulation.at("regime").get<std::string>(), "stressed");
    EXPECT_DOUBLE_EQ(simulation.at("limitLambda").get<double>(), 0.03);
    EXPECT_DOUBLE_EQ(simulation.at("cancelLambda").get<double>(), 0.08);
    EXPECT_DOUBLE_EQ(simulation.at("marketableLambda").get<double>(), 0.06);
    EXPECT_EQ(simulation.at("marketMaker").at("levels").get<size_t>(), 4u);
    ASSERT_EQ(body.at("agentMetrics").size(), 1u);
    EXPECT_EQ(body.at("agentMetrics").at(0).at("agentType").get<std::string>(), "market_maker");
}

TEST(ServerApiContractTest, OrderRequestParsesSymbolAndLimitOrderShape) {
    FakeIdSource ids;
    const helpers::json body{
        {"type", "limit"},
        {"side", "sell"},
        {"price", 101},
        {"quantity", 25},
        {"clientId", 12},
        {"tif", "IOC"},
        {"symbol", "AAPL"}
    };

    const auto request = helpers::parseOrderRequestFromJson(body, ids, "SIM");

    EXPECT_EQ(request.symbol, "AAPL");
    EXPECT_EQ(request.order.id, 9000u);
    EXPECT_EQ(request.order.orderType, OrderType::Limit);
    EXPECT_EQ(request.order.side, Side::Sell);
    EXPECT_EQ(request.order.price, 101);
    EXPECT_EQ(request.order.quantity, 25u);
    EXPECT_EQ(request.order.clientId, 12u);
    EXPECT_EQ(request.order.tif, TimeInForce::IOC);
}

TEST(ServerApiContractTest, ExecutionResultJsonMatchesOrderEndpointShape) {
    Order order;
    order.id = 77;
    order.orderType = OrderType::Market;
    order.side = Side::Buy;
    order.tif = TimeInForce::IOC;

    ExecutionResult result;
    result.status = ExecutionStatus::Filled;
    result.orderId = 77;
    result.filledQuantity = 10;
    result.remainingQuantity = 0;
    result.message = "filled";
    result.trades.push_back(Trade{
        4,    // tradeId
        77,   // buyOrderId
        12,   // sellOrderId
        1,    // buyClientId
        2,    // sellClientId
        100,  // price
        10,   // quantity
        1234  // timestamp
    });

    const auto body = helpers::executionResultToJson(order, result);

    EXPECT_EQ(body.at("submittedOrderId").get<uint64_t>(), 77u);
    EXPECT_EQ(body.at("orderType").get<std::string>(), "market");
    EXPECT_EQ(body.at("side").get<std::string>(), "buy");
    EXPECT_EQ(body.at("status").get<std::string>(), "filled");
    EXPECT_EQ(body.at("filledQuantity").get<uint64_t>(), 10u);
    ASSERT_EQ(body.at("trades").size(), 1u);
    EXPECT_EQ(body.at("trades").at(0).at("tradeId").get<uint64_t>(), 4u);
}

TEST(ServerApiContractTest, SnapshotEnvelopeMatchesJsonWebSocketConnectShape) {
    L2Snapshot snapshot;
    snapshot.sequence = 88;
    snapshot.symbol = "SIM";
    snapshot.depth = 2;
    snapshot.bids.push_back(BookLevel{100, 9, 2, Side::Buy});
    snapshot.asks.push_back(BookLevel{102, 7, 1, Side::Sell});
    snapshot.bestBid = 100;
    snapshot.bestAsk = 102;
    snapshot.spread = 2;
    snapshot.midPrice = 101;
    snapshot.timestamp = 123456;

    const auto envelope = helpers::json::parse(helpers::snapshotEnvelope(snapshot));
    const auto& payload = envelope.at("payload");

    EXPECT_EQ(envelope.at("type").get<std::string>(), "snapshot");
    EXPECT_EQ(envelope.at("sequence").get<uint64_t>(), 88u);
    EXPECT_EQ(envelope.at("symbol").get<std::string>(), "SIM");
    EXPECT_EQ(payload.at("depth").get<size_t>(), 2u);
    EXPECT_EQ(payload.at("bids").at(0).at("side").get<std::string>(), "buy");
    EXPECT_EQ(payload.at("asks").at(0).at("side").get<std::string>(), "sell");
    EXPECT_EQ(payload.at("midPrice").get<int64_t>(), 101);
}

TEST(ServerApiContractTest, SimulationStateEnvelopeIncludesRegimeNoiseAndLambdas) {
    SimulationStateEvent state;
    state.sequence = 99;
    state.symbol = "SIM";
    state.enabled = true;
    state.running = true;
    state.clockMode = "accelerated";
    state.speed = 50.0;
    state.volatility = "normal";
    state.simulationTimestamp = 5000;
    state.marketMakerCount = 2;
    state.momentumCount = 2;
    state.meanReversionCount = 1;
    state.noiseTraderCount = 4;
    state.regime = "calm";
    state.limitLambda = 0.044;
    state.cancelLambda = 0.012;
    state.marketableLambda = 0.0048;
    state.marketMakerLevels = 3;
    state.marketMakerQuoteQuantity = 80;
    state.marketMakerBaseSpreadTicks = 4;
    state.marketMakerInventorySkewDivisor = 70;

    const auto envelope = helpers::json::parse(helpers::simStateEnvelope(state));
    const auto& payload = envelope.at("payload");

    EXPECT_EQ(envelope.at("type").get<std::string>(), "sim_state");
    EXPECT_EQ(payload.at("noiseTraderCount").get<size_t>(), 4u);
    EXPECT_EQ(payload.at("regime").get<std::string>(), "calm");
    EXPECT_DOUBLE_EQ(payload.at("limitLambda").get<double>(), 0.044);
    EXPECT_DOUBLE_EQ(payload.at("cancelLambda").get<double>(), 0.012);
    EXPECT_DOUBLE_EQ(payload.at("marketableLambda").get<double>(), 0.0048);
    EXPECT_EQ(payload.at("marketMaker").at("levels").get<size_t>(), 3u);
    EXPECT_EQ(payload.at("marketMaker").at("inventorySkewDivisor").get<int64_t>(), 70);
}

TEST(ServerApiContractTest, AgentMetricsEnvelopeMatchesJsonWebSocketShape) {
    AgentMetricsEvent metrics;
    metrics.sequence = 123;
    metrics.symbol = "SIM";
    metrics.clientId = 1000;
    metrics.agentName = "PassiveMarketMaker";
    metrics.agentType = "market_maker";
    metrics.totalPnL = 9;
    metrics.averageQueuePosition = 2.5;
    metrics.averageFillProbability = 0.33;

    const auto envelope = helpers::json::parse(helpers::agentMetricsEnvelope(metrics));
    const auto& payload = envelope.at("payload");

    EXPECT_EQ(envelope.at("type").get<std::string>(), "agent_metrics");
    EXPECT_EQ(envelope.at("sequence").get<uint64_t>(), 123u);
    EXPECT_EQ(payload.at("clientId").get<uint64_t>(), 1000u);
    EXPECT_EQ(payload.at("agentType").get<std::string>(), "market_maker");
    EXPECT_DOUBLE_EQ(payload.at("averageFillProbability").get<double>(), 0.33);
}

TEST(ServerApiContractTest, SimulationControlCanForceRegimeForStateEndpoint) {
    SimulationConfig config;
    config.enabled = true;
    config.noiseTraderCount = 2;

    MarketRuntime runtime("SIM", config);

    ASSERT_TRUE(runtime.applyControl({"set_regime", "stressed"}));
    const auto state = runtime.getState();
    const auto body = helpers::stateToJson(state, 0);
    const auto& simulation = body.at("simulation");

    EXPECT_EQ(state.regime, "stressed");
    EXPECT_EQ(simulation.at("regime").get<std::string>(), "stressed");
    EXPECT_EQ(simulation.at("noiseTraderCount").get<size_t>(), 2u);
    EXPECT_GT(simulation.at("cancelLambda").get<double>(), 0.0);
    EXPECT_GT(simulation.at("marketableLambda").get<double>(), 0.0);
}

TEST(ServerApiContractTest, SimulationControlCanSetAgentCountsAndMarketMakerConfig) {
    SimulationConfig config;
    config.enabled = true;

    MarketRuntime runtime("SIM", config);

    SimulationControl counts;
    counts.action = "set_counts";
    counts.hasAgentCounts = true;
    counts.marketMakerCount = 4;
    counts.momentumCount = 3;
    counts.meanReversionCount = 2;
    counts.noiseTraderCount = 1;
    ASSERT_TRUE(runtime.applyControl(counts));

    SimulationControl mm;
    mm.action = "set_market_maker";
    mm.hasMarketMakerConfig = true;
    mm.marketMaker.levels = 5;
    mm.marketMaker.quoteQuantity = 120;
    mm.marketMaker.baseSpreadTicks = 4;
    ASSERT_TRUE(runtime.applyControl(mm));

    const auto state = runtime.getState();
    EXPECT_EQ(state.marketMakerCount, 4u);
    EXPECT_EQ(state.momentumCount, 3u);
    EXPECT_EQ(state.marketMaker.levels, 5u);
    EXPECT_EQ(state.marketMaker.quoteQuantity, 120u);
}

TEST(ServerApiContractTest, SimulationControlCanSetLiveTiming) {
    SimulationConfig config;
    config.enabled = true;

    MarketRuntime runtime("SIM", config);

    SimulationControl timing;
    timing.action = "set_timing";
    timing.hasTiming = true;
    timing.clockMode = "accelerated";
    timing.speed = 25.0;
    ASSERT_TRUE(runtime.applyControl(timing));

    auto state = runtime.getState();
    EXPECT_EQ(state.clockMode, "accelerated");
    EXPECT_DOUBLE_EQ(state.simulationSpeed, 25.0);

    timing.clockMode = "instant";
    EXPECT_FALSE(runtime.applyControl(timing));
}
