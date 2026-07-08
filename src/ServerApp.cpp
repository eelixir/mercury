#include "ServerApp.h"

#include "BacktestRunner.h"
#include "MarketDataPublisher.h"
#include "OrderEntryGateway.h"
#include "ServerHelpers.h"

#include <App.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
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

        std::string jsonStringOrEmpty(const json& parsed, const char* key) {
            return parsed.contains(key) && parsed.at(key).is_string()
                ? parsed.at(key).get<std::string>()
                : std::string();
        }

        std::string scenarioFileFromRequest(const json& parsed) {
            auto scenarioFile = jsonStringOrEmpty(parsed, "scenarioFile");
            if (!scenarioFile.empty()) {
                return scenarioFile;
            }

            const auto scenarioId = jsonStringOrEmpty(parsed, "scenarioId");
            if (!scenarioId.empty()) {
                return (std::filesystem::path("scenarios") / (scenarioId + ".json")).string();
            }

            return {};
        }

        ServerOptions labOptionsFromRequest(const ServerOptions& baseOptions,
                                            const json& parsed,
                                            const std::string& mode) {
            ServerOptions labOptions = baseOptions;
            labOptions.replayFile = std::nullopt;
            labOptions.sweepFile = std::nullopt;
            labOptions.backtestOutputDir = std::nullopt;
            labOptions.scenarioFile = std::nullopt;
            labOptions.marketMakerConfigFile = std::nullopt;
            labOptions.calibrationReplayFile = std::nullopt;
            labOptions.replaySpeed = 1.0;
            labOptions.replayLoop = false;
            labOptions.replayLoopPauseMs = 1000;

            const bool forceInstant = mode != "headless";
            labOptions.simulation.enabled = true;
            labOptions.simulation.headless = true;
            labOptions.simulation.clockMode = forceInstant
                ? SimulationClockMode::Instant
                : SimulationClockMode::Accelerated;

            const auto scenarioFile = scenarioFileFromRequest(parsed);
            if (!scenarioFile.empty()) {
                labOptions.scenarioFile = scenarioFile;
                applyScenarioDocument(labOptions, readJsonFile(scenarioFile), forceInstant);
            }

            auto mmConfigFile = jsonStringOrEmpty(parsed, "marketMakerConfigFile");
            if (mmConfigFile.empty()) {
                mmConfigFile = jsonStringOrEmpty(parsed, "mmConfigFile");
            }
            if (!mmConfigFile.empty()) {
                labOptions.marketMakerConfigFile = mmConfigFile;
                const auto mmConfig = readJsonFile(mmConfigFile);
                if (mmConfig.contains("marketMaker") && mmConfig.at("marketMaker").is_object()) {
                    applyMarketMakerOverrides(labOptions.simulation.marketMaker, mmConfig.at("marketMaker"));
                } else {
                    applyMarketMakerOverrides(labOptions.simulation.marketMaker, mmConfig);
                }
            }

            applyRunOverrides(labOptions, parsed, forceInstant);

            const auto outputDir = jsonStringOrEmpty(parsed, "outputDir");
            if (!outputDir.empty()) {
                labOptions.backtestOutputDir = outputDir;
            }

            if (mode == "sweep") {
                const auto sweepFile = jsonStringOrEmpty(parsed, "sweepFile");
                if (sweepFile.empty()) {
                    throw std::runtime_error("sweepFile is required");
                }
                labOptions.sweepFile = sweepFile;
            }

            if (mode == "calibrate_replay") {
                auto replayFile = jsonStringOrEmpty(parsed, "calibrationReplayFile");
                if (replayFile.empty()) {
                    replayFile = jsonStringOrEmpty(parsed, "replayFile");
                }
                if (replayFile.empty()) {
                    throw std::runtime_error("replayFile is required for calibration");
                }
                labOptions.calibrationReplayFile = replayFile;
                labOptions.replayFile = replayFile;
                if (!parsed.contains("replaySpeed") && labOptions.replaySpeed == 1.0) {
                    labOptions.replaySpeed = 1.0e12;
                }
            }

            return labOptions;
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
                event.marketMakerLevels = state.marketMaker.levels;
                event.marketMakerQuoteQuantity = state.marketMaker.quoteQuantity;
                event.marketMakerMinQuantity = state.marketMaker.minQuantity;
                event.marketMakerBaseSpreadTicks = state.marketMaker.baseSpreadTicks;
                event.marketMakerToxicitySensitivity = state.marketMaker.toxicitySensitivity;
                event.marketMakerWakeIntervalMs = state.marketMaker.wakeIntervalMs;
                event.marketMakerInventorySkewDivisor = state.marketMaker.inventorySkewDivisor;
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

        app.get("/api/scenarios", [](auto* res, auto* /*req*/) {
            writeJson(res, "200 OK", json{
                {"scenarios", json::array({
                    json{{"id", "calm-two-sided-market"}, {"name", "Calm Two-Sided Market"}},
                    json{{"id", "toxic-flow"}, {"name", "Toxic Flow"}},
                    json{{"id", "thin-book-stress"}, {"name", "Thin Book Stress"}},
                    json{{"id", "high-cancel-rate"}, {"name", "High Cancel Rate"}},
                    json{{"id", "momentum-burst"}, {"name", "Momentum Burst"}}
                })}
            });
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
                    control.scenario = parsed.value("scenario", std::string());

                    if (parsed.contains("clockMode") || parsed.contains("speed")) {
                        const auto current = runtime.getState();
                        control.hasTiming = true;
                        control.clockMode = parsed.value("clockMode", current.clockMode);
                        control.speed = parsed.value("speed", current.simulationSpeed);
                    }

                    if (parsed.contains("marketMakerCount") ||
                        parsed.contains("momentumCount") ||
                        parsed.contains("meanReversionCount") ||
                        parsed.contains("noiseTraderCount")) {
                        const auto current = runtime.getState();
                        control.hasAgentCounts = true;
                        control.marketMakerCount = parsed.value("marketMakerCount", current.marketMakerCount);
                        control.momentumCount = parsed.value("momentumCount", current.momentumCount);
                        control.meanReversionCount = parsed.value("meanReversionCount", current.meanReversionCount);
                        control.noiseTraderCount = parsed.value("noiseTraderCount", current.noiseTraderCount);
                    }

                    if (parsed.contains("marketMaker") && parsed.at("marketMaker").is_object()) {
                        const auto current = runtime.getState().marketMaker;
                        const auto& mm = parsed.at("marketMaker");
                        control.hasMarketMakerConfig = true;
                        control.marketMaker.levels = mm.value("levels", current.levels);
                        control.marketMaker.quoteQuantity = mm.value("quoteQuantity", current.quoteQuantity);
                        control.marketMaker.minQuantity = mm.value("minQuantity", current.minQuantity);
                        control.marketMaker.baseSpreadTicks = mm.value("baseSpreadTicks", current.baseSpreadTicks);
                        control.marketMaker.toxicitySensitivity = mm.value("toxicitySensitivity", current.toxicitySensitivity);
                        control.marketMaker.wakeIntervalMs = mm.value("wakeIntervalMs", current.wakeIntervalMs);
                        control.marketMaker.inventorySkewDivisor = mm.value("inventorySkewDivisor", current.inventorySkewDivisor);
                    }

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

        app.post("/api/replay/control", [&runtime](auto* res, auto* /*req*/) {
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
                    const auto action = parsed.value("action", std::string());

                    if (action == "stop") {
                        runtime.stopReplay();
                        writeJson(res, "200 OK", json{
                            {"status", "ok"},
                            {"replayActive", runtime.isReplayActive()}
                        });
                        return;
                    }

                    if (action == "start") {
                        const auto replayFile = parsed.value("replayFile", std::string());
                        if (replayFile.empty()) {
                            writeJson(res, "400 Bad Request", json{
                                {"error", "invalid_request"},
                                {"message", "replayFile is required"}
                            });
                            return;
                        }

                        const auto speed = parsed.value("speed", 1.0);
                        const auto loop = parsed.value("loop", false);
                        const auto loopPauseMs = parsed.value("loopPauseMs", uint64_t{1000});
                        if (!runtime.startReplay(replayFile, speed, loop, loopPauseMs)) {
                            writeJson(res, "400 Bad Request", json{
                                {"error", "invalid_replay"},
                                {"message", "Failed to start replay"}
                            });
                            return;
                        }

                        writeJson(res, "200 OK", json{
                            {"status", "ok"},
                            {"replayActive", runtime.isReplayActive()}
                        });
                        return;
                    }

                    writeJson(res, "400 Bad Request", json{
                        {"error", "invalid_control"},
                        {"message", "Unsupported replay control action"}
                    });
                } catch (const std::exception& ex) {
                    writeJson(res, "400 Bad Request", json{
                        {"error", "invalid_request"},
                        {"message", ex.what()}
                    });
                }
            });
        });

        app.post("/api/lab/run", [&options](auto* res, auto* /*req*/) {
            auto body = std::make_shared<std::string>();

            res->onAborted([body]() {
                (void) body;
            });

            res->onData([body, res, &options](std::string_view chunk, bool isLast) {
                body->append(chunk.data(), chunk.size());
                if (!isLast) {
                    return;
                }

                try {
                    const auto parsed = json::parse(*body);
                    const auto mode = parsed.value("mode", std::string("backtest"));
                    if (mode != "backtest" && mode != "headless" &&
                        mode != "sweep" && mode != "calibrate_replay") {
                        writeJson(res, "400 Bad Request", json{
                            {"error", "invalid_lab_mode"},
                            {"message", "mode must be backtest, headless, sweep, or calibrate_replay"}
                        });
                        return;
                    }

                    auto labOptions = labOptionsFromRequest(options, parsed, mode);
                    const auto outputDir = labOptions.backtestOutputDir
                        ? std::optional<std::filesystem::path>(std::filesystem::path(*labOptions.backtestOutputDir))
                        : std::nullopt;

                    if (mode == "sweep") {
                        auto result = runBacktestSweepResults(std::move(labOptions), false);
                        auto response = backtestSweepResultToJson(result);
                        response["status"] = "ok";
                        response["mode"] = mode;
                        if (outputDir) {
                            response["outputDir"] = pathString(*outputDir);
                        }
                        writeJson(res, "200 OK", response);
                        return;
                    }

                    const auto fallbackName = mode == "calibrate_replay"
                        ? std::string("replay-calibration")
                        : mode;
                    const auto runName = parsed.value("name", fallbackName);
                    auto result = runBacktestOnce(std::move(labOptions), runName, false, outputDir);
                    auto response = backtestRunResultToJson(result);
                    response["status"] = "ok";
                    response["mode"] = mode;
                    if (outputDir) {
                        response["outputDir"] = pathString(*outputDir);
                    }
                    writeJson(res, "200 OK", response);
                } catch (const std::exception& ex) {
                    writeJson(res, "400 Bad Request", json{
                        {"error", "invalid_lab_request"},
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
