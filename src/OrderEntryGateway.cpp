#include "OrderEntryGateway.h"
#include "ServerHelpers.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <memory>
#include <string>

namespace Mercury {

    using json = nlohmann::json;

    OrderEntryGateway::OrderEntryGateway(EngineService& engine)
        : engine_(engine) {}

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
            Order order = helpers::parseOrderFromJson(parsed, engine_);
            ExecutionResult result = engine_.submitOrder(order);
            helpers::writeJson(res, "200 OK",
                               helpers::executionResultToJson(order, result));
        } catch (const std::exception& ex) {
            helpers::writeJson(res, "400 Bad Request", json{
                {"error", "invalid_request"},
                {"message", ex.what()}
            });
        }
    }

}
