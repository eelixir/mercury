#pragma once

#include "EngineService.h"
#include "MarketData.h"
#include "PnLTracker.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Mercury {

    enum class SimulationClockMode {
        Realtime,
        Accelerated
    };

    enum class SimulationVolatilityPreset {
        Low,
        Normal,
        High
    };

    inline const char* simulationClockModeToString(SimulationClockMode mode) {
        switch (mode) {
            case SimulationClockMode::Realtime: return "realtime";
            case SimulationClockMode::Accelerated: return "accelerated";
            default: return "realtime";
        }
    }

    inline const char* simulationVolatilityToString(SimulationVolatilityPreset preset) {
        switch (preset) {
            case SimulationVolatilityPreset::Low: return "low";
            case SimulationVolatilityPreset::Normal: return "normal";
            case SimulationVolatilityPreset::High: return "high";
            default: return "normal";
        }
    }

    inline SimulationVolatilityPreset simulationVolatilityFromString(const std::string& value) {
        if (value == "low") return SimulationVolatilityPreset::Low;
        if (value == "high") return SimulationVolatilityPreset::High;
        return SimulationVolatilityPreset::Normal;
    }

    struct SimulationConfig {
        bool enabled = false;
        bool headless = false;
        SimulationClockMode clockMode = SimulationClockMode::Realtime;
        double speed = 1.0;
        uint32_t seed = 42;
        SimulationVolatilityPreset volatility = SimulationVolatilityPreset::Normal;
        size_t marketMakerCount = 2;
        size_t momentumCount = 2;
        size_t meanReversionCount = 2;
        uint64_t stepMs = 50;
        uint64_t publishIntervalMs = 250;
        uint64_t headlessDurationMs = 30000;
    };

    struct SimulationEnvironmentView {
        int64_t latentFairValue = 0;
        bool momentumBurst = false;
        int momentumDirection = 0;
        double realizedVolatilityBps = 0.0;
        double averageSpread = 0.0;
        std::string volatilityPreset;
        uint64_t simulationTimestamp = 0;
    };

    struct SimulatedOrderInfo {
        uint64_t orderId = 0;
        Side side = Side::Buy;
        int64_t price = 0;
        uint64_t quantity = 0;
    };

    struct SimulationObservation {
        L2Snapshot snapshot;
        std::vector<TradeEvent> recentTrades;
        StatsEvent stats;
        std::optional<PnLEvent> ownPnL;
        std::vector<SimulatedOrderInfo> liveOrders;
        SimulationEnvironmentView environment;
    };

    enum class OrderIntentKind {
        None,
        PlaceLimit,
        PlaceMarket,
        Cancel,
        Modify
    };

    struct OrderIntent {
        OrderIntentKind kind = OrderIntentKind::None;
        Side side = Side::Buy;
        int64_t price = 0;
        uint64_t quantity = 0;
        TimeInForce tif = TimeInForce::GTC;
        uint64_t orderId = 0;
        int64_t newPrice = 0;
        uint64_t newQuantity = 0;
    };

    class SimulationAgent {
    public:
        virtual ~SimulationAgent() = default;

        virtual std::string name() const = 0;
        virtual uint64_t wakeIntervalMs() const = 0;
        virtual std::vector<OrderIntent> onWake(const SimulationObservation& observation) = 0;
        virtual void reset() {}
    };

    using SimulationAgentFactory = std::function<std::unique_ptr<SimulationAgent>()>;

    struct SimulationControl {
        std::string action;
        std::string volatility;
    };

    struct MarketRuntimeState {
        bool running = false;
        bool replayActive = false;
        std::string symbol;           // Primary symbol (backward compat)
        std::vector<std::string> symbols;
        uint64_t sequence = 0;
        uint64_t nextOrderId = 1;
        uint64_t tradeCount = 0;
        uint64_t totalVolume = 0;
        size_t orderCount = 0;
        size_t bidLevels = 0;
        size_t askLevels = 0;
        size_t clientCount = 0;
        bool simulationEnabled = false;
        bool simulationRunning = false;
        bool simulationPaused = false;
        std::string clockMode = "realtime";
        double simulationSpeed = 1.0;
        std::string volatilityPreset = "normal";
        uint64_t simulationTimestamp = 0;
        size_t marketMakerCount = 0;
        size_t momentumCount = 0;
        size_t meanReversionCount = 0;
        double realizedVolatilityBps = 0.0;
        double averageSpread = 0.0;
    };

    struct HeadlessSummary {
        uint64_t simulationTimestamp = 0;
        uint64_t tradeCount = 0;
        uint64_t totalVolume = 0;
        size_t orderCount = 0;
        int64_t lastMidPrice = 0;
        double realizedVolatilityBps = 0.0;
        double averageSpread = 0.0;
    };

    class MarketRuntime : public MarketDataSink {
    public:
        explicit MarketRuntime(std::vector<std::string> symbols = {"SIM"},
                               SimulationConfig simulationConfig = {});

        // Backward-compatible single-symbol constructor.
        explicit MarketRuntime(std::string symbol,
                               SimulationConfig simulationConfig = {});

        ~MarketRuntime() override;

        MarketRuntime(const MarketRuntime&) = delete;
        MarketRuntime& operator=(const MarketRuntime&) = delete;

        void start();
        void stop();

        bool isRunning() const { return running_.load(); }
        bool isReplayActive() const;
        uint64_t allocateOrderId();

        ExecutionResult submitOrder(Order order);
        ExecutionResult submitOrder(Order order, uint64_t entryTimestampNs);
        ExecutionResult submitOrder(const std::string& symbol, Order order);
        ExecutionResult submitOrder(const std::string& symbol, Order order, uint64_t entryTimestampNs);

        L2Snapshot getSnapshot(size_t depth = 20);
        L2Snapshot getSnapshot(const std::string& symbol, size_t depth = 20);

        MarketRuntimeState getState();
        HeadlessSummary getHeadlessSummary();

        bool startReplay(const std::string& inputFile, double speed = 1.0,
                         bool loop = false, uint64_t loopPauseMs = 1000);
        void stopReplay();

        bool applyControl(const SimulationControl& control);

        void addSubscriber(MarketDataSink* sink);
        void removeSubscriber(MarketDataSink* sink);

        void addCustomAgentFactory(SimulationAgentFactory factory, uint64_t clientId);

        const std::string& getSymbol() const { return symbols_.front(); }
        const std::vector<std::string>& getSymbols() const { return symbols_; }
        const SimulationConfig& getSimulationConfig() const { return simulationConfig_; }

        void onBookDelta(const BookDelta& delta) override;
        void onTradeEvent(const TradeEvent& trade) override;
        void onStatsEvent(const StatsEvent& stats) override;
        void onPnLEvent(const PnLEvent& pnl) override;
        void onExecutionEvent(const ExecutionEvent& execution) override;

    private:
        struct AgentDescriptor {
            SimulationAgentFactory factory;
            uint64_t clientId = 0;
        };

        struct AgentSlot {
            std::unique_ptr<SimulationAgent> agent;
            uint64_t clientId = 0;
            uint64_t nextWakeMs = 0;
        };

        struct LiveOrderRecord {
            uint64_t orderId = 0;
            uint64_t clientId = 0;
            Side side = Side::Buy;
            int64_t price = 0;
            uint64_t quantity = 0;
        };

        // Per-symbol simulation environment state.
        struct PerSymbolEnvironment {
            int64_t latentFairValue = 100;
            bool momentumBurst = false;
            int momentumDirection = 0;
            uint64_t burstRemainingMs = 0;
            double averageSpread = 0.0;
            double realizedVolatilityBps = 0.0;
            std::vector<int64_t> midHistory;
            StatsEvent lastStats;
            std::vector<TradeEvent> recentTrades;
            std::unordered_map<uint64_t, LiveOrderRecord> liveOrdersById;
            std::unordered_map<uint64_t, PnLEvent> pnlByClient;
            uint64_t lastSimStatePublishMs = 0;
        };

        std::vector<std::string> symbols_;
        SimulationConfig simulationConfig_;
        std::unique_ptr<EngineService> engineService_;

        std::atomic<bool> running_{false};
        std::atomic<bool> stopRequested_{false};
        std::atomic<bool> simulationPaused_{false};

        std::thread simulationThread_;

        mutable std::mutex stateMutex_;
        std::unordered_map<std::string, PerSymbolEnvironment> envs_;
        uint64_t simulationTimestampMs_ = 0;
        uint64_t lastPublishedSequence_ = 0;
        std::mt19937 rng_;

        std::mutex subscribersMutex_;
        std::vector<MarketDataSink*> subscribers_;

        std::mutex agentsMutex_;
        std::vector<AgentDescriptor> customAgentFactories_;
        // Agents are per-symbol.
        std::unordered_map<std::string, std::vector<AgentSlot>> agentsBySymbol_;

        void createEngineService();
        void rebuildAgents();
        void simulationLoop();
        void advanceEnvironment(const std::string& symbol, uint64_t stepMs);
        void maybeWakeAgents();
        void publishSimulationState(bool force = false);
        void fanout(const std::function<void(MarketDataSink*)>& fn);
        SimulationObservation buildObservation(const std::string& symbol, uint64_t clientId);
        Order translateIntent(const OrderIntent& intent, uint64_t clientId);
        void applyExecutionToLiveOrders(const std::string& symbol, const Order& order, const ExecutionResult& result);
        void updateMetricsFromStats(const std::string& symbol, const StatsEvent& stats);
        void restartRuntime();

        static uint64_t steadyNowNs();
    };

}
