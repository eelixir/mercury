#include "ServerApp.h"

#include "EngineService.h"
#include "MarketData.h"
#include <App.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace Mercury {

    namespace {

        using json = nlohmann::json;

        constexpr size_t DEFAULT_L2_DEPTH = 20;
        constexpr size_t MAX_L2_DEPTH = 100;
        constexpr std::string_view MARKET_TOPIC = "market";

        struct PerSocketData {
            size_t depth = DEFAULT_L2_DEPTH;
        };

        size_t clampDepth(size_t depth) {
            return std::min<size_t>(MAX_L2_DEPTH, std::max<size_t>(1, depth));
        }

        std::string toLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string sideToLowerString(Side side) {
            return side == Side::Buy ? "buy" : "sell";
        }

        std::string orderTypeToLowerString(OrderType type) {
            switch (type) {
                case OrderType::Market: return "market";
                case OrderType::Limit: return "limit";
                case OrderType::Cancel: return "cancel";
                case OrderType::Modify: return "modify";
                default: return "unknown";
            }
        }

        std::string tifToString(TimeInForce tif) {
            switch (tif) {
                case TimeInForce::GTC: return "GTC";
                case TimeInForce::IOC: return "IOC";
                case TimeInForce::FOK: return "FOK";
                default: return "GTC";
            }
        }

        std::string executionStatusToLowerString(ExecutionStatus status) {
            switch (status) {
                case ExecutionStatus::Filled: return "filled";
                case ExecutionStatus::PartialFill: return "partial_fill";
                case ExecutionStatus::Resting: return "resting";
                case ExecutionStatus::Cancelled: return "cancelled";
                case ExecutionStatus::Modified: return "modified";
                case ExecutionStatus::Rejected: return "rejected";
                default: return "unknown";
            }
        }

        OrderType parseOrderType(const std::string& raw) {
            const auto value = toLower(raw);
            if (value == "market") return OrderType::Market;
            if (value == "cancel") return OrderType::Cancel;
            if (value == "modify") return OrderType::Modify;
            return OrderType::Limit;
        }

        Side parseSide(const std::string& raw) {
            return toLower(raw) == "sell" ? Side::Sell : Side::Buy;
        }

        TimeInForce parseTimeInForce(const std::string& raw) {
            const auto value = toLower(raw);
            if (value == "ioc") return TimeInForce::IOC;
            if (value == "fok") return TimeInForce::FOK;
            return TimeInForce::GTC;
        }

        json bookLevelToJson(const BookLevel& level) {
            return json{
                {"price", level.price},
                {"quantity", level.quantity},
                {"orderCount", level.orderCount},
                {"side", sideToLowerString(level.side)}
            };
        }

        json tradeToJson(const Trade& trade) {
            return json{
                {"tradeId", trade.tradeId},
                {"buyOrderId", trade.buyOrderId},
                {"sellOrderId", trade.sellOrderId},
                {"buyClientId", trade.buyClientId},
                {"sellClientId", trade.sellClientId},
                {"price", trade.price},
                {"quantity", trade.quantity},
                {"timestamp", trade.timestamp}
            };
        }

        json snapshotPayloadToJson(const L2Snapshot& snapshot) {
            json bids = json::array();
            for (const auto& level : snapshot.bids) {
                bids.push_back(bookLevelToJson(level));
            }

            json asks = json::array();
            for (const auto& level : snapshot.asks) {
                asks.push_back(bookLevelToJson(level));
            }

            return json{
                {"depth", snapshot.depth},
                {"bids", std::move(bids)},
                {"asks", std::move(asks)},
                {"bestBid", snapshot.bestBid ? json(*snapshot.bestBid) : json(nullptr)},
                {"bestAsk", snapshot.bestAsk ? json(*snapshot.bestAsk) : json(nullptr)},
                {"spread", snapshot.spread},
                {"midPrice", snapshot.midPrice},
                {"timestamp", snapshot.timestamp}
            };
        }

        json envelopeToJson(std::string_view type, uint64_t sequence, std::string_view symbol, json payload) {
            return json{
                {"type", type},
                {"sequence", sequence},
                {"symbol", symbol},
                {"payload", std::move(payload)}
            };
        }

        std::string snapshotEnvelope(const L2Snapshot& snapshot) {
            return envelopeToJson("snapshot", snapshot.sequence, snapshot.symbol, snapshotPayloadToJson(snapshot)).dump();
        }

        json executionResultToJson(const Order& order, const ExecutionResult& result) {
            json trades = json::array();
            for (const auto& trade : result.trades) {
                trades.push_back(tradeToJson(trade));
            }

            return json{
                {"submittedOrderId", order.id},
                {"orderType", orderTypeToLowerString(order.orderType)},
                {"side", sideToLowerString(order.side)},
                {"tif", tifToString(order.tif)},
                {"status", executionStatusToLowerString(result.status)},
                {"rejectReason", rejectReasonToString(result.rejectReason)},
                {"orderId", result.orderId},
                {"filledQuantity", result.filledQuantity},
                {"remainingQuantity", result.remainingQuantity},
                {"message", result.message},
                {"trades", std::move(trades)}
            };
        }

        json stateToJson(const EngineState& state, uint64_t connections) {
            return json{
                {"running", state.running},
                {"replayActive", state.replayActive},
                {"symbol", state.symbol},
                {"sequence", state.sequence},
                {"nextOrderId", state.nextOrderId},
                {"tradeCount", state.tradeCount},
                {"totalVolume", state.totalVolume},
                {"orderCount", state.orderCount},
                {"bidLevels", state.bidLevels},
                {"askLevels", state.askLevels},
                {"clientCount", state.clientCount},
                {"connections", connections}
            };
        }

        Order parseOrderFromJson(const json& body, EngineService& engineService) {
            Order order;
            const auto orderType = parseOrderType(body.value("type", std::string("limit")));
            order.orderType = orderType;
            order.clientId = body.value("clientId", 0ULL);
            order.tif = parseTimeInForce(body.value("tif", std::string("GTC")));

            if (orderType == OrderType::Cancel) {
                order.id = body.value("orderId", body.value("id", 0ULL));
                order.side = parseSide(body.value("side", std::string("buy")));
                return order;
            }

            if (orderType == OrderType::Modify) {
                order.id = body.value("id", 0ULL);
                if (order.id == 0) {
                    order.id = engineService.allocateOrderId();
                }
                order.targetOrderId = body.value("orderId", 0ULL);
                order.newPrice = body.value("newPrice", 0LL);
                order.newQuantity = body.value("newQuantity", 0ULL);
                order.side = parseSide(body.value("side", std::string("buy")));
                return order;
            }

            order.id = body.value("id", body.value("orderId", 0ULL));
            if (order.id == 0) {
                order.id = engineService.allocateOrderId();
            }
            order.side = parseSide(body.value("side", std::string("buy")));
            order.price = body.value("price", 0LL);
            order.quantity = body.value("quantity", 0ULL);
            return order;
        }

        template <bool SSL>
        void applyCors(uWS::HttpResponse<SSL>* res) {
            res->writeHeader("Access-Control-Allow-Origin", "*");
            res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
            res->writeHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
        }

        template <bool SSL>
        void writeJson(uWS::HttpResponse<SSL>* res, std::string_view status, const json& payload) {
            applyCors(res);
            res->writeStatus(status);
            res->writeHeader("Content-Type", "application/json");
            res->end(payload.dump());
        }

        class MarketDataPublisher : public MarketDataSink {
        public:
            void attach(uWS::Loop* loop, uWS::App* app) {
                std::lock_guard<std::mutex> lock(mutex_);
                loop_ = loop;
                app_ = app;
            }

            void incrementConnections() {
                connections_.fetch_add(1);
            }

            void decrementConnections() {
                connections_.fetch_sub(1);
            }

            uint64_t connectionCount() const {
                return connections_.load();
            }

            void onBookDelta(const BookDelta& delta) override {
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

            void onTradeEvent(const TradeEvent& trade) override {
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

            void onStatsEvent(const StatsEvent& stats) override {
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

            void onPnLEvent(const PnLEvent& pnl) override {
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

        private:
            void broadcast(std::string message) {
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
                    app->publish(MARKET_TOPIC, message, uWS::OpCode::TEXT, false);
                });
            }

            std::mutex mutex_;
            uWS::Loop* loop_ = nullptr;
            uWS::App* app_ = nullptr;
            std::atomic<uint64_t> connections_{0};
        };

    }

    int runServer(const ServerOptions& options) {
        EngineService engineService(options.symbol);
        MarketDataPublisher publisher;

        engineService.setMarketDataSink(&publisher);
        engineService.start();

        if (options.replayFile && !engineService.startReplay(*options.replayFile, options.replaySpeed,
                                                             options.replayLoop, options.replayLoopPauseMs)) {
            std::cerr << "Failed to start replay from " << *options.replayFile << "\n";
            engineService.stop();
            return 1;
        }

        uWS::App app;
        bool listening = false;

        auto sendSnapshot = [&engineService](auto* ws, size_t depth) {
            auto snapshot = engineService.getSnapshot(depth);
            ws->send(snapshotEnvelope(snapshot), uWS::OpCode::TEXT);
        };

        app.options("/*", [](auto* res, auto* /*req*/) {
            applyCors(res);
            res->writeStatus("204 No Content");
            res->end("");
        });

        app.get("/api/health", [&engineService](auto* res, auto* /*req*/) {
            writeJson(res, "200 OK", json{
                {"status", "ok"},
                {"running", engineService.isRunning()},
                {"replayActive", engineService.isReplayActive()}
            });
        });

        app.get("/api/state", [&engineService, &publisher](auto* res, auto* /*req*/) {
            writeJson(res, "200 OK", stateToJson(engineService.getState(), publisher.connectionCount()));
        });

        app.post("/api/orders", [&engineService](auto* res, auto* /*req*/) {
            auto body = std::make_shared<std::string>();

            res->onAborted([body]() {
                (void) body;
            });

            res->onData([body, res, &engineService](std::string_view chunk, bool isLast) {
                body->append(chunk.data(), chunk.size());
                if (!isLast) {
                    return;
                }

                try {
                    const auto parsed = json::parse(*body);
                    Order order = parseOrderFromJson(parsed, engineService);
                    ExecutionResult result = engineService.submitOrder(order);
                    writeJson(res, "200 OK", executionResultToJson(order, result));
                } catch (const std::exception& ex) {
                    writeJson(res, "400 Bad Request", json{
                        {"error", "invalid_request"},
                        {"message", ex.what()}
                    });
                }
            });
        });

        app.ws<PerSocketData>("/ws/market", {
            .compression = uWS::DISABLED,
            .maxPayloadLength = 1024 * 1024,
            .idleTimeout = 30,
            .maxBackpressure = 1024 * 1024,
            .closeOnBackpressureLimit = false,
            .resetIdleTimeoutOnSend = false,
            .sendPingsAutomatically = true,
            .open = [&publisher, &sendSnapshot](auto* ws) {
                ws->subscribe(MARKET_TOPIC);
                ws->getUserData()->depth = DEFAULT_L2_DEPTH;
                publisher.incrementConnections();
                sendSnapshot(ws, DEFAULT_L2_DEPTH);
            },
            .message = [&sendSnapshot](auto* ws, std::string_view message, uWS::OpCode /*opCode*/) {
                try {
                    const auto parsed = json::parse(message);
                    const auto type = parsed.value("type", std::string());
                    if (type == "subscribe") {
                        size_t depth = clampDepth(parsed.value("depth", static_cast<int>(DEFAULT_L2_DEPTH)));
                        ws->getUserData()->depth = depth;
                        sendSnapshot(ws, depth);
                    }
                } catch (...) {
                    // Ignore malformed client messages.
                }
            },
            .close = [&publisher](auto* /*ws*/, int /*code*/, std::string_view /*message*/) {
                publisher.decrementConnections();
            }
        });

        app.listen(options.host, options.port, [&publisher, &app, &listening, &options](auto* listenSocket) {
            listening = listenSocket != nullptr;
            if (listenSocket) {
                publisher.attach(uWS::Loop::get(), &app);
                std::cout << "Mercury server listening on http://" << options.host << ":" << options.port << "\n";
            }
        });

        if (!listening) {
            std::cerr << "Failed to bind server to " << options.host << ":" << options.port << "\n";
            engineService.stop();
            return 1;
        }

        app.run();

        engineService.stopReplay();
        engineService.stop();
        return 0;
    }

}
