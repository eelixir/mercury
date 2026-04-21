#pragma once

#include "Order.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Mercury {

    enum class BookDeltaAction {
        Upsert,
        Remove
    };

    inline const char* bookDeltaActionToString(BookDeltaAction action) {
        switch (action) {
            case BookDeltaAction::Upsert: return "upsert";
            case BookDeltaAction::Remove: return "remove";
            default: return "unknown";
        }
    }

    struct BookLevel {
        int64_t price = 0;
        uint64_t quantity = 0;
        size_t orderCount = 0;
        Side side = Side::Buy;
    };

    struct BookMutation {
        Side side = Side::Buy;
        int64_t price = 0;
        uint64_t quantity = 0;
        size_t orderCount = 0;
        BookDeltaAction action = BookDeltaAction::Upsert;
        uint64_t timestamp = 0;
    };

    struct L2Snapshot {
        uint64_t sequence = 0;
        std::string symbol;
        size_t depth = 0;
        std::vector<BookLevel> bids;
        std::vector<BookLevel> asks;
        std::optional<int64_t> bestBid;
        std::optional<int64_t> bestAsk;
        int64_t spread = 0;
        int64_t midPrice = 0;
        uint64_t timestamp = 0;
    };

    struct BookDelta {
        uint64_t sequence = 0;
        std::string symbol;
        Side side = Side::Buy;
        int64_t price = 0;
        uint64_t quantity = 0;
        size_t orderCount = 0;
        BookDeltaAction action = BookDeltaAction::Upsert;
        uint64_t timestamp = 0;
        uint64_t engineLatencyNs = 0;
    };

    struct TradeEvent {
        uint64_t sequence = 0;
        std::string symbol;
        uint64_t tradeId = 0;
        int64_t price = 0;
        uint64_t quantity = 0;
        uint64_t buyOrderId = 0;
        uint64_t sellOrderId = 0;
        uint64_t buyClientId = 0;
        uint64_t sellClientId = 0;
        uint64_t timestamp = 0;
        uint64_t engineLatencyNs = 0;
    };

    struct StatsEvent {
        uint64_t sequence = 0;
        std::string symbol;
        uint64_t tradeCount = 0;
        uint64_t totalVolume = 0;
        size_t orderCount = 0;
        size_t bidLevels = 0;
        size_t askLevels = 0;
        std::optional<int64_t> bestBid;
        std::optional<int64_t> bestAsk;
        int64_t spread = 0;
        int64_t midPrice = 0;
        uint64_t timestamp = 0;
        uint64_t messagesPerSecond = 0;
    };

    struct PnLEvent {
        uint64_t sequence = 0;
        std::string symbol;
        uint64_t clientId = 0;
        int64_t netPosition = 0;
        int64_t realizedPnL = 0;
        int64_t unrealizedPnL = 0;
        int64_t totalPnL = 0;
        uint64_t timestamp = 0;
    };

    struct ExecutionEvent {
        uint64_t sequence = 0;
        std::string symbol;
        uint64_t orderId = 0;
        ExecutionStatus status = ExecutionStatus::Rejected;
        RejectReason rejectReason = RejectReason::None;
        uint64_t filledQuantity = 0;
        uint64_t remainingQuantity = 0;
        uint64_t timestamp = 0;
    };

    struct SimulationStateEvent {
        uint64_t sequence = 0;
        std::string symbol;
        bool enabled = false;
        bool running = false;
        bool paused = false;
        std::string clockMode;
        double speed = 1.0;
        std::string volatility;
        uint64_t simulationTimestamp = 0;
        size_t marketMakerCount = 0;
        size_t momentumCount = 0;
        size_t meanReversionCount = 0;
        size_t noiseTraderCount = 0;
        double realizedVolatilityBps = 0.0;
        double averageSpread = 0.0;
        std::string regime;
        double limitLambda = 0.0;
        double cancelLambda = 0.0;
        double marketableLambda = 0.0;
    };

    class MarketDataSink {
    public:
        virtual ~MarketDataSink() = default;

        virtual void onBookDelta(const BookDelta& delta) = 0;
        virtual void onTradeEvent(const TradeEvent& trade) = 0;
        virtual void onStatsEvent(const StatsEvent& stats) = 0;
        virtual void onPnLEvent(const PnLEvent& pnl) = 0;
        virtual void onExecutionEvent(const ExecutionEvent& /*execution*/) {}
        virtual void onSimulationState(const SimulationStateEvent& /*state*/) {}
    };

}
