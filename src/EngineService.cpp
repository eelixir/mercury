#include "EngineService.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>

namespace Mercury {

    EngineService::EngineService(std::string symbol)
        : symbol_(std::move(symbol)) {
    }

    EngineService::~EngineService() {
        stopReplay();
        stop();
    }

    void EngineService::start() {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }

        stopRequested_.store(false);
        currentSequence_ = 0;

        engine_.setBookMutationCallback([this](const BookMutation& mutation) {
            handleBookMutation(mutation);
        });

        engine_.setTradeCallback([this](const Trade& trade) {
            handleTrade(trade);
        });

        pnlTracker_.setPnLCallback([this](const PnLSnapshot& snapshot) {
            handlePnL(snapshot);
        });

        engineThread_ = std::thread([this]() {
            engineLoop();
        });
    }

    void EngineService::stop() {
        if (!running_.load()) {
            return;
        }

        stopReplay();
        stopRequested_.store(true);
        queueCv_.notify_all();

        if (engineThread_.joinable()) {
            engineThread_.join();
        }

        running_.store(false);
    }

    void EngineService::setMarketDataSink(MarketDataSink* sink) {
        std::lock_guard<std::mutex> lock(sinkMutex_);
        sink_ = sink;
    }

    uint64_t EngineService::allocateOrderId() {
        return nextOrderId_.fetch_add(1);
    }

    ExecutionResult EngineService::submitOrder(Order order) {
        return submitOrder(std::move(order), 0);
    }

    ExecutionResult EngineService::submitOrder(Order order, uint64_t entryTimestampNs) {
        if (!running_.load()) {
            start();
        }

        if ((order.orderType == OrderType::Limit || order.orderType == OrderType::Market) && order.id == 0) {
            order.id = allocateOrderId();
        } else if (order.orderType == OrderType::Modify && order.id == 0) {
            order.id = allocateOrderId();
        }

        return invoke([this, order, entryTimestampNs]() mutable {
            entryTimestampNs_ = entryTimestampNs;
            auto result = engine_.submitOrder(order);
            entryTimestampNs_ = 0;
            publishStats();
            return result;
        });
    }

    L2Snapshot EngineService::getSnapshot(size_t depth) {
        if (!running_.load()) {
            start();
        }

        depth = std::min<size_t>(100, std::max<size_t>(1, depth));
        return invoke([this, depth]() {
            return buildSnapshot(depth);
        });
    }

    EngineState EngineService::getState() {
        if (!running_.load()) {
            start();
        }

        return invoke([this]() {
            EngineState state;
            state.running = running_.load();
            state.replayActive = replayActive_.load();
            state.symbol = symbol_;
            state.sequence = currentSequence_;
            state.nextOrderId = nextOrderId_.load();
            state.tradeCount = engine_.getTradeCount();
            state.totalVolume = engine_.getTotalVolume();
            state.orderCount = engine_.getOrderBook().getOrderCount();
            state.bidLevels = engine_.getOrderBook().getBidLevelCount();
            state.askLevels = engine_.getOrderBook().getAskLevelCount();
            state.clientCount = pnlTracker_.getClientCount();
            return state;
        });
    }

    bool EngineService::startReplay(const std::string& inputFile, double speed,
                                    bool loop, uint64_t loopPauseMs) {
        if (replayActive_.exchange(true)) {
            return false;
        }

        if (speed <= 0.0) {
            speed = 1.0;
        }

        if (!running_.load()) {
            start();
        }

        auto orders = csvParser_.parseFile(inputFile);
        if (orders.empty()) {
            replayActive_.store(false);
            return false;
        }

        uint64_t maxOrderId = 0;
        for (const auto& order : orders) {
            maxOrderId = std::max(maxOrderId, order.id);
        }
        nextOrderId_.store(std::max(nextOrderId_.load(), maxOrderId + 1));

        if (replayThread_.joinable()) {
            replayThread_.join();
        }

        replayThread_ = std::thread(
            [this, orders = std::move(orders), speed, loop, loopPauseMs]() mutable {
                auto sleepInterruptible = [this](uint64_t millis) {
                    const uint64_t step = 50;
                    uint64_t remaining = millis;
                    while (remaining > 0 && replayActive_.load()) {
                        uint64_t chunk = std::min(step, remaining);
                        std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
                        remaining -= chunk;
                    }
                };

                uint64_t loopOffset = 0;

                do {
                    for (size_t i = 0; i < orders.size() && replayActive_.load(); ++i) {
                        Order o = orders[i];
                        o.id += loopOffset;
                        submitOrder(o);

                        if (i + 1 >= orders.size()) {
                            continue;
                        }

                        uint64_t currentTs = orders[i].timestamp;
                        uint64_t nextTs = orders[i + 1].timestamp;
                        if (nextTs > currentTs) {
                            auto delta = static_cast<double>(nextTs - currentTs) / speed;
                            auto sleepMillis = static_cast<uint64_t>(std::round(delta));
                            sleepInterruptible(sleepMillis);
                        }
                    }

                    if (!loop || !replayActive_.load()) {
                        break;
                    }

                    // Offset successive loop iterations so IDs don't collide with
                    // whatever is still resting in the book from the previous pass.
                    loopOffset += static_cast<uint64_t>(orders.size()) + 1;
                    nextOrderId_.store(std::max(nextOrderId_.load(), loopOffset + 1));

                    sleepInterruptible(loopPauseMs);
                } while (replayActive_.load());

                replayActive_.store(false);
            });

        return true;
    }

    void EngineService::stopReplay() {
        replayActive_.store(false);
        if (replayThread_.joinable()) {
            replayThread_.join();
        }
    }

    void EngineService::engineLoop() {
        while (true) {
            Task task;

            {
                std::unique_lock<std::mutex> lock(queueMutex_);
                queueCv_.wait(lock, [this]() {
                    return stopRequested_.load() || !taskQueue_.empty();
                });

                if (stopRequested_.load() && taskQueue_.empty()) {
                    break;
                }

                task = std::move(taskQueue_.front());
                taskQueue_.pop();
            }

            task();
        }
    }

    void EngineService::enqueue(Task task) {
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            taskQueue_.push(std::move(task));
        }
        queueCv_.notify_one();
    }

    void EngineService::handleBookMutation(const BookMutation& mutation) {
        BookDelta delta;
        delta.sequence = nextSequence();
        delta.symbol = symbol_;
        delta.side = mutation.side;
        delta.price = mutation.price;
        delta.quantity = mutation.quantity;
        delta.orderCount = mutation.orderCount;
        delta.action = mutation.action;
        delta.timestamp = mutation.timestamp;
        if (entryTimestampNs_ != 0) {
            delta.engineLatencyNs = steadyNowNs() - entryTimestampNs_;
        }
        ++messageCount_;

        std::lock_guard<std::mutex> lock(sinkMutex_);
        if (sink_) {
            sink_->onBookDelta(delta);
        }
    }

    void EngineService::handleTrade(const Trade& trade) {
        pnlTracker_.onTradeExecuted(trade, trade.buyClientId, trade.sellClientId, trade.price);

        TradeEvent event;
        event.sequence = nextSequence();
        event.symbol = symbol_;
        event.tradeId = trade.tradeId;
        event.price = trade.price;
        event.quantity = trade.quantity;
        event.buyOrderId = trade.buyOrderId;
        event.sellOrderId = trade.sellOrderId;
        event.buyClientId = trade.buyClientId;
        event.sellClientId = trade.sellClientId;
        event.timestamp = trade.timestamp;
        if (entryTimestampNs_ != 0) {
            event.engineLatencyNs = steadyNowNs() - entryTimestampNs_;
        }
        ++messageCount_;

        std::lock_guard<std::mutex> lock(sinkMutex_);
        if (sink_) {
            sink_->onTradeEvent(event);
        }
    }

    void EngineService::handlePnL(const PnLSnapshot& snapshot) {
        PnLEvent event;
        event.sequence = nextSequence();
        event.symbol = symbol_;
        event.clientId = snapshot.clientId;
        event.netPosition = snapshot.netPosition;
        event.realizedPnL = snapshot.realizedPnL;
        event.unrealizedPnL = snapshot.unrealizedPnL;
        event.totalPnL = snapshot.totalPnL;
        event.timestamp = snapshot.timestamp;
        ++messageCount_;

        std::lock_guard<std::mutex> lock(sinkMutex_);
        if (sink_) {
            sink_->onPnLEvent(event);
        }
    }

    void EngineService::publishStats() {
        const auto& book = engine_.getOrderBook();

        StatsEvent stats;
        stats.sequence = nextSequence();
        stats.symbol = symbol_;
        stats.tradeCount = engine_.getTradeCount();
        stats.totalVolume = engine_.getTotalVolume();
        stats.orderCount = book.getOrderCount();
        stats.bidLevels = book.getBidLevelCount();
        stats.askLevels = book.getAskLevelCount();
        stats.bestBid = book.tryGetBestBid();
        stats.bestAsk = book.tryGetBestAsk();
        stats.spread = book.getSpread();
        stats.midPrice = book.getMidPrice();
        stats.timestamp = wallTimestampMillis();

        // MPS — sample throughput every ~1 second.
        uint64_t nowMs = stats.timestamp;
        uint64_t elapsedMs = nowMs - lastMpsTimestamp_;
        if (elapsedMs >= 1000) {
            uint64_t deltaMessages = messageCount_ - lastMessageCount_;
            currentMps_ = (deltaMessages * 1000) / elapsedMs;
            lastMpsTimestamp_ = nowMs;
            lastMessageCount_ = messageCount_;
        }
        stats.messagesPerSecond = currentMps_;
        ++messageCount_;

        std::lock_guard<std::mutex> lock(sinkMutex_);
        if (sink_) {
            sink_->onStatsEvent(stats);
        }
    }

    L2Snapshot EngineService::buildSnapshot(size_t depth) const {
        const auto& book = engine_.getOrderBook();

        L2Snapshot snapshot;
        snapshot.sequence = currentSequence_;
        snapshot.symbol = symbol_;
        snapshot.depth = depth;
        snapshot.bids = book.getTopLevels(Side::Buy, depth);
        snapshot.asks = book.getTopLevels(Side::Sell, depth);
        snapshot.bestBid = book.tryGetBestBid();
        snapshot.bestAsk = book.tryGetBestAsk();
        snapshot.spread = book.getSpread();
        snapshot.midPrice = book.getMidPrice();
        snapshot.timestamp = wallTimestampMillis();

        return snapshot;
    }

    uint64_t EngineService::wallTimestampMillis() {
        auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        return static_cast<uint64_t>(now.time_since_epoch().count());
    }

    uint64_t EngineService::steadyNowNs() {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count());
    }

}
