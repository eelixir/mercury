#include "MarketRuntime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <thread>

namespace Mercury {

    namespace {

        struct VolatilityParams {
            double fairValueSigma = 0.5;
            double burstProbabilityPerStep = 0.01;
            int64_t burstStepTicks = 2;
            uint64_t burstDurationMs = 600;
            uint64_t marketMakerSpread = 4;
            uint64_t marketMakerQuoteQty = 60;
            uint64_t momentumBaseQty = 25;
            uint64_t momentumMaxQty = 40;
            uint64_t meanReversionBaseQty = 35;
            double topFragility = 1.0;
            double fairValuePull = 0.1;
            int64_t maxNoiseTicks = 2;
        };

        VolatilityParams paramsForPreset(SimulationVolatilityPreset preset) {
            switch (preset) {
                case SimulationVolatilityPreset::Low:
                    return VolatilityParams{0.08, 0.0015, 1, 200, 2, 120, 6, 12, 36, 0.65, 0.10, 1};
                case SimulationVolatilityPreset::High:
                    return VolatilityParams{0.35, 0.01, 2, 450, 5, 80, 18, 35, 42, 1.10, 0.16, 3};
                case SimulationVolatilityPreset::Normal:
                default:
                    return VolatilityParams{0.18, 0.004, 1, 300, 3, 100, 10, 20, 40, 0.85, 0.12, 2};
            }
        }

        const SimulatedOrderInfo* findOrderBySide(const std::vector<SimulatedOrderInfo>& orders, Side side) {
            for (const auto& order : orders) {
                if (order.side == side) {
                    return &order;
                }
            }
            return nullptr;
        }

        int64_t fallbackReferencePrice(const SimulationObservation& observation) {
            if (observation.snapshot.midPrice > 0) {
                return observation.snapshot.midPrice;
            }
            if (observation.snapshot.bestBid && observation.snapshot.bestAsk) {
                return (*observation.snapshot.bestBid + *observation.snapshot.bestAsk) / 2;
            }
            if (observation.environment.latentFairValue > 0) {
                return observation.environment.latentFairValue;
            }
            if (!observation.recentTrades.empty() && observation.recentTrades.front().price > 0) {
                return observation.recentTrades.front().price;
            }
            if (observation.stats.midPrice > 0) {
                return observation.stats.midPrice;
            }
            if (observation.snapshot.bestBid) {
                return *observation.snapshot.bestBid;
            }
            if (observation.snapshot.bestAsk) {
                return *observation.snapshot.bestAsk;
            }
            return 100;
        }

        double topLevelImbalance(const SimulationObservation& observation) {
            if (observation.snapshot.bids.empty() || observation.snapshot.asks.empty()) {
                return 0.0;
            }

            const double bidQty = static_cast<double>(observation.snapshot.bids.front().quantity);
            const double askQty = static_cast<double>(observation.snapshot.asks.front().quantity);
            const double totalQty = bidQty + askQty;
            if (totalQty <= 0.0) {
                return 0.0;
            }

            return (bidQty - askQty) / totalQty;
        }

        class PassiveMarketMakerAgent final : public SimulationAgent {
        public:
            explicit PassiveMarketMakerAgent(SimulationVolatilityPreset preset)
                : preset_(preset) {}

            std::string name() const override { return "PassiveMarketMaker"; }

            uint64_t wakeIntervalMs() const override {
                switch (preset_) {
                    case SimulationVolatilityPreset::Low: return 170;
                    case SimulationVolatilityPreset::High: return 70;
                    case SimulationVolatilityPreset::Normal:
                    default: return 95;
                }
            }

            std::vector<OrderIntent> onWake(const SimulationObservation& observation) override {
                std::vector<OrderIntent> intents;
                const auto fair = observation.environment.latentFairValue > 0
                    ? observation.environment.latentFairValue
                    : fallbackReferencePrice(observation);
                if (fair <= 0) {
                    return intents;
                }

                const auto params = paramsForPreset(preset_);
                const int64_t inventory = observation.ownPnL ? observation.ownPnL->netPosition : 0;
                const int64_t toxicityAdjust = observation.environment.momentumBurst ? 2 : 0;
                const int64_t inventoryAdjust = std::min<int64_t>(4, std::abs(inventory) / 40);
                const int64_t spread = static_cast<int64_t>(params.marketMakerSpread) + toxicityAdjust + inventoryAdjust;
                const int64_t skew = inventory / 50;
                const int64_t bidPrice = std::max<int64_t>(1, fair - spread / 2 - skew);
                const int64_t askPrice = std::max<int64_t>(bidPrice + 1, fair + spread / 2 - skew);
                const uint64_t minQuoteQty = std::max<uint64_t>(10, params.marketMakerQuoteQty / 4);

                uint64_t bidQty = params.marketMakerQuoteQty;
                uint64_t askQty = params.marketMakerQuoteQty;
                if (inventory > 0) {
                    bidQty = inventory > 120 ? minQuoteQty : std::max<uint64_t>(minQuoteQty, bidQty / 2);
                    askQty = static_cast<uint64_t>(askQty * 1.25);
                } else if (inventory < 0) {
                    askQty = -inventory > 120 ? minQuoteQty : std::max<uint64_t>(minQuoteQty, askQty / 2);
                    bidQty = static_cast<uint64_t>(bidQty * 1.25);
                }

                if (observation.snapshot.bids.empty()) {
                    bidQty = std::max<uint64_t>(bidQty, minQuoteQty);
                }
                if (observation.snapshot.asks.empty()) {
                    askQty = std::max<uint64_t>(askQty, minQuoteQty);
                }

                const auto* liveBid = findOrderBySide(observation.liveOrders, Side::Buy);
                const auto* liveAsk = findOrderBySide(observation.liveOrders, Side::Sell);

                if (bidQty == 0 && liveBid) {
                    intents.push_back(OrderIntent{OrderIntentKind::Cancel, Side::Buy, 0, 0, TimeInForce::GTC, liveBid->orderId});
                } else if (bidQty > 0) {
                    if (liveBid) {
                        if (std::abs(liveBid->price - bidPrice) >= 1 || liveBid->quantity != bidQty) {
                            intents.push_back(OrderIntent{
                                OrderIntentKind::Modify, Side::Buy, 0, 0, TimeInForce::GTC,
                                liveBid->orderId, bidPrice, bidQty
                            });
                        }
                    } else {
                        intents.push_back(OrderIntent{
                            OrderIntentKind::PlaceLimit, Side::Buy, bidPrice, bidQty, TimeInForce::GTC
                        });
                    }
                }

                if (askQty == 0 && liveAsk) {
                    intents.push_back(OrderIntent{OrderIntentKind::Cancel, Side::Sell, 0, 0, TimeInForce::GTC, liveAsk->orderId});
                } else if (askQty > 0) {
                    if (liveAsk) {
                        if (std::abs(liveAsk->price - askPrice) >= 1 || liveAsk->quantity != askQty) {
                            intents.push_back(OrderIntent{
                                OrderIntentKind::Modify, Side::Sell, 0, 0, TimeInForce::GTC,
                                liveAsk->orderId, askPrice, askQty
                            });
                        }
                    } else {
                        intents.push_back(OrderIntent{
                            OrderIntentKind::PlaceLimit, Side::Sell, askPrice, askQty, TimeInForce::GTC
                        });
                    }
                }

                return intents;
            }

        private:
            SimulationVolatilityPreset preset_;
        };

        class AggressiveMomentumAgent final : public SimulationAgent {
        public:
            explicit AggressiveMomentumAgent(SimulationVolatilityPreset preset)
                : preset_(preset) {}

            std::string name() const override { return "AggressiveMomentum"; }

            uint64_t wakeIntervalMs() const override {
                switch (preset_) {
                    case SimulationVolatilityPreset::Low: return 220;
                    case SimulationVolatilityPreset::High: return 60;
                    case SimulationVolatilityPreset::Normal:
                    default: return 100;
                }
            }

            std::vector<OrderIntent> onWake(const SimulationObservation& observation) override {
                const auto reference = fallbackReferencePrice(observation);

                mids_.push_back(reference);
                if (mids_.size() > 6) {
                    mids_.pop_front();
                }

                if (mids_.size() < 4) {
                    return {};
                }

                const auto params = paramsForPreset(preset_);
                const int64_t shortMove = mids_.back() - mids_.front();
                const double imbalance = topLevelImbalance(observation);
                const int64_t fair = observation.environment.latentFairValue > 0
                    ? observation.environment.latentFairValue
                    : reference;
                const int64_t buyEdge = observation.snapshot.bestAsk
                    ? (fair - *observation.snapshot.bestAsk)
                    : 0;
                const int64_t sellEdge = observation.snapshot.bestBid
                    ? (*observation.snapshot.bestBid - fair)
                    : 0;

                int direction = 0;
                bool burstDriven = false;
                bool probeDriven = false;
                if (observation.environment.momentumBurst) {
                    direction = observation.environment.momentumDirection;
                    burstDriven = true;
                } else if (std::abs(shortMove) >= 1) {
                    direction = shortMove > 0 ? 1 : -1;
                } else if (buyEdge >= 1 && imbalance > -0.30) {
                    direction = 1;
                } else if (sellEdge >= 1 && imbalance < 0.30) {
                    direction = -1;
                } else if (std::abs(imbalance) >= 0.22) {
                    direction = imbalance > 0.0 ? 1 : -1;
                }

                const uint64_t nowMs = observation.environment.simulationTimestamp;
                if (direction == 0) {
                    const uint64_t probeCooldownMs =
                        preset_ == SimulationVolatilityPreset::High ? 180 :
                        preset_ == SimulationVolatilityPreset::Low ? 420 : 280;
                    if (nowMs >= lastAggressionTimestampMs_ + probeCooldownMs) {
                        if (std::abs(imbalance) >= 0.10) {
                            direction = imbalance > 0.0 ? 1 : -1;
                            probeDriven = true;
                        } else if (observation.snapshot.bestBid.has_value() &&
                                   observation.snapshot.bestAsk.has_value()) {
                            direction = ((nowMs / probeCooldownMs) % 2 == 0) ? 1 : -1;
                            probeDriven = true;
                        }
                    }
                }

                if (direction == 0) {
                    return {};
                }

                const uint64_t cooldownMs = burstDriven
                    ? (preset_ == SimulationVolatilityPreset::High ? 60 : 90)
                    : (preset_ == SimulationVolatilityPreset::Low ? 220 : 130);
                if (nowMs < lastAggressionTimestampMs_ + cooldownMs) {
                    return {};
                }

                const uint64_t baseQty = burstDriven
                    ? params.momentumBaseQty
                    : probeDriven
                        ? std::max<uint64_t>(3, params.momentumBaseQty / 4)
                        : std::max<uint64_t>(4, params.momentumBaseQty / 3);
                const uint64_t maxQty = burstDriven
                    ? params.momentumMaxQty
                    : probeDriven
                        ? std::max<uint64_t>(baseQty + 2, params.momentumMaxQty / 5)
                        : std::max<uint64_t>(baseQty + 6, params.momentumMaxQty / 3);
                const uint64_t qty = std::min<uint64_t>(
                    maxQty,
                    baseQty + static_cast<uint64_t>(std::abs(shortMove)) * (burstDriven ? 2ULL : 1ULL));

                if ((direction > 0 && !observation.snapshot.bestAsk.has_value()) ||
                    (direction < 0 && !observation.snapshot.bestBid.has_value())) {
                    return {};
                }

                lastAggressionTimestampMs_ = nowMs;
                return {OrderIntent{
                    OrderIntentKind::PlaceMarket,
                    direction > 0 ? Side::Buy : Side::Sell,
                    0,
                    qty,
                    TimeInForce::IOC
                }};
            }

            void reset() override {
                mids_.clear();
            }

        private:
            SimulationVolatilityPreset preset_;
            std::deque<int64_t> mids_;
            uint64_t lastAggressionTimestampMs_ = 0;
        };

        class MeanReversionAgent final : public SimulationAgent {
        public:
            explicit MeanReversionAgent(SimulationVolatilityPreset preset)
                : preset_(preset) {}

            std::string name() const override { return "MeanReversion"; }

            uint64_t wakeIntervalMs() const override {
                switch (preset_) {
                    case SimulationVolatilityPreset::Low: return 240;
                    case SimulationVolatilityPreset::High: return 110;
                    case SimulationVolatilityPreset::Normal:
                    default: return 160;
                }
            }

            std::vector<OrderIntent> onWake(const SimulationObservation& observation) override {
                std::vector<OrderIntent> intents;
                const auto fair = observation.environment.latentFairValue;
                const auto reference = fallbackReferencePrice(observation);
                if (fair <= 0 || reference <= 0) {
                    return intents;
                }

                const auto deviation = reference - fair;
                const auto params = paramsForPreset(preset_);
                const int64_t threshold = static_cast<int64_t>(2 + params.topFragility);
                const auto* liveBid = findOrderBySide(observation.liveOrders, Side::Buy);
                const auto* liveAsk = findOrderBySide(observation.liveOrders, Side::Sell);
                const uint64_t nowMs = observation.environment.simulationTimestamp;
                const bool canAggress =
                    nowMs >= lastAggressionTimestampMs_ +
                        (preset_ == SimulationVolatilityPreset::High ? 110 : 170);

                if (std::abs(deviation) < threshold) {
                    if (liveBid) {
                        intents.push_back(OrderIntent{OrderIntentKind::Cancel, Side::Buy, 0, 0, TimeInForce::GTC, liveBid->orderId});
                    }
                    if (liveAsk) {
                        intents.push_back(OrderIntent{OrderIntentKind::Cancel, Side::Sell, 0, 0, TimeInForce::GTC, liveAsk->orderId});
                    }
                    return intents;
                }

                const uint64_t qty = params.meanReversionBaseQty + static_cast<uint64_t>(std::abs(deviation));
                if (deviation > 0) {
                    if (canAggress && std::abs(deviation) >= threshold * 2 && observation.snapshot.bestBid.has_value()) {
                        lastAggressionTimestampMs_ = nowMs;
                        intents.push_back(OrderIntent{
                            OrderIntentKind::PlaceLimit, Side::Sell, *observation.snapshot.bestBid,
                            std::max<uint64_t>(8, qty / 2), TimeInForce::IOC
                        });
                    }

                    const int64_t price = std::max<int64_t>(1, fair + 1);
                    if (liveAsk) {
                        intents.push_back(OrderIntent{
                            OrderIntentKind::Modify, Side::Sell, 0, 0, TimeInForce::GTC,
                            liveAsk->orderId, price, qty
                        });
                    } else {
                        intents.push_back(OrderIntent{
                            OrderIntentKind::PlaceLimit, Side::Sell, price, qty, TimeInForce::GTC
                        });
                    }
                    if (liveBid) {
                        intents.push_back(OrderIntent{OrderIntentKind::Cancel, Side::Buy, 0, 0, TimeInForce::GTC, liveBid->orderId});
                    }
                } else {
                    if (canAggress && std::abs(deviation) >= threshold * 2 && observation.snapshot.bestAsk.has_value()) {
                        lastAggressionTimestampMs_ = nowMs;
                        intents.push_back(OrderIntent{
                            OrderIntentKind::PlaceLimit, Side::Buy, *observation.snapshot.bestAsk,
                            std::max<uint64_t>(8, qty / 2), TimeInForce::IOC
                        });
                    }

                    const int64_t price = std::max<int64_t>(1, fair - 1);
                    if (liveBid) {
                        intents.push_back(OrderIntent{
                            OrderIntentKind::Modify, Side::Buy, 0, 0, TimeInForce::GTC,
                            liveBid->orderId, price, qty
                        });
                    } else {
                        intents.push_back(OrderIntent{
                            OrderIntentKind::PlaceLimit, Side::Buy, price, qty, TimeInForce::GTC
                        });
                    }
                    if (liveAsk) {
                        intents.push_back(OrderIntent{OrderIntentKind::Cancel, Side::Sell, 0, 0, TimeInForce::GTC, liveAsk->orderId});
                    }
                }

                return intents;
            }

        private:
            SimulationVolatilityPreset preset_;
            uint64_t lastAggressionTimestampMs_ = 0;
        };

        template <typename T>
        void eraseByPointer(std::vector<T*>& items, T* item) {
            items.erase(std::remove(items.begin(), items.end(), item), items.end());
        }

    }

    MarketRuntime::MarketRuntime(std::string symbol, SimulationConfig simulationConfig)
        : symbol_(std::move(symbol)),
          simulationConfig_(simulationConfig),
          rng_(simulationConfig.seed) {
        createEngineService();
        rebuildAgents();
    }

    MarketRuntime::~MarketRuntime() {
        stop();
    }

    void MarketRuntime::createEngineService() {
        engineService_ = std::make_unique<EngineService>(symbol_);
        engineService_->setMarketDataSink(this);
    }

    void MarketRuntime::rebuildAgents() {
        std::lock_guard<std::mutex> lock(agentsMutex_);
        agents_.clear();

        auto schedule = [this](uint64_t baseMs) {
            if (baseMs == 0) {
                return uint64_t{0};
            }
            std::uniform_int_distribution<uint64_t> dist(0, baseMs / 2 + 1);
            return dist(rng_);
        };

        for (size_t i = 0; i < simulationConfig_.marketMakerCount; ++i) {
            auto agent = std::make_unique<PassiveMarketMakerAgent>(simulationConfig_.volatility);
            const auto base = agent->wakeIntervalMs();
            agents_.push_back(AgentSlot{std::move(agent), 1000 + static_cast<uint64_t>(i), schedule(base)});
        }

        for (size_t i = 0; i < simulationConfig_.momentumCount; ++i) {
            auto agent = std::make_unique<AggressiveMomentumAgent>(simulationConfig_.volatility);
            const auto base = agent->wakeIntervalMs();
            agents_.push_back(AgentSlot{std::move(agent), 2000 + static_cast<uint64_t>(i), schedule(base)});
        }

        for (size_t i = 0; i < simulationConfig_.meanReversionCount; ++i) {
            auto agent = std::make_unique<MeanReversionAgent>(simulationConfig_.volatility);
            const auto base = agent->wakeIntervalMs();
            agents_.push_back(AgentSlot{std::move(agent), 3000 + static_cast<uint64_t>(i), schedule(base)});
        }

        for (const auto& descriptor : customAgentFactories_) {
            auto agent = descriptor.factory();
            const auto base = agent->wakeIntervalMs();
            agents_.push_back(AgentSlot{std::move(agent), descriptor.clientId, schedule(base)});
        }
    }

    void MarketRuntime::start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        stopRequested_.store(false);
        simulationPaused_.store(false);
        engineService_->start();

        if (simulationConfig_.enabled) {
            simulationThread_ = std::thread([this]() {
                simulationLoop();
            });
        }
    }

    void MarketRuntime::stop() {
        if (!running_.load()) {
            return;
        }

        stopRequested_.store(true);
        simulationPaused_.store(false);

        if (simulationThread_.joinable()) {
            simulationThread_.join();
        }

        if (engineService_) {
            engineService_->stopReplay();
            engineService_->stop();
        }

        running_.store(false);
    }

    bool MarketRuntime::isReplayActive() const {
        return engineService_ ? engineService_->isReplayActive() : false;
    }

    uint64_t MarketRuntime::allocateOrderId() {
        return engineService_->allocateOrderId();
    }

    ExecutionResult MarketRuntime::submitOrder(Order order) {
        return submitOrder(std::move(order), 0);
    }

    ExecutionResult MarketRuntime::submitOrder(Order order, uint64_t entryTimestampNs) {
        return engineService_->submitOrder(std::move(order), entryTimestampNs);
    }

    L2Snapshot MarketRuntime::getSnapshot(size_t depth) {
        return engineService_->getSnapshot(depth);
    }

    MarketRuntimeState MarketRuntime::getState() {
        auto engineState = engineService_->getState();

        MarketRuntimeState state;
        state.running = engineState.running;
        state.replayActive = engineState.replayActive;
        state.symbol = engineState.symbol;
        state.sequence = engineState.sequence;
        state.nextOrderId = engineState.nextOrderId;
        state.tradeCount = engineState.tradeCount;
        state.totalVolume = engineState.totalVolume;
        state.orderCount = engineState.orderCount;
        state.bidLevels = engineState.bidLevels;
        state.askLevels = engineState.askLevels;
        state.clientCount = engineState.clientCount;
        state.simulationEnabled = simulationConfig_.enabled;
        state.simulationRunning = simulationConfig_.enabled && running_.load();
        state.simulationPaused = simulationPaused_.load();
        state.clockMode = simulationClockModeToString(simulationConfig_.clockMode);
        state.simulationSpeed = simulationConfig_.speed;
        state.volatilityPreset = simulationVolatilityToString(simulationConfig_.volatility);
        state.marketMakerCount = simulationConfig_.marketMakerCount;
        state.momentumCount = simulationConfig_.momentumCount;
        state.meanReversionCount = simulationConfig_.meanReversionCount;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state.simulationTimestamp = simulationTimestampMs_;
            state.realizedVolatilityBps = realizedVolatilityBps_;
            state.averageSpread = averageSpread_;
        }

        return state;
    }

    HeadlessSummary MarketRuntime::getHeadlessSummary() {
        auto state = getState();
        HeadlessSummary summary;
        summary.simulationTimestamp = state.simulationTimestamp;
        summary.tradeCount = state.tradeCount;
        summary.totalVolume = state.totalVolume;
        summary.orderCount = state.orderCount;
        summary.realizedVolatilityBps = state.realizedVolatilityBps;
        summary.averageSpread = state.averageSpread;

        std::lock_guard<std::mutex> lock(stateMutex_);
        summary.lastMidPrice = lastStats_.midPrice;
        return summary;
    }

    bool MarketRuntime::startReplay(const std::string& inputFile, double speed, bool loop, uint64_t loopPauseMs) {
        return engineService_->startReplay(inputFile, speed, loop, loopPauseMs);
    }

    void MarketRuntime::stopReplay() {
        engineService_->stopReplay();
    }

    bool MarketRuntime::applyControl(const SimulationControl& control) {
        if (!simulationConfig_.enabled) {
            return false;
        }

        if (control.action == "pause") {
            simulationPaused_.store(true);
            publishSimulationState(true);
            return true;
        }
        if (control.action == "resume") {
            simulationPaused_.store(false);
            publishSimulationState(true);
            return true;
        }
        if (control.action == "restart") {
            restartRuntime();
            publishSimulationState(true);
            return true;
        }
        if (control.action == "set_volatility") {
            simulationConfig_.volatility = simulationVolatilityFromString(control.volatility);
            rebuildAgents();
            publishSimulationState(true);
            return true;
        }
        return false;
    }

    void MarketRuntime::addSubscriber(MarketDataSink* sink) {
        if (!sink) {
            return;
        }

        std::lock_guard<std::mutex> lock(subscribersMutex_);
        if (std::find(subscribers_.begin(), subscribers_.end(), sink) == subscribers_.end()) {
            subscribers_.push_back(sink);
        }
    }

    void MarketRuntime::removeSubscriber(MarketDataSink* sink) {
        std::lock_guard<std::mutex> lock(subscribersMutex_);
        eraseByPointer(subscribers_, sink);
    }

    void MarketRuntime::addCustomAgentFactory(SimulationAgentFactory factory, uint64_t clientId) {
        if (!factory) {
            return;
        }
        customAgentFactories_.push_back(AgentDescriptor{std::move(factory), clientId});
        rebuildAgents();
    }

    void MarketRuntime::fanout(const std::function<void(MarketDataSink*)>& fn) {
        std::vector<MarketDataSink*> subscribers;
        {
            std::lock_guard<std::mutex> lock(subscribersMutex_);
            subscribers = subscribers_;
        }

        for (auto* sink : subscribers) {
            fn(sink);
        }
    }

    void MarketRuntime::onBookDelta(const BookDelta& delta) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPublishedSequence_ = std::max(lastPublishedSequence_, delta.sequence);
        }
        fanout([&](MarketDataSink* sink) { sink->onBookDelta(delta); });
    }

    void MarketRuntime::onTradeEvent(const TradeEvent& trade) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPublishedSequence_ = std::max(lastPublishedSequence_, trade.sequence);
            recentTrades_.insert(recentTrades_.begin(), trade);
            if (recentTrades_.size() > 64) {
                recentTrades_.resize(64);
            }

            auto reduceOrder = [&](uint64_t orderId, uint64_t fillQty) {
                auto it = liveOrdersById_.find(orderId);
                if (it == liveOrdersById_.end()) {
                    return;
                }
                if (it->second.quantity <= fillQty) {
                    liveOrdersById_.erase(it);
                } else {
                    it->second.quantity -= fillQty;
                }
            };

            reduceOrder(trade.buyOrderId, trade.quantity);
            reduceOrder(trade.sellOrderId, trade.quantity);
        }

        fanout([&](MarketDataSink* sink) { sink->onTradeEvent(trade); });
    }

    void MarketRuntime::onStatsEvent(const StatsEvent& stats) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPublishedSequence_ = std::max(lastPublishedSequence_, stats.sequence);
            lastStats_ = stats;
            updateMetricsFromStats(stats);
        }
        fanout([&](MarketDataSink* sink) { sink->onStatsEvent(stats); });
    }

    void MarketRuntime::onPnLEvent(const PnLEvent& pnl) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPublishedSequence_ = std::max(lastPublishedSequence_, pnl.sequence);
            pnlByClient_[pnl.clientId] = pnl;
        }
        fanout([&](MarketDataSink* sink) { sink->onPnLEvent(pnl); });
    }

    void MarketRuntime::onExecutionEvent(const ExecutionEvent& execution) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPublishedSequence_ = std::max(lastPublishedSequence_, execution.sequence);
        }
        fanout([&](MarketDataSink* sink) { sink->onExecutionEvent(execution); });
    }

    void MarketRuntime::updateMetricsFromStats(const StatsEvent& stats) {
        if (stats.spread > 0) {
            if (averageSpread_ <= 0.0) {
                averageSpread_ = static_cast<double>(stats.spread);
            } else {
                averageSpread_ = averageSpread_ * 0.9 + static_cast<double>(stats.spread) * 0.1;
            }
        }

        if (stats.midPrice > 0) {
            midHistory_.push_back(stats.midPrice);
            if (midHistory_.size() > 64) {
                midHistory_.erase(midHistory_.begin());
            }

            if (midHistory_.size() >= 8) {
                std::vector<double> returns;
                returns.reserve(midHistory_.size() - 1);
                for (size_t i = 1; i < midHistory_.size(); ++i) {
                    if (midHistory_[i - 1] <= 0) {
                        continue;
                    }
                    returns.push_back(
                        (static_cast<double>(midHistory_[i]) - static_cast<double>(midHistory_[i - 1])) /
                        static_cast<double>(midHistory_[i - 1]));
                }

                if (!returns.empty()) {
                    double mean = 0.0;
                    for (double value : returns) {
                        mean += value;
                    }
                    mean /= static_cast<double>(returns.size());

                    double variance = 0.0;
                    for (double value : returns) {
                        const auto diff = value - mean;
                        variance += diff * diff;
                    }
                    variance /= static_cast<double>(returns.size());
                    realizedVolatilityBps_ = std::sqrt(variance) * 10000.0;
                }
            }
        }
    }

    SimulationObservation MarketRuntime::buildObservation(uint64_t clientId) {
        SimulationObservation observation;
        observation.snapshot = engineService_->getSnapshot(20);

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            observation.recentTrades = recentTrades_;
            observation.stats = lastStats_;

            auto pnlIt = pnlByClient_.find(clientId);
            if (pnlIt != pnlByClient_.end()) {
                observation.ownPnL = pnlIt->second;
            }

            for (const auto& [orderId, record] : liveOrdersById_) {
                if (record.clientId != clientId) {
                    continue;
                }
                observation.liveOrders.push_back(SimulatedOrderInfo{
                    orderId,
                    record.side,
                    record.price,
                    record.quantity
                });
            }

            observation.environment.latentFairValue = latentFairValue_;
            observation.environment.momentumBurst = momentumBurst_;
            observation.environment.momentumDirection = momentumDirection_;
            observation.environment.realizedVolatilityBps = realizedVolatilityBps_;
            observation.environment.averageSpread = averageSpread_;
            observation.environment.volatilityPreset = simulationVolatilityToString(simulationConfig_.volatility);
            observation.environment.simulationTimestamp = simulationTimestampMs_;
        }

        if (observation.stats.timestamp == 0) {
            observation.stats.symbol = symbol_;
            observation.stats.midPrice = observation.snapshot.midPrice;
            observation.stats.spread = observation.snapshot.spread;
            observation.stats.bestBid = observation.snapshot.bestBid;
            observation.stats.bestAsk = observation.snapshot.bestAsk;
            observation.stats.bidLevels = observation.snapshot.bids.size();
            observation.stats.askLevels = observation.snapshot.asks.size();
            observation.stats.orderCount = observation.snapshot.bids.size() + observation.snapshot.asks.size();
        }

        return observation;
    }

    Order MarketRuntime::translateIntent(const OrderIntent& intent, uint64_t clientId) {
        Order order;
        order.clientId = clientId;

        switch (intent.kind) {
            case OrderIntentKind::PlaceLimit:
                order.id = allocateOrderId();
                order.orderType = OrderType::Limit;
                order.side = intent.side;
                order.price = intent.price;
                order.quantity = intent.quantity;
                order.tif = intent.tif;
                break;
            case OrderIntentKind::PlaceMarket:
                order.id = allocateOrderId();
                order.orderType = OrderType::Market;
                order.side = intent.side;
                order.quantity = intent.quantity;
                order.tif = TimeInForce::IOC;
                break;
            case OrderIntentKind::Cancel:
                order.id = intent.orderId;
                order.orderType = OrderType::Cancel;
                order.side = intent.side;
                break;
            case OrderIntentKind::Modify:
                order.id = allocateOrderId();
                order.orderType = OrderType::Modify;
                order.side = intent.side;
                order.targetOrderId = intent.orderId;
                order.newPrice = intent.newPrice;
                order.newQuantity = intent.newQuantity;
                break;
            case OrderIntentKind::None:
            default:
                break;
        }

        return order;
    }

    void MarketRuntime::applyExecutionToLiveOrders(const Order& order, const ExecutionResult& result) {
        std::lock_guard<std::mutex> lock(stateMutex_);

        auto upsertLiveOrder = [&](uint64_t orderId, Side side, int64_t price, uint64_t quantity, uint64_t clientId) {
            if (quantity == 0) {
                liveOrdersById_.erase(orderId);
                return;
            }
            liveOrdersById_[orderId] = LiveOrderRecord{orderId, clientId, side, price, quantity};
        };

        switch (order.orderType) {
            case OrderType::Limit:
                if ((result.status == ExecutionStatus::Resting ||
                     (result.status == ExecutionStatus::PartialFill && order.tif != TimeInForce::IOC)) &&
                    result.remainingQuantity > 0) {
                    upsertLiveOrder(order.id, order.side, order.price, result.remainingQuantity, order.clientId);
                } else {
                    liveOrdersById_.erase(order.id);
                }
                break;
            case OrderType::Market:
                liveOrdersById_.erase(order.id);
                break;
            case OrderType::Cancel:
                liveOrdersById_.erase(order.id);
                break;
            case OrderType::Modify:
                if ((result.status == ExecutionStatus::Modified || result.status == ExecutionStatus::PartialFill) &&
                    result.remainingQuantity > 0) {
                    const int64_t nextPrice = order.newPrice > 0 ? order.newPrice : order.price;
                    upsertLiveOrder(order.targetOrderId, order.side, nextPrice, result.remainingQuantity, order.clientId);
                } else {
                    liveOrdersById_.erase(order.targetOrderId);
                }
                break;
        }
    }

    void MarketRuntime::advanceEnvironment(uint64_t stepMs) {
        const auto params = paramsForPreset(simulationConfig_.volatility);
        std::normal_distribution<double> noise(0.0, params.fairValueSigma);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);

        std::lock_guard<std::mutex> lock(stateMutex_);
        simulationTimestampMs_ += stepMs;

        if (latentFairValue_ <= 0) {
            latentFairValue_ = lastStats_.midPrice > 0 ? lastStats_.midPrice : 100;
        }

        const int64_t anchorPrice =
            lastStats_.midPrice > 0
                ? lastStats_.midPrice
                : (lastStats_.bestBid && lastStats_.bestAsk
                    ? (*lastStats_.bestBid + *lastStats_.bestAsk) / 2
                    : 100);
        const int64_t noiseTicks = std::clamp(
            static_cast<int64_t>(std::llround(noise(rng_))),
            -params.maxNoiseTicks,
            params.maxNoiseTicks);
        const int64_t pullTicks = static_cast<int64_t>(std::llround(
            static_cast<double>(anchorPrice - latentFairValue_) * params.fairValuePull));

        latentFairValue_ += pullTicks + noiseTicks;
        latentFairValue_ = std::max<int64_t>(1, latentFairValue_);

        if (burstRemainingMs_ > 0) {
            latentFairValue_ += momentumDirection_ * params.burstStepTicks;
            burstRemainingMs_ = burstRemainingMs_ > stepMs ? burstRemainingMs_ - stepMs : 0;
            momentumBurst_ = burstRemainingMs_ > 0;
            if (!momentumBurst_) {
                momentumDirection_ = 0;
            }
        } else if (uniform(rng_) < params.burstProbabilityPerStep) {
            momentumBurst_ = true;
            momentumDirection_ = uniform(rng_) < 0.5 ? -1 : 1;
            burstRemainingMs_ = params.burstDurationMs;
        }
    }

    void MarketRuntime::maybeWakeAgents() {
        uint64_t currentSimTime = 0;
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            currentSimTime = simulationTimestampMs_;
        }

        std::lock_guard<std::mutex> agentsLock(agentsMutex_);

        for (auto& slot : agents_) {
            size_t catchUpWakes = 0;
            while (currentSimTime >= slot.nextWakeMs && catchUpWakes < 3) {
                auto observation = buildObservation(slot.clientId);
                auto intents = slot.agent->onWake(observation);
                for (const auto& intent : intents) {
                    if (intent.kind == OrderIntentKind::None) {
                        continue;
                    }

                    auto order = translateIntent(intent, slot.clientId);
                    auto result = engineService_->submitOrder(order);
                    applyExecutionToLiveOrders(order, result);
                }

                const uint64_t baseWake = std::max<uint64_t>(1, slot.agent->wakeIntervalMs());
                std::uniform_int_distribution<uint64_t> dist(0, baseWake / 3 + 1);
                slot.nextWakeMs += baseWake + dist(rng_);
                ++catchUpWakes;
            }

            if (currentSimTime >= slot.nextWakeMs) {
                const uint64_t baseWake = std::max<uint64_t>(1, slot.agent->wakeIntervalMs());
                std::uniform_int_distribution<uint64_t> dist(baseWake / 4, baseWake / 2 + 1);
                slot.nextWakeMs = currentSimTime + dist(rng_);
            }
        }
    }

    void MarketRuntime::publishSimulationState(bool force) {
        SimulationStateEvent event;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (!force && simulationTimestampMs_ - lastSimStatePublishMs_ < simulationConfig_.publishIntervalMs) {
                return;
            }
            lastSimStatePublishMs_ = simulationTimestampMs_;
            event.sequence = lastPublishedSequence_;
            event.symbol = symbol_;
            event.enabled = simulationConfig_.enabled;
            event.running = running_.load();
            event.paused = simulationPaused_.load();
            event.clockMode = simulationClockModeToString(simulationConfig_.clockMode);
            event.speed = simulationConfig_.speed;
            event.volatility = simulationVolatilityToString(simulationConfig_.volatility);
            event.simulationTimestamp = simulationTimestampMs_;
            event.marketMakerCount = simulationConfig_.marketMakerCount;
            event.momentumCount = simulationConfig_.momentumCount;
            event.meanReversionCount = simulationConfig_.meanReversionCount;
            event.realizedVolatilityBps = realizedVolatilityBps_;
            event.averageSpread = averageSpread_;
        }

        fanout([&](MarketDataSink* sink) { sink->onSimulationState(event); });
    }

    void MarketRuntime::simulationLoop() {
        const uint64_t stepMs = std::max<uint64_t>(20, simulationConfig_.stepMs);
        while (!stopRequested_.load()) {
            if (simulationPaused_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            advanceEnvironment(stepMs);
            maybeWakeAgents();
            publishSimulationState(false);

            const double speed = std::max(0.1, simulationConfig_.speed);
            uint64_t realSleepMs = stepMs;
            if (simulationConfig_.clockMode == SimulationClockMode::Accelerated) {
                realSleepMs = static_cast<uint64_t>(std::max(1.0, std::round(static_cast<double>(stepMs) / speed)));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(realSleepMs));
        }
    }

    void MarketRuntime::restartRuntime() {
        const bool wasRunning = running_.load();
        stop();

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            recentTrades_.clear();
            lastStats_ = StatsEvent{};
            pnlByClient_.clear();
            liveOrdersById_.clear();
            midHistory_.clear();
            simulationTimestampMs_ = 0;
            latentFairValue_ = 100;
            momentumBurst_ = false;
            momentumDirection_ = 0;
            burstRemainingMs_ = 0;
            averageSpread_ = 0.0;
            realizedVolatilityBps_ = 0.0;
            lastPublishedSequence_ = 0;
            lastSimStatePublishMs_ = 0;
        }

        createEngineService();
        rebuildAgents();

        if (wasRunning) {
            start();
        }
    }

    uint64_t MarketRuntime::steadyNowNs() {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    }

}
