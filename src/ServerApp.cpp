#include "ServerApp.h"

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
        MarketRuntime runtime(options.symbols, options.simulation);
        MarketDataPublisher publisher;
        OrderEntryGateway gateway(runtime);

        runtime.addSubscriber(&publisher);
        runtime.start();

        if (options.replayFile && !runtime.startReplay(*options.replayFile, options.replaySpeed,
                                                       options.replayLoop, options.replayLoopPauseMs)) {
            std::cerr << "Failed to start replay from " << *options.replayFile << "\n";
            runtime.stop();
            return 1;
        }

        uWS::App app;
        bool listening = false;

        auto sendSnapshot = [&runtime](auto* ws, size_t depth) {
            for (const auto& sym : runtime.getSymbols()) {
                auto snapshot = runtime.getSnapshot(sym, depth);
                ws->send(snapshotEnvelope(snapshot), uWS::OpCode::TEXT);
            }
        };

        auto sendSimulationState = [&runtime](auto* ws) {
            auto state = runtime.getState();
            for (const auto& sym : runtime.getSymbols()) {
                SimulationStateEvent event;
                event.sequence = state.sequence;
                event.symbol = sym; // Use the specific symbol
                event.enabled = state.simulationEnabled;
                event.running = state.simulationRunning;
                event.paused = state.simulationPaused;
                event.clockMode = state.clockMode;
                event.speed = state.simulationSpeed;
                event.volatility = state.volatilityPreset;
                event.simulationTimestamp = state.simulationTimestamp;
                event.marketMakerCount = state.marketMakerCount;
                event.momentumCount = state.momentumCount;
                event.meanReversionCount = state.meanReversionCount;
                event.noiseTraderCount = state.noiseTraderCount;
                event.realizedVolatilityBps = state.realizedVolatilityBps; // primary symbol stats for initial connect
                event.averageSpread = state.averageSpread;
                event.toxicityScore = state.toxicityScore;
                event.regime = state.regime;
                event.limitLambda = state.limitLambda;
                event.cancelLambda = state.cancelLambda;
                event.marketableLambda = state.marketableLambda;
                ws->send(simStateEnvelope(event), uWS::OpCode::TEXT);
            }
        };

        app.options("/*", [](auto* res, auto* /*req*/) {
            applyCors(res);
            res->writeStatus("204 No Content");
            res->end("");
        });

        app.get("/api/health", [&runtime](auto* res, auto* /*req*/) {
            const auto state = runtime.getState();
            writeJson(res, "200 OK", json{
                {"status", "ok"},
                {"running", runtime.isRunning()},
                {"replayActive", runtime.isReplayActive()},
                {"simulationEnabled", state.simulationEnabled},
                {"simulationPaused", state.simulationPaused}
            });
        });

        app.get("/api/state", [&runtime, &publisher](auto* res, auto* /*req*/) {
            writeJson(res, "200 OK", stateToJson(runtime.getState(), publisher.connectionCount()));
        });

        gateway.attach(app);

        app.post("/api/simulation/control", [&runtime](auto* res, auto* /*req*/) {
            auto body = std::make_shared<std::string>();

            res->onAborted([body]() {
                (void) body;
            });

            res->onData([body, res, &runtime](std::string_view chunk, bool isLast) {
                body->append(chunk.data(), chunk.size());
                if (!isLast) {
                    return;
                }

                try {
                    const auto parsed = json::parse(*body);
                    SimulationControl control;
                    control.action = parsed.value("action", std::string());
                    control.volatility = parsed.value("volatility", std::string("normal"));

                    if (!runtime.applyControl(control)) {
                        writeJson(res, "400 Bad Request", json{
                            {"error", "invalid_control"},
                            {"message", "Unsupported simulation control action"}
                        });
                        return;
                    }

                    writeJson(res, "200 OK", json{
                        {"status", "ok"},
                        {"simulation", stateToJson(runtime.getState(), 0)["simulation"]}
                    });
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
            .open = [&publisher, &sendSnapshot, &sendSimulationState](auto* ws) {
                ws->subscribe(MarketDataPublisher::TOPIC);
                ws->getUserData()->depth = DEFAULT_L2_DEPTH;
                publisher.incrementConnections();
                sendSnapshot(ws, DEFAULT_L2_DEPTH);
                sendSimulationState(ws);
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

        // Binary market data — raw packed structs for book_delta and trade.
        // No snapshot on connect; binary clients should use the JSON WebSocket
        // or GET /api/state for initial state.
        app.ws<PerSocketData>("/ws/market/bin", {
            .compression = uWS::DISABLED,
            .maxPayloadLength = 1024 * 1024,
            .idleTimeout = 30,
            .maxBackpressure = 1024 * 1024,
            .closeOnBackpressureLimit = false,
            .resetIdleTimeoutOnSend = false,
            .sendPingsAutomatically = true,
            .open = [&publisher](auto* ws) {
                ws->subscribe(MarketDataPublisher::TOPIC_BIN);
                publisher.incrementConnections();
            },
            .message = [](auto* /*ws*/, std::string_view /*message*/, uWS::OpCode /*opCode*/) {
                // Binary path is read-only; ignore client messages.
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
            runtime.stop();
            return 1;
        }

        app.run();

        publisher.detach();
        runtime.removeSubscriber(&publisher);
        runtime.stopReplay();
        runtime.stop();
        return 0;
    }

}
