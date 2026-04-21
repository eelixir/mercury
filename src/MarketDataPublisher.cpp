#include "MarketDataPublisher.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace Mercury {

    namespace {

        using json = nlohmann::json;

        std::string sideToLowerString(Side side) {
            return side == Side::Buy ? "buy" : "sell";
        }

        json envelopeToJson(std::string_view type,
                            uint64_t sequence,
                            std::string_view symbol,
                            json payload) {
            return json{
                {"type", type},
                {"sequence", sequence},
                {"symbol", symbol},
                {"payload", std::move(payload)}
            };
        }

    }

    void MarketDataPublisher::attach(uWS::Loop* loop, uWS::App* app) {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = loop;
        app_ = app;
    }

    void MarketDataPublisher::detach() {
        std::lock_guard<std::mutex> lock(mutex_);
        loop_ = nullptr;
        app_ = nullptr;
    }

    void MarketDataPublisher::incrementConnections() {
        connections_.fetch_add(1);
    }

    void MarketDataPublisher::decrementConnections() {
        connections_.fetch_sub(1);
    }

    uint64_t MarketDataPublisher::connectionCount() const {
        return connections_.load();
    }

    void MarketDataPublisher::onBookDelta(const BookDelta& delta) {
        json payload{
            {"side", sideToLowerString(delta.side)},
            {"price", delta.price},
            {"quantity", delta.quantity},
            {"orderCount", delta.orderCount},
            {"action", bookDeltaActionToString(delta.action)},
            {"timestamp", delta.timestamp}
        };
        broadcast(envelopeToJson("book_delta", delta.sequence, delta.symbol, std::move(payload)).dump());
    }

    void MarketDataPublisher::onTradeEvent(const TradeEvent& trade) {
        json payload{
            {"tradeId", trade.tradeId},
            {"price", trade.price},
            {"quantity", trade.quantity},
            {"buyOrderId", trade.buyOrderId},
            {"sellOrderId", trade.sellOrderId},
            {"buyClientId", trade.buyClientId},
            {"sellClientId", trade.sellClientId},
            {"timestamp", trade.timestamp}
        };
        broadcast(envelopeToJson("trade", trade.sequence, trade.symbol, std::move(payload)).dump());
    }

    void MarketDataPublisher::onStatsEvent(const StatsEvent& stats) {
        json payload{
            {"tradeCount", stats.tradeCount},
            {"totalVolume", stats.totalVolume},
            {"orderCount", stats.orderCount},
            {"bidLevels", stats.bidLevels},
            {"askLevels", stats.askLevels},
            {"bestBid", stats.bestBid ? json(*stats.bestBid) : json(nullptr)},
            {"bestAsk", stats.bestAsk ? json(*stats.bestAsk) : json(nullptr)},
            {"spread", stats.spread},
            {"midPrice", stats.midPrice},
            {"timestamp", stats.timestamp}
        };
        broadcast(envelopeToJson("stats", stats.sequence, stats.symbol, std::move(payload)).dump());
    }

    void MarketDataPublisher::onPnLEvent(const PnLEvent& pnl) {
        json payload{
            {"clientId", pnl.clientId},
            {"netPosition", pnl.netPosition},
            {"realizedPnL", pnl.realizedPnL},
            {"unrealizedPnL", pnl.unrealizedPnL},
            {"totalPnL", pnl.totalPnL},
            {"timestamp", pnl.timestamp}
        };
        broadcast(envelopeToJson("pnl", pnl.sequence, pnl.symbol, std::move(payload)).dump());
    }

    void MarketDataPublisher::broadcast(std::string message) {
        uWS::Loop* loop = nullptr;
        uWS::App* app = nullptr;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            loop = loop_;
            app = app_;
        }

        if (!loop || !app) {
            return;
        }

        loop->defer([app, message = std::move(message)]() mutable {
            app->publish(TOPIC, message, uWS::OpCode::TEXT, false);
        });
    }

}
