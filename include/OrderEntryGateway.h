#pragma once

#include "EngineService.h"

#include <App.h>

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
        explicit OrderEntryGateway(EngineService& engine);
        ~OrderEntryGateway() = default;

        OrderEntryGateway(const OrderEntryGateway&) = delete;
        OrderEntryGateway& operator=(const OrderEntryGateway&) = delete;

        // Register POST /api/orders on the given app.
        // Must be called before app.listen().
        void attach(uWS::App& app);

    private:
        EngineService& engine_;

        // Called when the full request body has arrived.
        template <bool SSL>
        void handleRequest(uWS::HttpResponse<SSL>* res,
                           const std::string& body);
    };

}
