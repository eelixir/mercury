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

        struct DesiredQuote {
            Side side = Side::Buy;
            int64_t price = 0;
            uint64_t quantity = 0;
        };

        double clamp01(double value) {
            return std::max(0.0, std::min(1.0, value));
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

        bool isMomentumClient(uint64_t clientId) {
            const auto localId = clientId % 10000;
            return localId >= 2000 && localId < 3000;
        }

        double estimateFillProbability(const SimulatedOrderInfo& order,
                                       const L2Snapshot& snapshot,
                                       double imbalance,
                                       double toxicity) {
            const double queueFactor = 1.0 / (1.0 + static_cast<double>(order.ordersAhead));
            const double depthFactor = order.quantityAhead == 0
                ? 1.0
                : 1.0 / (1.0 + static_cast<double>(order.quantityAhead) /
                                  static_cast<double>(std::max<uint64_t>(1, order.quantity)));
            const double sideBias = order.side == Side::Buy ? imbalance : -imbalance;
            double touchBoost = 0.0;
            if ((order.side == Side::Buy && snapshot.bestBid == order.price) ||
                (order.side == Side::Sell && snapshot.bestAsk == order.price)) {
                touchBoost = 0.12;
            }

            return clamp01(0.08 + queueFactor * 0.48 + depthFactor * 0.20 +
                           sideBias * 0.12 + touchBoost - toxicity * 0.15);
        }

        std::vector<const SimulatedOrderInfo*> collectOrdersBySide(const std::vector<SimulatedOrderInfo>& orders, Side side) {
            std::vector<const SimulatedOrderInfo*> filtered;
            filtered.reserve(orders.size());

            for (const auto& order : orders) {
                if (order.side == side) {
                    filtered.push_back(&order);
                }
            }

            std::sort(filtered.begin(), filtered.end(), [side](const SimulatedOrderInfo* left, const SimulatedOrderInfo* right) {
                if (left->price == right->price) {
                    return left->queuePosition < right->queuePosition;
                }
                return side == Side::Buy ? left->price > right->price : left->price < right->price;
            });

            return filtered;
        }

        std::vector<OrderIntent> reconcileQuotes(const std::vector<const SimulatedOrderInfo*>& liveOrders,
                                                 const std::vector<DesiredQuote>& desiredQuotes) {
            std::vector<OrderIntent> intents;
            const size_t shared = std::min(liveOrders.size(), desiredQuotes.size());

            for (size_t i = 0; i < shared; ++i) {
                const auto* live = liveOrders[i];
                const auto& desired = desiredQuotes[i];
                if (live->price != desired.price || live->quantity != desired.quantity) {
                    intents.push_back(OrderIntent{
                        OrderIntentKind::Modify,
                        desired.side,
                        0,
                        0,
                        TimeInForce::GTC,
                        live->orderId,
                        desired.price,
                        desired.quantity
                    });
                }
            }

            for (size_t i = shared; i < liveOrders.size(); ++i) {
                intents.push_back(OrderIntent{
                    OrderIntentKind::Cancel,
                    liveOrders[i]->side,
                    0,
                    0,
                    TimeInForce::GTC,
                    liveOrders[i]->orderId
                });
            }

            for (size_t i = shared; i < desiredQuotes.size(); ++i) {
                intents.push_back(OrderIntent{
                    OrderIntentKind::PlaceLimit,
                    desiredQuotes[i].side,
                    desiredQuotes[i].price,
                    desiredQuotes[i].quantity,
                    TimeInForce::GTC
                });
            }

            return intents;
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
                const auto fair = observation.environment.latentFairValue > 0
                    ? observation.environment.latentFairValue
                    : fallbackReferencePrice(observation);
                if (fair <= 0) {
                    return {};
                }

                const auto params = paramsForPreset(preset_);
                const int64_t inventory = observation.ownPnL ? observation.ownPnL->netPosition : 0;
                const double toxicity = clamp01(observation.environment.toxicityScore);
                const int64_t toxicityAdjust = static_cast<int64_t>(std::llround(toxicity * 4.0)) +
                                               (observation.environment.momentumBurst ? 1 : 0);
                const int64_t inventoryAdjust = std::min<int64_t>(4, std::abs(inventory) / 40);
                const int64_t spread = static_cast<int64_t>(params.marketMakerSpread) + toxicityAdjust + inventoryAdjust;
                const int64_t skew = inventory / 50;
                const uint64_t minQuoteQty = std::max<uint64_t>(10, params.marketMakerQuoteQty / 4);
                const size_t quoteLevels = preset_ == SimulationVolatilityPreset::High
                    ? 4
                    : (preset_ == SimulationVolatilityPreset::Low ? 2 : 3);
                const int64_t rawBid = std::max<int64_t>(1, fair - spread / 2 - skew);
                const int64_t rawAsk = std::max<int64_t>(rawBid + 1, fair + spread / 2 - skew);
                int64_t bestBidPrice = rawBid;
                int64_t bestAskPrice = rawAsk;
                const int64_t touchOffset = std::min<int64_t>(4, 1 + toxicityAdjust);

                if (observation.snapshot.bestBid.has_value()) {
                    bestBidPrice = std::max(bestBidPrice, *observation.snapshot.bestBid - touchOffset);
                }
                if (observation.snapshot.bestAsk.has_value()) {
                    bestAskPrice = std::min(bestAskPrice, *observation.snapshot.bestAsk + touchOffset);
                }
                if (bestAskPrice <= bestBidPrice) {
                    bestAskPrice = bestBidPrice + 1;
                }

                double bidInventoryScale = 1.0;
                double askInventoryScale = 1.0;
                if (inventory > 0) {
                    bidInventoryScale = inventory > 120 ? 0.35 : 0.65;
                    askInventoryScale = 1.20;
                } else if (inventory < 0) {
                    askInventoryScale = -inventory > 120 ? 0.35 : 0.65;
                    bidInventoryScale = 1.20;
                }

                std::vector<DesiredQuote> desiredBids;
                std::vector<DesiredQuote> desiredAsks;
                desiredBids.reserve(quoteLevels);
                desiredAsks.reserve(quoteLevels);

                for (size_t level = 0; level < quoteLevels; ++level) {
                    const uint64_t levelBaseQty = std::max<uint64_t>(
                        minQuoteQty,
                        params.marketMakerQuoteQty / static_cast<uint64_t>(level + 1));
                    const uint64_t bidQty = std::max<uint64_t>(
                        minQuoteQty,
                        static_cast<uint64_t>(std::llround(static_cast<double>(levelBaseQty) * bidInventoryScale)));
                    const uint64_t askQty = std::max<uint64_t>(
                        minQuoteQty,
                        static_cast<uint64_t>(std::llround(static_cast<double>(levelBaseQty) * askInventoryScale)));

                    desiredBids.push_back(DesiredQuote{
                        Side::Buy,
                        std::max<int64_t>(1, bestBidPrice - static_cast<int64_t>(level)),
                        bidQty
                    });
                    desiredAsks.push_back(DesiredQuote{
                        Side::Sell,
                        std::max<int64_t>(bestBidPrice + 1, bestAskPrice + static_cast<int64_t>(level)),
                        askQty
                    });
                }

                auto bidOrders = collectOrdersBySide(observation.liveOrders, Side::Buy);
                auto askOrders = collectOrdersBySide(observation.liveOrders, Side::Sell);

                if (!bidOrders.empty()) {
                    const auto* topBid = bidOrders.front();
                    if (topBid->atTouch &&
                        topBid->fillProbability < 0.20 &&
                        topBid->ordersAhead >= 2 &&
                        observation.snapshot.bestAsk.has_value() &&
                        desiredBids.front().price + 1 < *observation.snapshot.bestAsk &&
                        toxicity < 0.75) {
                        desiredBids.front().price = std::max<int64_t>(desiredBids.front().price, topBid->price + 1);
                    }
                }
                if (!askOrders.empty()) {
                    const auto* topAsk = askOrders.front();
                    if (topAsk->atTouch &&
                        topAsk->fillProbability < 0.20 &&
                        topAsk->ordersAhead >= 2 &&
                        observation.snapshot.bestBid.has_value() &&
                        desiredAsks.front().price - 1 > *observation.snapshot.bestBid &&
                        toxicity < 0.75) {
                        desiredAsks.front().price = std::min<int64_t>(desiredAsks.front().price, topAsk->price - 1);
                    }
                }

                if (desiredAsks.front().price <= desiredBids.front().price) {
                    desiredAsks.front().price = desiredBids.front().price + 1;
                }

                auto intents = reconcileQuotes(bidOrders, desiredBids);
                auto askIntents = reconcileQuotes(askOrders, desiredAsks);
                intents.insert(intents.end(), askIntents.begin(), askIntents.end());
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

        // PoissonFlowAgent emits limit, cancel, and marketable intents driven by
        // three independent Poisson processes whose rates come from the per-symbol
        // RegimeManager. Order sizes are drawn from a Pareto distribution so the
        // flow mixes "retail" noise with rare "whale" prints.
        class PoissonFlowAgent final : public SimulationAgent {
        public:
            PoissonFlowAgent(SimulationVolatilityPreset preset, uint32_t seed)
                : preset_(preset), rng_(seed) {}

            std::string name() const override { return "PoissonFlow"; }

            uint64_t wakeIntervalMs() const override {
                // Short ticks so the discretised Poisson sampling stays close to
                // the continuous-time ideal. Per-event emission is governed by
                // the regime-scaled lambdas, not this cadence.
                switch (preset_) {
                    case SimulationVolatilityPreset::Low:  return 120;
                    case SimulationVolatilityPreset::High: return 50;
                    case SimulationVolatilityPreset::Normal:
                    default:                               return 80;
                }
            }

            std::vector<OrderIntent> onWake(const SimulationObservation& observation) override {
                std::vector<OrderIntent> intents;
                const uint64_t now = observation.environment.simulationTimestamp;
                const uint64_t dt = lastWakeMs_ == 0 || now <= lastWakeMs_
                    ? wakeIntervalMs()
                    : now - lastWakeMs_;
                lastWakeMs_ = now;

                const auto intensity = observation.environment.intensity;
                const auto dispersion = observation.environment.dispersion;
                const int64_t reference = fallbackReferencePrice(observation);
                if (reference <= 0) {
                    return intents;
                }

                // 1. Cancels. Target the agent's own resting orders so the flow
                //    composes cleanly with market-maker quoting behaviour.
                const uint32_t cancelCount =
                    RegimeManager::samplePoissonCount(intensity.cancelLambda, dt, rng_);
                if (cancelCount > 0 && !observation.liveOrders.empty()) {
                    std::uniform_int_distribution<size_t> pick(0, observation.liveOrders.size() - 1);
                    const size_t n = std::min<size_t>(cancelCount, observation.liveOrders.size());
                    for (size_t i = 0; i < n; ++i) {
                        const auto& target = observation.liveOrders[pick(rng_)];
                        intents.push_back(OrderIntent{
                            OrderIntentKind::Cancel, target.side, 0, 0, TimeInForce::GTC, target.orderId
                        });
                    }
                }

                std::uniform_real_distribution<double> sideRoll(0.0, 1.0);

                // 2. Marketable flow — IOC orders that cross the spread. Only
                //    emit on a side where there is opposing liquidity.
                const uint32_t mktCount =
                    RegimeManager::samplePoissonCount(intensity.marketableLambda, dt, rng_);
                for (uint32_t i = 0; i < mktCount; ++i) {
                    const bool buy = sideRoll(rng_) < 0.5;
                    if (buy && !observation.snapshot.bestAsk.has_value()) continue;
                    if (!buy && !observation.snapshot.bestBid.has_value()) continue;
                    const uint64_t qty = RegimeManager::sampleOrderSize(dispersion, rng_);
                    intents.push_back(OrderIntent{
                        OrderIntentKind::PlaceMarket,
                        buy ? Side::Buy : Side::Sell,
                        0,
                        qty,
                        TimeInForce::IOC
                    });
                }

                // 3. Limit arrivals — resting orders placed near top-of-book
                //    with a small random displacement into the book.
                const uint32_t limitCount =
                    RegimeManager::samplePoissonCount(intensity.limitLambda, dt, rng_);
                for (uint32_t i = 0; i < limitCount; ++i) {
                    const bool buy = sideRoll(rng_) < 0.5;
                    std::uniform_int_distribution<int64_t> offsetDist(0, 4);
                    const int64_t offset = offsetDist(rng_);
                    int64_t price = reference;
                    if (buy) {
                        const int64_t anchor = observation.snapshot.bestBid.value_or(reference - 1);
                        price = std::max<int64_t>(1, anchor - offset);
                    } else {
                        const int64_t anchor = observation.snapshot.bestAsk.value_or(reference + 1);
                        price = std::max<int64_t>(1, anchor + offset);
                    }
                    const uint64_t qty = RegimeManager::sampleOrderSize(dispersion, rng_);
                    intents.push_back(OrderIntent{
                        OrderIntentKind::PlaceLimit,
                        buy ? Side::Buy : Side::Sell,
                        price,
                        qty,
                        TimeInForce::GTC
                    });
                }

                return intents;
            }

            void reset() override {
                lastWakeMs_ = 0;
            }

        private:
            SimulationVolatilityPreset preset_;
            std::mt19937 rng_;
            uint64_t lastWakeMs_ = 0;
        };

        template <typename T>
        void eraseByPointer(std::vector<T*>& items, T* item) {
            items.erase(std::remove(items.begin(), items.end(), item), items.end());
        }

    }

    MarketRuntime::MarketRuntime(std::vector<std::string> symbols, SimulationConfig simulationConfig)
        : symbols_(std::move(symbols)),
          simulationConfig_(simulationConfig),
          rng_(simulationConfig.seed) {
        if (symbols_.empty()) {
            symbols_.push_back("SIM");
        }
        for (const auto& sym : symbols_) {
            PerSymbolEnvironment env;
            env.regime.setPreset(simulationConfig_.volatility);
            envs_.emplace(sym, std::move(env));
        }
        createEngineService();
        rebuildAgents();
    }

    MarketRuntime::MarketRuntime(std::string symbol, SimulationConfig simulationConfig)
        : MarketRuntime(std::vector<std::string>{std::move(symbol)}, simulationConfig) {}

    MarketRuntime::~MarketRuntime() {
        stop();
    }

    void MarketRuntime::createEngineService() {
        engineService_ = std::make_unique<EngineService>(symbols_);
        engineService_->setMarketDataSink(this);
    }

    void MarketRuntime::rebuildAgents() {
        std::lock_guard<std::mutex> lock(agentsMutex_);
        agentsBySymbol_.clear();

        auto schedule = [this](uint64_t baseMs) {
            if (baseMs == 0) {
                return uint64_t{0};
            }
            std::uniform_int_distribution<uint64_t> dist(0, baseMs / 2 + 1);
            return dist(rng_);
        };

        for (size_t symIdx = 0; symIdx < symbols_.size(); ++symIdx) {
            const auto& sym = symbols_[symIdx];
            auto& agents = agentsBySymbol_[sym];
            const uint64_t symOffset = static_cast<uint64_t>(symIdx) * 10000;

            for (size_t i = 0; i < simulationConfig_.marketMakerCount; ++i) {
                auto agent = std::make_unique<PassiveMarketMakerAgent>(simulationConfig_.volatility);
                const auto base = agent->wakeIntervalMs();
                agents.push_back(AgentSlot{std::move(agent), symOffset + 1000 + static_cast<uint64_t>(i), schedule(base)});
            }

            for (size_t i = 0; i < simulationConfig_.momentumCount; ++i) {
                auto agent = std::make_unique<AggressiveMomentumAgent>(simulationConfig_.volatility);
                const auto base = agent->wakeIntervalMs();
                agents.push_back(AgentSlot{std::move(agent), symOffset + 2000 + static_cast<uint64_t>(i), schedule(base)});
            }

            for (size_t i = 0; i < simulationConfig_.meanReversionCount; ++i) {
                auto agent = std::make_unique<MeanReversionAgent>(simulationConfig_.volatility);
                const auto base = agent->wakeIntervalMs();
                agents.push_back(AgentSlot{std::move(agent), symOffset + 3000 + static_cast<uint64_t>(i), schedule(base)});
            }

            for (size_t i = 0; i < simulationConfig_.noiseTraderCount; ++i) {
                const uint32_t childSeed =
                    static_cast<uint32_t>(simulationConfig_.seed) ^
                    static_cast<uint32_t>((symIdx + 1) * 0x9E3779B1u) ^
                    static_cast<uint32_t>((i + 1) * 0x85EBCA77u);
                auto agent = std::make_unique<PoissonFlowAgent>(simulationConfig_.volatility, childSeed);
                const auto base = agent->wakeIntervalMs();
                agents.push_back(AgentSlot{std::move(agent), symOffset + 4000 + static_cast<uint64_t>(i), schedule(base)});
            }

            for (const auto& descriptor : customAgentFactories_) {
                auto agent = descriptor.factory();
                const auto base = agent->wakeIntervalMs();
                agents.push_back(AgentSlot{std::move(agent), descriptor.clientId, schedule(base)});
            }
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
        return submitOrder(symbols_.front(), std::move(order), 0);
    }

    ExecutionResult MarketRuntime::submitOrder(Order order, uint64_t entryTimestampNs) {
        return submitOrder(symbols_.front(), std::move(order), entryTimestampNs);
    }

    ExecutionResult MarketRuntime::submitOrder(const std::string& symbol, Order order) {
        return submitOrder(symbol, std::move(order), 0);
    }

    ExecutionResult MarketRuntime::submitOrder(const std::string& symbol, Order order, uint64_t entryTimestampNs) {
        return engineService_->submitOrder(symbol, std::move(order), entryTimestampNs);
    }

    L2Snapshot MarketRuntime::getSnapshot(size_t depth) {
        return engineService_->getSnapshot(symbols_.front(), depth);
    }

    L2Snapshot MarketRuntime::getSnapshot(const std::string& symbol, size_t depth) {
        return engineService_->getSnapshot(symbol, depth);
    }

    MarketRuntimeState MarketRuntime::getState() {
        auto engineState = engineService_->getState();

        MarketRuntimeState state;
        state.running = engineState.running;
        state.replayActive = engineState.replayActive;
        state.symbol = engineState.symbols.empty() ? "SIM" : engineState.symbols.front();
        state.symbols = engineState.symbols;
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
        state.noiseTraderCount = simulationConfig_.noiseTraderCount;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state.simulationTimestamp = simulationTimestampMs_;
            const auto& primarySym = symbols_.front();
            auto envIt = envs_.find(primarySym);
            if (envIt != envs_.end()) {
                state.realizedVolatilityBps = envIt->second.realizedVolatilityBps;
                state.averageSpread = envIt->second.averageSpread;
                state.toxicityScore = envIt->second.toxicityScore;
                const auto intensity = envIt->second.regime.intensity();
                state.regime = marketRegimeToString(envIt->second.regime.regime());
                state.limitLambda = intensity.limitLambda;
                state.cancelLambda = intensity.cancelLambda;
                state.marketableLambda = intensity.marketableLambda;
            }
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
        auto envIt = envs_.find(symbols_.front());
        if (envIt != envs_.end()) {
            summary.lastMidPrice = envIt->second.lastStats.midPrice;
        }
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
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                for (auto& [sym, env] : envs_) {
                    env.regime.setPreset(simulationConfig_.volatility);
                }
            }
            rebuildAgents();
            publishSimulationState(true);
            return true;
        }
        if (control.action == "set_regime") {
            const auto regime = marketRegimeFromString(control.volatility);
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                for (auto& [sym, env] : envs_) {
                    env.regime.forceRegime(regime);
                }
            }
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
            auto& env = envs_[trade.symbol];
            env.recentTrades.insert(env.recentTrades.begin(), trade);
            if (env.recentTrades.size() > 64) {
                env.recentTrades.resize(64);
            }

            auto reduceOrder = [&](uint64_t orderId, uint64_t fillQty) {
                auto it = env.liveOrdersById.find(orderId);
                if (it == env.liveOrdersById.end()) {
                    return;
                }
                if (it->second.quantity <= fillQty) {
                    env.liveOrdersById.erase(it);
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
            envs_[stats.symbol].lastStats = stats;
            updateMetricsFromStats(stats.symbol, stats);
        }
        fanout([&](MarketDataSink* sink) { sink->onStatsEvent(stats); });
    }

    void MarketRuntime::onPnLEvent(const PnLEvent& pnl) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastPublishedSequence_ = std::max(lastPublishedSequence_, pnl.sequence);
            envs_[pnl.symbol].pnlByClient[pnl.clientId] = pnl;
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

    void MarketRuntime::updateMetricsFromStats(const std::string& symbol, const StatsEvent& stats) {
        auto& env = envs_[symbol];
        if (stats.spread > 0) {
            if (env.averageSpread <= 0.0) {
                env.averageSpread = static_cast<double>(stats.spread);
            } else {
                env.averageSpread = env.averageSpread * 0.9 + static_cast<double>(stats.spread) * 0.1;
            }
        }

        if (stats.midPrice > 0) {
            env.midHistory.push_back(stats.midPrice);
            if (env.midHistory.size() > 64) {
                env.midHistory.erase(env.midHistory.begin());
            }

            if (env.midHistory.size() >= 8) {
                std::vector<double> returns;
                returns.reserve(env.midHistory.size() - 1);
                for (size_t i = 1; i < env.midHistory.size(); ++i) {
                    if (env.midHistory[i - 1] <= 0) {
                        continue;
                    }
                    returns.push_back(
                        (static_cast<double>(env.midHistory[i]) - static_cast<double>(env.midHistory[i - 1])) /
                        static_cast<double>(env.midHistory[i - 1]));
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
                    env.realizedVolatilityBps = std::sqrt(variance) * 10000.0;
                }
            }
        }
    }

    SimulationObservation MarketRuntime::buildObservation(const std::string& symbol, uint64_t clientId) {
        SimulationObservation observation;
        observation.snapshot = engineService_->getSnapshot(symbol, 20);
        std::vector<LiveOrderRecord> ownOrders;

        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            auto& env = envs_[symbol];
            observation.recentTrades = env.recentTrades;
            observation.stats = env.lastStats;

            auto pnlIt = env.pnlByClient.find(clientId);
            if (pnlIt != env.pnlByClient.end()) {
                observation.ownPnL = pnlIt->second;
            }

            for (const auto& entry : env.liveOrdersById) {
                const auto& record = entry.second;
                if (record.clientId != clientId) {
                    continue;
                }
                ownOrders.push_back(record);
            }

            observation.environment.latentFairValue = env.latentFairValue;
            observation.environment.momentumBurst = env.momentumBurst;
            observation.environment.momentumDirection = env.momentumDirection;
            observation.environment.realizedVolatilityBps = env.realizedVolatilityBps;
            observation.environment.averageSpread = env.averageSpread;
            observation.environment.toxicityScore = env.toxicityScore;
            observation.environment.volatilityPreset = simulationVolatilityToString(simulationConfig_.volatility);
            observation.environment.simulationTimestamp = simulationTimestampMs_;
            observation.environment.regime = env.regime.regime();
            observation.environment.intensity = env.regime.intensity();
            observation.environment.dispersion = env.regime.dispersion();
        }

        std::vector<uint64_t> orderIds;
        orderIds.reserve(ownOrders.size());
        for (const auto& record : ownOrders) {
            orderIds.push_back(record.orderId);
        }
        const auto queuePositions = engineService_->getQueuePositions(symbol, orderIds);

        const double imbalance = topLevelImbalance(observation);
        for (const auto& record : ownOrders) {
            SimulatedOrderInfo info;
            info.orderId = record.orderId;
            info.side = record.side;
            info.price = record.price;
            info.quantity = record.quantity;
            info.atTouch =
                (record.side == Side::Buy && observation.snapshot.bestBid == record.price) ||
                (record.side == Side::Sell && observation.snapshot.bestAsk == record.price);

            auto queueIt = queuePositions.find(record.orderId);
            if (queueIt != queuePositions.end()) {
                info.queuePosition = queueIt->second.queueIndex;
                info.ordersAhead = queueIt->second.ordersAhead;
                info.quantityAhead = queueIt->second.quantityAhead;
            }

            info.fillProbability = estimateFillProbability(
                info,
                observation.snapshot,
                imbalance,
                observation.environment.toxicityScore);
            observation.liveOrders.push_back(info);
        }

        if (observation.stats.timestamp == 0) {
            observation.stats.symbol = symbol;
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

    void MarketRuntime::applyExecutionToLiveOrders(const std::string& symbol, const Order& order, const ExecutionResult& result) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& liveOrders = envs_[symbol].liveOrdersById;

        auto upsertLiveOrder = [&](uint64_t orderId, Side side, int64_t price, uint64_t quantity, uint64_t clientId) {
            if (quantity == 0) {
                liveOrders.erase(orderId);
                return;
            }
            liveOrders[orderId] = LiveOrderRecord{orderId, clientId, side, price, quantity};
        };

        switch (order.orderType) {
            case OrderType::Limit:
                if ((result.status == ExecutionStatus::Resting ||
                     (result.status == ExecutionStatus::PartialFill && order.tif != TimeInForce::IOC)) &&
                    result.remainingQuantity > 0) {
                    upsertLiveOrder(order.id, order.side, order.price, result.remainingQuantity, order.clientId);
                } else {
                    liveOrders.erase(order.id);
                }
                break;
            case OrderType::Market:
                liveOrders.erase(order.id);
                break;
            case OrderType::Cancel:
                liveOrders.erase(order.id);
                break;
            case OrderType::Modify:
                if ((result.status == ExecutionStatus::Modified || result.status == ExecutionStatus::PartialFill) &&
                    result.remainingQuantity > 0) {
                    const int64_t nextPrice = order.newPrice > 0 ? order.newPrice : order.price;
                    upsertLiveOrder(order.targetOrderId, order.side, nextPrice, result.remainingQuantity, order.clientId);
                } else {
                    liveOrders.erase(order.targetOrderId);
                }
                break;
        }
    }

    void MarketRuntime::advanceEnvironment(const std::string& symbol, uint64_t stepMs) {
        const auto params = paramsForPreset(simulationConfig_.volatility);
        std::normal_distribution<double> noise(0.0, params.fairValueSigma);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);

        std::lock_guard<std::mutex> lock(stateMutex_);
        auto& env = envs_[symbol];

        if (env.latentFairValue <= 0) {
            env.latentFairValue = env.lastStats.midPrice > 0 ? env.lastStats.midPrice : 100;
        }

        const int64_t anchorPrice =
            env.lastStats.midPrice > 0
                ? env.lastStats.midPrice
                : (env.lastStats.bestBid && env.lastStats.bestAsk
                    ? (*env.lastStats.bestBid + *env.lastStats.bestAsk) / 2
                    : 100);
        const int64_t noiseTicks = std::clamp(
            static_cast<int64_t>(std::llround(noise(rng_))),
            -params.maxNoiseTicks,
            params.maxNoiseTicks);
        const int64_t pullTicks = static_cast<int64_t>(std::llround(
            static_cast<double>(anchorPrice - env.latentFairValue) * params.fairValuePull));

        env.latentFairValue += pullTicks + noiseTicks;
        env.latentFairValue = std::max<int64_t>(1, env.latentFairValue);

        if (env.burstRemainingMs > 0) {
            env.latentFairValue += env.momentumDirection * params.burstStepTicks;
            env.burstRemainingMs = env.burstRemainingMs > stepMs ? env.burstRemainingMs - stepMs : 0;
            env.momentumBurst = env.burstRemainingMs > 0;
            if (!env.momentumBurst) {
                env.momentumDirection = 0;
            }
        } else if (uniform(rng_) < params.burstProbabilityPerStep) {
            env.momentumBurst = true;
            env.momentumDirection = uniform(rng_) < 0.5 ? -1 : 1;
            env.burstRemainingMs = params.burstDurationMs;
        }

        uint64_t displayedDepth = 0;
        const int64_t bestBid = env.lastStats.bestBid.value_or(std::max<int64_t>(1, env.latentFairValue - 1));
        const int64_t bestAsk = env.lastStats.bestAsk.value_or(env.latentFairValue + 1);
        for (const auto& entry : env.liveOrdersById) {
            const auto& record = entry.second;
            if (record.side == Side::Buy && record.price >= bestBid - 2) {
                displayedDepth += record.quantity;
            } else if (record.side == Side::Sell && record.price <= bestAsk + 2) {
                displayedDepth += record.quantity;
            }
        }

        uint64_t toxicFlow = 0;
        size_t inspectedTrades = 0;
        for (const auto& trade : env.recentTrades) {
            if (inspectedTrades++ >= 16) {
                break;
            }

            const bool momentumInvolved = isMomentumClient(trade.buyClientId) || isMomentumClient(trade.sellClientId);
            const bool sweepSized =
                displayedDepth > 0 &&
                trade.quantity >= std::max<uint64_t>(8, displayedDepth / 8);
            toxicFlow += momentumInvolved || sweepSized
                ? trade.quantity
                : std::max<uint64_t>(1, trade.quantity / 4);
        }

        double rawToxicity = displayedDepth > 0
            ? static_cast<double>(toxicFlow) / static_cast<double>(displayedDepth)
            : 0.0;
        if (env.momentumBurst) {
            rawToxicity += 0.25;
        }
        if (env.regime.regime() == MarketRegime::Stressed) {
            rawToxicity += 0.10;
        }
        env.toxicityScore = clamp01(env.toxicityScore * 0.82 + clamp01(rawToxicity) * 0.18);

        env.regime.observe(env.realizedVolatilityBps,
                           env.momentumBurst,
                           env.averageSpread,
                           stepMs);
    }

    void MarketRuntime::maybeWakeAgents() {
        uint64_t currentSimTime = 0;
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            currentSimTime = simulationTimestampMs_;
        }

        std::lock_guard<std::mutex> agentsLock(agentsMutex_);

        for (auto& [sym, agents] : agentsBySymbol_) {
            for (auto& slot : agents) {
                size_t catchUpWakes = 0;
                while (currentSimTime >= slot.nextWakeMs && catchUpWakes < 3) {
                    auto observation = buildObservation(sym, slot.clientId);
                    auto intents = slot.agent->onWake(observation);
                    for (const auto& intent : intents) {
                        if (intent.kind == OrderIntentKind::None) {
                            continue;
                        }

                        auto order = translateIntent(intent, slot.clientId);
                        auto result = engineService_->submitOrder(sym, order);
                        applyExecutionToLiveOrders(sym, order, result);
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
    }

    void MarketRuntime::publishSimulationState(bool force) {
        std::lock_guard<std::mutex> lock(stateMutex_);

        for (const auto& sym : symbols_) {
            auto& env = envs_[sym];
            if (!force && simulationTimestampMs_ - env.lastSimStatePublishMs < simulationConfig_.publishIntervalMs) {
                continue;
            }
            env.lastSimStatePublishMs = simulationTimestampMs_;

            SimulationStateEvent event;
            event.sequence = lastPublishedSequence_;
            event.symbol = sym;
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
            event.noiseTraderCount = simulationConfig_.noiseTraderCount;
            event.realizedVolatilityBps = env.realizedVolatilityBps;
            event.averageSpread = env.averageSpread;
            event.toxicityScore = env.toxicityScore;
            const auto intensity = env.regime.intensity();
            event.regime = marketRegimeToString(env.regime.regime());
            event.limitLambda = intensity.limitLambda;
            event.cancelLambda = intensity.cancelLambda;
            event.marketableLambda = intensity.marketableLambda;

            fanout([&](MarketDataSink* sink) { sink->onSimulationState(event); });
        }
    }

    void MarketRuntime::simulationLoop() {
        const uint64_t stepMs = std::max<uint64_t>(20, simulationConfig_.stepMs);
        while (!stopRequested_.load()) {
            if (simulationPaused_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                simulationTimestampMs_ += stepMs;
            }

            for (const auto& sym : symbols_) {
                advanceEnvironment(sym, stepMs);
            }
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
            for (auto& [sym, env] : envs_) {
                env = PerSymbolEnvironment{};
            }
            simulationTimestampMs_ = 0;
            lastPublishedSequence_ = 0;
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
