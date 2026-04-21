#include "ServerApp.h"

#include "EngineService.h"
#include "MarketData.h"
#include "MarketDataPublisher.h"
#include "OrderEntryGateway.h"
#include "ServerHelpers.h"

#include <App.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace Mercury {

    namespace {

        using json = nlohmann::json;
        using namespace helpers;

        constexpr size_t DEFAULT_L2_DEPTH = 20;
        constexpr size_t MAX_L2_DEPTH = 100;

        struct PerSocketData {
            size_t depth = DEFAULT_L2_DEPTH;
        };

        size_t clampDepth(size_t depth) {
            return std::min<size_t>(MAX_L2_DEPTH, std::max<size_t>(1, depth));
        }

    }

    int runServer(const ServerOptions& options) {
        EngineService engineService(options.symbol);
        MarketDataPublisher publisher;
        OrderEntryGateway gateway(engineService);

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

        gateway.attach(app);

        app.ws<PerSocketData>("/ws/market", {
            .compression = uWS::DISABLED,
            .maxPayloadLength = 1024 * 1024,
            .idleTimeout = 30,
            .maxBackpressure = 1024 * 1024,
            .closeOnBackpressureLimit = false,
            .resetIdleTimeoutOnSend = false,
            .sendPingsAutomatically = true,
            .open = [&publisher, &sendSnapshot](auto* ws) {
                ws->subscribe(MarketDataPublisher::TOPIC);
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

        publisher.detach();
        engineService.stopReplay();
        engineService.stop();
        return 0;
    }

}
