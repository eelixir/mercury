#include "OrderEntryGateway.h"
#include "ServerHelpers.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace Mercury {

    using json = nlohmann::json;

    OrderEntryGateway::OrderEntryGateway(MarketRuntime& runtime)
        : runtime_(runtime) {}

    void OrderEntryGateway::attach(uWS::App& app) {
        app.post("/api/orders", [this](auto* res, auto* /*req*/) {
            auto body = std::make_shared<std::string>();

            res->onAborted([body]() {
                (void) body;
            });

            res->onData([body, res, this](std::string_view chunk, bool isLast) {
                body->append(chunk.data(), chunk.size());
                if (!isLast) {
                    return;
                }

                handleRequest(res, *body);
            });
        });
    }

    // Explicit instantiation for the non-SSL case used by uWS::App.
    template void OrderEntryGateway::handleRequest<false>(
        uWS::HttpResponse<false>* res, const std::string& body);

    template <bool SSL>
    void OrderEntryGateway::handleRequest(uWS::HttpResponse<SSL>* res,
                                          const std::string& body) {
        try {
            const auto parsed = json::parse(body);
            const std::string defaultSymbol = runtime_.getSymbols().empty() ? "SIM" : runtime_.getSymbols().front();
            auto req = helpers::parseOrderRequestFromJson(parsed, runtime_, defaultSymbol);

            auto entryNs = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            ExecutionResult result = runtime_.submitOrder(req.symbol, req.order, entryNs);
            helpers::writeJson(res, "200 OK",
                               helpers::executionResultToJson(req.order, result));
        } catch (const std::exception& ex) {
            helpers::writeJson(res, "400 Bad Request", json{
                {"error", "invalid_request"},
                {"message", ex.what()}
            });
        }
    }

}
