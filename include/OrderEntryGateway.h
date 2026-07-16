#pragma once

#include "MarketRuntime.h"

#include <App.h>

#include <atomic>
#include <memory>
#include <string>

namespace Mercury {

    // Handles HTTP POST /api/orders.
    //
    // Owns request parsing, JSON validation, and response serialization.
    // Delegates order execution to EngineService (synchronous roundtrip
    // via the engine thread).
    //
    // Threading:
    //   - attach() registers the route on the uWS::App and must be called
    //     before app.listen().
    //   - The POST handler runs on the uWS event-loop thread.  It blocks
    //     inside EngineService::submitOrder() which uses promise/future
    //     to serialize onto the engine thread and return the result.
    class OrderEntryGateway {
    public:
        explicit OrderEntryGateway(MarketRuntime& runtime);
        ~OrderEntryGateway() = default;

        OrderEntryGateway(const OrderEntryGateway&) = delete;
        OrderEntryGateway& operator=(const OrderEntryGateway&) = delete;

        // Register POST /api/orders on the given app.
        // Must be called before app.listen().
        void attach(uWS::App& app);

    private:
        MarketRuntime& runtime_;

        // Called when the full request body has arrived.
        // @p aborted is set by onAborted; must not write the response if true.
        template <bool SSL>
        void handleRequest(uWS::HttpResponse<SSL>* res,
                           const std::string& body,
                           const std::shared_ptr<std::atomic<bool>>& aborted);
    };

}
