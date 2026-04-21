#pragma once

#include "CSVParser.h"
#include "MarketData.h"
#include "MatchingEngine.h"
#include "PnLTracker.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>

namespace Mercury {

    struct EngineState {
        bool running = false;
        bool replayActive = false;
        std::string symbol;
        uint64_t sequence = 0;
        uint64_t nextOrderId = 1;
        uint64_t tradeCount = 0;
        uint64_t totalVolume = 0;
        size_t orderCount = 0;
        size_t bidLevels = 0;
        size_t askLevels = 0;
        size_t clientCount = 0;
    };

    class EngineService {
    public:
        explicit EngineService(std::string symbol = "SIM");
        ~EngineService();

        EngineService(const EngineService&) = delete;
        EngineService& operator=(const EngineService&) = delete;

        void start();
        void stop();

        bool isRunning() const { return running_.load(); }
        bool isReplayActive() const { return replayActive_.load(); }

        void setMarketDataSink(MarketDataSink* sink);

        uint64_t allocateOrderId();
        ExecutionResult submitOrder(Order order);
        ExecutionResult submitOrder(Order order, uint64_t entryTimestampNs);
        L2Snapshot getSnapshot(size_t depth = 20);
        EngineState getState();

        bool startReplay(const std::string& inputFile, double speed = 1.0,
                         bool loop = false, uint64_t loopPauseMs = 1000);
        void stopReplay();

        const std::string& getSymbol() const { return symbol_; }

    private:
        using Task = std::function<void()>;

        MatchingEngine engine_;
        PnLTracker pnlTracker_;
        CSVParser csvParser_;
        std::string symbol_;

        std::atomic<bool> running_{false};
        std::atomic<bool> stopRequested_{false};
        std::atomic<bool> replayActive_{false};
        std::atomic<uint64_t> nextOrderId_{1};

        std::thread engineThread_;
        std::thread replayThread_;

        std::mutex queueMutex_;
        std::condition_variable queueCv_;
        std::queue<Task> taskQueue_;

        std::mutex sinkMutex_;
        MarketDataSink* sink_ = nullptr;

        uint64_t currentSequence_ = 0;

        // Latency tracking — set on the engine thread before each
        // engine_.submitOrder() and read by callbacks during that call.
        uint64_t entryTimestampNs_ = 0;

        // Throughput counter — incremented by each outbound event,
        // sampled every ~1 second inside publishStats().
        uint64_t messageCount_ = 0;
        uint64_t lastMpsTimestamp_ = 0;
        uint64_t lastMessageCount_ = 0;
        uint64_t currentMps_ = 0;

        void engineLoop();
        void enqueue(Task task);

        template <typename F>
        auto invoke(F&& fn) -> std::invoke_result_t<F>;

        void handleBookMutation(const BookMutation& mutation);
        void handleTrade(const Trade& trade);
        void handlePnL(const PnLSnapshot& snapshot);
        void publishStats();
        L2Snapshot buildSnapshot(size_t depth) const;
        uint64_t nextSequence() { return ++currentSequence_; }
        static uint64_t wallTimestampMillis();
        static uint64_t steadyNowNs();
    };

    template <typename F>
    auto EngineService::invoke(F&& fn) -> std::invoke_result_t<F> {
        using Result = std::invoke_result_t<F>;

        auto promise = std::make_shared<std::promise<Result>>();
        auto future = promise->get_future();

        enqueue([promise, fn = std::forward<F>(fn)]() mutable {
            try {
                if constexpr (std::is_void_v<Result>) {
                    fn();
                    promise->set_value();
                } else {
                    promise->set_value(fn());
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });

        return future.get();
    }

}
