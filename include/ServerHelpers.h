#pragma once

#include "MarketData.h"
#include "MarketRuntime.h"
#include "Order.h"

#include <App.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace Mercury {

    namespace helpers {

        using json = nlohmann::json;

        // ----------------------------------------------------------------
        // String / enum conversion utilities
        // ----------------------------------------------------------------

        inline std::string toLower(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        inline std::string sideToLowerString(Side side) {
            return side == Side::Buy ? "buy" : "sell";
        }

        inline std::string orderTypeToLowerString(OrderType type) {
            switch (type) {
                case OrderType::Market: return "market";
                case OrderType::Limit: return "limit";
                case OrderType::Cancel: return "cancel";
                case OrderType::Modify: return "modify";
                default: return "unknown";
            }
        }

        inline std::string tifToString(TimeInForce tif) {
            switch (tif) {
                case TimeInForce::GTC: return "GTC";
                case TimeInForce::IOC: return "IOC";
                case TimeInForce::FOK: return "FOK";
                default: return "GTC";
            }
        }

        inline std::string executionStatusToLowerString(ExecutionStatus status) {
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

        // ----------------------------------------------------------------
        // Parsing helpers
        // ----------------------------------------------------------------

        inline OrderType parseOrderType(const std::string& raw) {
            const auto value = toLower(raw);
            if (value == "market") return OrderType::Market;
            if (value == "cancel") return OrderType::Cancel;
            if (value == "modify") return OrderType::Modify;
            return OrderType::Limit;
        }

        inline Side parseSide(const std::string& raw) {
            return toLower(raw) == "sell" ? Side::Sell : Side::Buy;
        }

        inline TimeInForce parseTimeInForce(const std::string& raw) {
            const auto value = toLower(raw);
            if (value == "ioc") return TimeInForce::IOC;
            if (value == "fok") return TimeInForce::FOK;
            return TimeInForce::GTC;
        }

        // ----------------------------------------------------------------
        // JSON serialization helpers
        // ----------------------------------------------------------------

        inline json bookLevelToJson(const BookLevel& level) {
            return json{
                {"price", level.price},
                {"quantity", level.quantity},
                {"orderCount", level.orderCount},
                {"side", sideToLowerString(level.side)}
            };
        }

        inline json tradeToJson(const Trade& trade) {
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

        inline json snapshotPayloadToJson(const L2Snapshot& snapshot) {
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

        inline json envelopeToJson(std::string_view type, uint64_t sequence, std::string_view symbol, json payload) {
            return json{
                {"type", type},
                {"sequence", sequence},
                {"symbol", symbol},
                {"payload", std::move(payload)}
            };
        }

        inline std::string snapshotEnvelope(const L2Snapshot& snapshot) {
            return envelopeToJson("snapshot", snapshot.sequence, snapshot.symbol, snapshotPayloadToJson(snapshot)).dump();
        }

        inline json executionResultToJson(const Order& order, const ExecutionResult& result) {
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

        inline json stateToJson(const MarketRuntimeState& state, uint64_t connections) {
            return json{
                {"running", state.running},
                {"replayActive", state.replayActive},
                {"symbol", state.symbol},
                {"symbols", state.symbols},
                {"sequence", state.sequence},
                {"nextOrderId", state.nextOrderId},
                {"tradeCount", state.tradeCount},
                {"totalVolume", state.totalVolume},
                {"orderCount", state.orderCount},
                {"bidLevels", state.bidLevels},
                {"askLevels", state.askLevels},
                {"clientCount", state.clientCount},
                {"connections", connections},
                {"simulation", {
                    {"enabled", state.simulationEnabled},
                    {"running", state.simulationRunning},
                    {"paused", state.simulationPaused},
                    {"clockMode", state.clockMode},
                    {"speed", state.simulationSpeed},
                    {"volatility", state.volatilityPreset},
                    {"simulationTimestamp", state.simulationTimestamp},
                    {"marketMakerCount", state.marketMakerCount},
                    {"momentumCount", state.momentumCount},
                    {"meanReversionCount", state.meanReversionCount},
                    {"realizedVolatilityBps", state.realizedVolatilityBps},
                    {"averageSpread", state.averageSpread},
                    {"toxicityScore", state.toxicityScore}
                }}
            };
        }

        inline std::string simStateEnvelope(const SimulationStateEvent& state) {
            json payload{
                {"enabled", state.enabled},
                {"running", state.running},
                {"paused", state.paused},
                {"clockMode", state.clockMode},
                {"speed", state.speed},
                {"volatility", state.volatility},
                {"simulationTimestamp", state.simulationTimestamp},
                {"marketMakerCount", state.marketMakerCount},
                {"momentumCount", state.momentumCount},
                {"meanReversionCount", state.meanReversionCount},
                {"noiseTraderCount", state.noiseTraderCount},
                {"realizedVolatilityBps", state.realizedVolatilityBps},
                {"averageSpread", state.averageSpread},
                {"toxicityScore", state.toxicityScore},
                {"regime", state.regime},
                {"limitLambda", state.limitLambda},
                {"cancelLambda", state.cancelLambda},
                {"marketableLambda", state.marketableLambda}
            };
            return envelopeToJson("sim_state", state.sequence, state.symbol, std::move(payload)).dump();
        }

        // ----------------------------------------------------------------
        // Order parsing from JSON request body
        // ----------------------------------------------------------------

        struct OrderRequest {
            Order order;
            std::string symbol;
        };

        template <typename IdSource>
        inline Order parseOrderFromJson(const json& body, IdSource& idSource) {
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
                    order.id = idSource.allocateOrderId();
                }
                order.targetOrderId = body.value("orderId", 0ULL);
                order.newPrice = body.value("newPrice", 0LL);
                order.newQuantity = body.value("newQuantity", 0ULL);
                order.side = parseSide(body.value("side", std::string("buy")));
                return order;
            }

            order.id = body.value("id", body.value("orderId", 0ULL));
            if (order.id == 0) {
                order.id = idSource.allocateOrderId();
            }
            order.side = parseSide(body.value("side", std::string("buy")));
            order.price = body.value("price", 0LL);
            order.quantity = body.value("quantity", 0ULL);

            return order;
        }

        template <typename IdSource>
        inline OrderRequest parseOrderRequestFromJson(const json& body, IdSource& idSource, const std::string& defaultSymbol) {
            OrderRequest req;
            req.order = parseOrderFromJson(body, idSource);
            req.symbol = body.value("symbol", defaultSymbol);
            return req;
        }

        // ----------------------------------------------------------------
        // HTTP response helpers
        // ----------------------------------------------------------------

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

    }  // namespace helpers

}  // namespace Mercury
