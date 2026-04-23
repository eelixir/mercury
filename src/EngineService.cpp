#include "EngineService.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>

namespace Mercury {

    EngineService::EngineService(std::vector<std::string> symbols)
        : symbols_(std::move(symbols)) {
        for (const auto& sym : symbols_) {
            books_.emplace(sym, std::make_unique<InstrumentBook>(sym));
        }
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

        for (auto& [sym, book] : books_) {
            wireBookCallbacks(*book);
        }

        engineThread_ = std::thread([this]() {
            engineLoop();
        });
    }

    void EngineService::wireBookCallbacks(InstrumentBook& book) {
        book.engine.setBookMutationCallback([this](const BookMutation& mutation) {
            handleBookMutation(mutation);
        });

        book.engine.setTradeCallback([this](const Trade& trade) {
            handleTrade(trade);
        });

        book.engine.setExecutionCallback([this](const ExecutionResult& result) {
            handleExecution(result);
        });

        book.pnlTracker.setPnLCallback([this](const PnLSnapshot& snapshot) {
            handlePnL(snapshot);
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

    bool EngineService::hasSymbol(const std::string& symbol) const {
        return books_.find(symbol) != books_.end();
    }

    ExecutionResult EngineService::submitOrder(const std::string& symbol, Order order) {
        return submitOrder(symbol, std::move(order), 0);
    }

    ExecutionResult EngineService::submitOrder(const std::string& symbol, Order order, uint64_t entryTimestampNs) {
        if (!running_.load()) {
            start();
        }

        if (!hasSymbol(symbol)) {
            return ExecutionResult::makeRejection(order.id, RejectReason::InvalidSymbol);
        }

        if ((order.orderType == OrderType::Limit || order.orderType == OrderType::Market) && order.id == 0) {
            order.id = allocateOrderId();
        } else if (order.orderType == OrderType::Modify && order.id == 0) {
            order.id = allocateOrderId();
        }

        return invoke([this, symbol, order, entryTimestampNs]() mutable {
            activeSymbol_ = symbol;
            entryTimestampNs_ = entryTimestampNs;
            auto& book = *books_.at(symbol);
            auto result = book.engine.submitOrder(order);
            entryTimestampNs_ = 0;
            publishStats(symbol);
            activeSymbol_.clear();
            return result;
        });
    }

    L2Snapshot EngineService::getSnapshot(const std::string& symbol, size_t depth) {
        if (!running_.load()) {
            start();
        }

        if (!hasSymbol(symbol)) {
            L2Snapshot empty;
            empty.symbol = symbol;
            return empty;
        }

        depth = std::min<size_t>(100, std::max<size_t>(1, depth));
        return invoke([this, symbol, depth]() {
            return buildSnapshot(symbol, depth);
        });
    }

    std::optional<OrderBook::QueuePositionInfo> EngineService::getQueuePosition(const std::string& symbol, uint64_t orderId) {
        if (!running_.load()) {
            start();
        }

        if (!hasSymbol(symbol) || orderId == 0) {
            return std::nullopt;
        }

        return invoke([this, symbol, orderId]() -> std::optional<OrderBook::QueuePositionInfo> {
            auto it = books_.find(symbol);
            if (it == books_.end()) {
                return std::nullopt;
            }
            return it->second->engine.getOrderBook().getQueuePosition(orderId);
        });
    }

    std::unordered_map<uint64_t, OrderBook::QueuePositionInfo> EngineService::getQueuePositions(
        const std::string& symbol,
        const std::vector<uint64_t>& orderIds) {
        if (!running_.load()) {
            start();
        }

        if (!hasSymbol(symbol) || orderIds.empty()) {
            return {};
        }

        return invoke([this, symbol, orderIds]() {
            std::unordered_map<uint64_t, OrderBook::QueuePositionInfo> positions;
            auto it = books_.find(symbol);
            if (it == books_.end()) {
                return positions;
            }

            const auto& book = it->second->engine.getOrderBook();
            positions.reserve(orderIds.size());
            for (uint64_t orderId : orderIds) {
                if (auto position = book.getQueuePosition(orderId)) {
                    positions.emplace(orderId, *position);
                }
            }
            return positions;
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
            state.symbols = symbols_;
            state.sequence = currentSequence_;
            state.nextOrderId = nextOrderId_.load();

            // Aggregate across all books.
            for (const auto& [sym, book] : books_) {
                state.tradeCount += book->engine.getTradeCount();
                state.totalVolume += book->engine.getTotalVolume();
                state.orderCount += book->engine.getOrderBook().getOrderCount();
                state.bidLevels += book->engine.getOrderBook().getBidLevelCount();
                state.askLevels += book->engine.getOrderBook().getAskLevelCount();
                state.clientCount += book->pnlTracker.getClientCount();
            }
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

        // Replay routes all orders to the first symbol.
        const std::string replaySymbol = symbols_.empty() ? "SIM" : symbols_.front();

        replayThread_ = std::thread(
            [this, orders = std::move(orders), speed, loop, loopPauseMs, replaySymbol]() mutable {
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
                        submitOrder(replaySymbol, o);

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
        delta.symbol = activeSymbol_;
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
        auto& book = *books_.at(activeSymbol_);
        book.pnlTracker.onTradeExecuted(trade, trade.buyClientId, trade.sellClientId, trade.price);

        TradeEvent event;
        event.sequence = nextSequence();
        event.symbol = activeSymbol_;
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
        event.symbol = activeSymbol_;
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

    void EngineService::handleExecution(const ExecutionResult& result) {
        ExecutionEvent event;
        event.sequence = nextSequence();
        event.symbol = activeSymbol_;
        event.orderId = result.orderId;
        event.status = result.status;
        event.rejectReason = result.rejectReason;
        event.filledQuantity = result.filledQuantity;
        event.remainingQuantity = result.remainingQuantity;
        event.timestamp = wallTimestampMillis();
        ++messageCount_;

        std::lock_guard<std::mutex> lock(sinkMutex_);
        if (sink_) {
            sink_->onExecutionEvent(event);
        }
    }

    void EngineService::publishStats(const std::string& symbol) {
        auto it = books_.find(symbol);
        if (it == books_.end()) {
            return;
        }

        const auto& book = it->second->engine.getOrderBook();

        StatsEvent stats;
        stats.sequence = nextSequence();
        stats.symbol = symbol;
        stats.tradeCount = it->second->engine.getTradeCount();
        stats.totalVolume = it->second->engine.getTotalVolume();
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

    L2Snapshot EngineService::buildSnapshot(const std::string& symbol, size_t depth) const {
        auto it = books_.find(symbol);
        if (it == books_.end()) {
            L2Snapshot empty;
            empty.symbol = symbol;
            return empty;
        }

        const auto& book = it->second->engine.getOrderBook();

        L2Snapshot snapshot;
        snapshot.sequence = currentSequence_;
        snapshot.symbol = symbol;
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
