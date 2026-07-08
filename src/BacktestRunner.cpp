#include "BacktestRunner.h"

#include "CSVParser.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Mercury {

    namespace {

        namespace fs = std::filesystem;

        class BacktestRecorder final : public MarketDataSink {
        public:
            void onBookDelta(const BookDelta& /*delta*/) override {}

            void onTradeEvent(const TradeEvent& trade) override {
                std::lock_guard<std::mutex> lock(mutex_);
                events_.trades.push_back(trade);
            }

            void onStatsEvent(const StatsEvent& stats) override {
                std::lock_guard<std::mutex> lock(mutex_);
                events_.stats.push_back(stats);
            }

            void onPnLEvent(const PnLEvent& pnl) override {
                std::lock_guard<std::mutex> lock(mutex_);
                events_.pnl.push_back(pnl);
            }

            void onSimulationState(const SimulationStateEvent& state) override {
                std::lock_guard<std::mutex> lock(mutex_);
                events_.simStates.push_back(state);
            }

            void onAgentMetrics(const AgentMetricsEvent& metrics) override {
                std::lock_guard<std::mutex> lock(mutex_);
                events_.agentMetrics.push_back(metrics);
            }

            BacktestEventLog snapshot() const {
                std::lock_guard<std::mutex> lock(mutex_);
                return events_;
            }

        private:
            mutable std::mutex mutex_;
            BacktestEventLog events_;
        };

        template <typename T>
        void applyNumericOverride(const nlohmann::json& object, const char* key, T& target) {
            if (object.contains(key)) {
                target = object.at(key).get<T>();
            }
        }

        nlohmann::json nullableInt(const std::optional<int64_t>& value) {
            return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
        }

        nlohmann::json tradeEventToJson(const TradeEvent& trade) {
            return {
                {"sequence", trade.sequence},
                {"symbol", trade.symbol},
                {"tradeId", trade.tradeId},
                {"price", trade.price},
                {"quantity", trade.quantity},
                {"buyOrderId", trade.buyOrderId},
                {"sellOrderId", trade.sellOrderId},
                {"buyClientId", trade.buyClientId},
                {"sellClientId", trade.sellClientId},
                {"timestamp", trade.timestamp},
                {"engineLatencyNs", trade.engineLatencyNs}
            };
        }

        nlohmann::json statsEventToJson(const StatsEvent& stats) {
            return {
                {"sequence", stats.sequence},
                {"symbol", stats.symbol},
                {"tradeCount", stats.tradeCount},
                {"totalVolume", stats.totalVolume},
                {"orderCount", stats.orderCount},
                {"bidLevels", stats.bidLevels},
                {"askLevels", stats.askLevels},
                {"bestBid", nullableInt(stats.bestBid)},
                {"bestAsk", nullableInt(stats.bestAsk)},
                {"spread", stats.spread},
                {"midPrice", stats.midPrice},
                {"timestamp", stats.timestamp},
                {"messagesPerSecond", stats.messagesPerSecond}
            };
        }

        nlohmann::json pnlEventToJson(const PnLEvent& pnl) {
            return {
                {"sequence", pnl.sequence},
                {"symbol", pnl.symbol},
                {"clientId", pnl.clientId},
                {"netPosition", pnl.netPosition},
                {"realizedPnL", pnl.realizedPnL},
                {"unrealizedPnL", pnl.unrealizedPnL},
                {"totalPnL", pnl.totalPnL},
                {"timestamp", pnl.timestamp}
            };
        }

        nlohmann::json simStateEventToJson(const SimulationStateEvent& state) {
            return {
                {"sequence", state.sequence},
                {"symbol", state.symbol},
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
                {"marketableLambda", state.marketableLambda},
                {"marketMakerLevels", state.marketMakerLevels},
                {"marketMakerQuoteQuantity", state.marketMakerQuoteQuantity},
                {"marketMakerMinQuantity", state.marketMakerMinQuantity},
                {"marketMakerBaseSpreadTicks", state.marketMakerBaseSpreadTicks},
                {"marketMakerToxicitySensitivity", state.marketMakerToxicitySensitivity},
                {"marketMakerWakeIntervalMs", state.marketMakerWakeIntervalMs},
                {"marketMakerInventorySkewDivisor", state.marketMakerInventorySkewDivisor}
            };
        }

        nlohmann::json agentMetricsEventToJson(const AgentMetricsEvent& metrics) {
            return {
                {"sequence", metrics.sequence},
                {"symbol", metrics.symbol},
                {"clientId", metrics.clientId},
                {"agentName", metrics.agentName},
                {"agentType", metrics.agentType},
                {"timestamp", metrics.timestamp},
                {"simulationTimestamp", metrics.simulationTimestamp},
                {"wakeCount", metrics.wakeCount},
                {"intentCount", metrics.intentCount},
                {"submittedCount", metrics.submittedCount},
                {"limitOrderCount", metrics.limitOrderCount},
                {"marketOrderCount", metrics.marketOrderCount},
                {"cancelCount", metrics.cancelCount},
                {"modifyCount", metrics.modifyCount},
                {"fillCount", metrics.fillCount},
                {"filledQuantity", metrics.filledQuantity},
                {"restingQuantity", metrics.restingQuantity},
                {"liveOrderCount", metrics.liveOrderCount},
                {"netPosition", metrics.netPosition},
                {"realizedPnL", metrics.realizedPnL},
                {"unrealizedPnL", metrics.unrealizedPnL},
                {"totalPnL", metrics.totalPnL},
                {"averageQueuePosition", metrics.averageQueuePosition},
                {"averageQuantityAhead", metrics.averageQuantityAhead},
                {"averageFillProbability", metrics.averageFillProbability},
                {"averageTimeToFillMs", metrics.averageTimeToFillMs}
            };
        }

        template <typename T, typename Mapper>
        nlohmann::json mapJsonArray(const std::vector<T>& rows, Mapper mapper) {
            auto out = nlohmann::json::array();
            for (const auto& row : rows) {
                out.push_back(mapper(row));
            }
            return out;
        }

        void printSingleRunSummary(const ServerOptions& options,
                                   const BacktestSummary& summary,
                                   const std::optional<fs::path>& outputDir) {
            std::cout << "\n========================================\n";
            std::cout << (options.simulation.clockMode == SimulationClockMode::Instant
                ? "   Mercury Instant Backtest\n"
                : "   Mercury Headless Simulation\n");
            std::cout << "========================================\n";
            std::cout << "Run:           " << summary.name << "\n";
            std::cout << "Clock Mode:    " << summary.clockMode << "\n";
            std::cout << "Sim Time:      " << summary.simulationTimestampMs << " ms\n";
            std::cout << "Wall Time:     " << summary.wallTimeMs << " ms\n";
            std::cout << "Effective x:   " << std::fixed << std::setprecision(2) << summary.effectiveSpeed << "\n";
            std::cout << "Trades:        " << summary.tradeCount << "\n";
            std::cout << "Volume:        " << summary.totalVolume << "\n";
            std::cout << "Orders in Book:" << summary.orderCount << "\n";
            std::cout << "Mid Price:     " << summary.lastMidPrice << "\n";
            std::cout << "Max Drawdown:  " << summary.maxDrawdownTicks << " ticks ("
                      << std::fixed << std::setprecision(2) << summary.maxDrawdownBps << " bps)\n";
            std::cout << "Final Regime:  " << summary.finalRegime << "\n";
            std::cout << "Final PnL:     " << summary.finalTotalPnL << "\n";
            std::cout << "Agents:        " << summary.agents.size() << "\n";
            std::cout << "Avg Queue Pos: " << std::fixed << std::setprecision(2)
                      << summary.queueAnalytics.averageQueuePosition << "\n";
            std::cout << "Realized Vol:  " << std::fixed << std::setprecision(2)
                      << summary.realizedVolatilityBps << " bps\n";
            std::cout << "Avg Spread:    " << std::fixed << std::setprecision(2)
                      << summary.averageSpread << "\n";
            if (outputDir) {
                std::cout << "Artifacts:     " << pathString(*outputDir) << "\n";
            }
            std::cout << "========================================\n";
        }

        void writeSweepSummary(const fs::path& outputDir,
                               const std::vector<BacktestSummary>& summaries) {
            fs::create_directories(outputDir);

            nlohmann::json json = nlohmann::json::array();
            std::ostringstream csv;
            csv << "name,seed,volatility,marketMakerCount,momentumCount,meanReversionCount,noiseTraderCount,"
                << "simulationTimestampMs,wallTimeMs,effectiveSpeed,tradeCount,totalVolume,orderCount,lastMidPrice,"
                << "realizedVolatilityBps,averageSpread,maxDrawdownTicks,maxDrawdownBps,finalRegime,finalToxicityScore,"
                << "finalTotalPnL,averageQueuePosition,averageFillProbability,averageTimeToFillMs\n";

            for (const auto& summary : summaries) {
                json.push_back(backtestSummaryToJson(summary));
                csv << csvEscape(summary.name) << ','
                    << summary.seed << ','
                    << csvEscape(summary.volatility) << ','
                    << summary.marketMakerCount << ','
                    << summary.momentumCount << ','
                    << summary.meanReversionCount << ','
                    << summary.noiseTraderCount << ','
                    << summary.simulationTimestampMs << ','
                    << summary.wallTimeMs << ','
                    << summary.effectiveSpeed << ','
                    << summary.tradeCount << ','
                    << summary.totalVolume << ','
                    << summary.orderCount << ','
                    << summary.lastMidPrice << ','
                    << summary.realizedVolatilityBps << ','
                    << summary.averageSpread << ','
                    << summary.maxDrawdownTicks << ','
                    << summary.maxDrawdownBps << ','
                    << csvEscape(summary.finalRegime) << ','
                    << summary.finalToxicityScore << ','
                    << summary.finalTotalPnL << ','
                    << summary.queueAnalytics.averageQueuePosition << ','
                    << summary.queueAnalytics.averageFillProbability << ','
                    << summary.queueAnalytics.averageTimeToFillMs << '\n';
            }

            writeTextFile(pathString(outputDir / "sweep_summary.json"), json.dump(2));
            writeTextFile(pathString(outputDir / "sweep_summary.csv"), csv.str());
        }

    }  // namespace

    std::string sanitizeRunName(const std::string& raw) {
        std::string out;
        out.reserve(raw.size());
        for (char ch : raw) {
            const bool ok =
                (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '_' ||
                ch == '-';
            out.push_back(ok ? ch : '_');
        }
        return out.empty() ? "backtest" : out;
    }

    std::string pathString(const std::filesystem::path& path) {
        return path.generic_string();
    }

    std::vector<std::string> parseSymbolList(std::string symbols) {
        std::vector<std::string> parsed;
        size_t pos = 0;
        while ((pos = symbols.find(',')) != std::string::npos) {
            if (pos > 0) {
                parsed.push_back(symbols.substr(0, pos));
            }
            symbols.erase(0, pos + 1);
        }
        if (!symbols.empty()) {
            parsed.push_back(symbols);
        }
        return parsed.empty() ? std::vector<std::string>{"SIM"} : parsed;
    }

    nlohmann::json readJsonFile(const std::filesystem::path& path) {
        std::ifstream in(path);
        if (!in) {
            throw std::runtime_error("Failed to read " + pathString(path));
        }
        nlohmann::json value;
        in >> value;
        return value;
    }

    void applyMarketMakerOverrides(MarketMakerConfig& config, const nlohmann::json& object) {
        applyNumericOverride(object, "levels", config.levels);
        applyNumericOverride(object, "quoteQuantity", config.quoteQuantity);
        applyNumericOverride(object, "quoteSize", config.quoteQuantity);
        applyNumericOverride(object, "minQuantity", config.minQuantity);
        applyNumericOverride(object, "minSize", config.minQuantity);
        applyNumericOverride(object, "baseSpreadTicks", config.baseSpreadTicks);
        applyNumericOverride(object, "spreadTicks", config.baseSpreadTicks);
        applyNumericOverride(object, "toxicitySensitivity", config.toxicitySensitivity);
        applyNumericOverride(object, "wakeIntervalMs", config.wakeIntervalMs);
        applyNumericOverride(object, "inventorySkewDivisor", config.inventorySkewDivisor);
    }

    void applyRunOverrides(ServerOptions& options, const nlohmann::json& run, bool forceInstant) {
        auto& config = options.simulation;
        const bool replaySpeedProvided = run.contains("replaySpeed");

        applyNumericOverride(run, "seed", config.seed);
        applyNumericOverride(run, "speed", config.speed);
        applyNumericOverride(run, "durationMs", config.headlessDurationMs);
        applyNumericOverride(run, "simDurationMs", config.headlessDurationMs);
        applyNumericOverride(run, "stepMs", config.stepMs);
        applyNumericOverride(run, "publishIntervalMs", config.publishIntervalMs);
        applyNumericOverride(run, "marketMakerCount", config.marketMakerCount);
        applyNumericOverride(run, "mmCount", config.marketMakerCount);
        applyNumericOverride(run, "momentumCount", config.momentumCount);
        applyNumericOverride(run, "momCount", config.momentumCount);
        applyNumericOverride(run, "meanReversionCount", config.meanReversionCount);
        applyNumericOverride(run, "mrCount", config.meanReversionCount);
        applyNumericOverride(run, "noiseTraderCount", config.noiseTraderCount);
        applyNumericOverride(run, "noiseCount", config.noiseTraderCount);
        applyNumericOverride(run, "replaySpeed", options.replaySpeed);
        applyNumericOverride(run, "replayLoopPauseMs", options.replayLoopPauseMs);

        if (run.contains("volatility")) {
            config.volatility = simulationVolatilityFromString(run.at("volatility").get<std::string>());
        }
        if (run.contains("marketMaker") && run.at("marketMaker").is_object()) {
            applyMarketMakerOverrides(config.marketMaker, run.at("marketMaker"));
        }
        if (run.contains("symbols")) {
            if (run.at("symbols").is_array()) {
                options.symbols = run.at("symbols").get<std::vector<std::string>>();
            } else {
                options.symbols = parseSymbolList(run.at("symbols").get<std::string>());
            }
        }
        if (run.contains("symbol")) {
            options.symbols = parseSymbolList(run.at("symbol").get<std::string>());
        }
        if (run.contains("replay")) {
            const auto replay = run.at("replay").get<std::string>();
            options.replayFile = replay.empty() ? std::optional<std::string>{} : std::optional<std::string>(replay);
        }
        if (run.contains("replayFile")) {
            const auto replay = run.at("replayFile").get<std::string>();
            options.replayFile = replay.empty() ? std::optional<std::string>{} : std::optional<std::string>(replay);
        }
        if (run.contains("replayLoop")) {
            options.replayLoop = run.at("replayLoop").get<bool>();
        }
        if (options.replayFile && !replaySpeedProvided && options.replaySpeed == 1.0) {
            options.replaySpeed = 1.0e12;
        }

        if (forceInstant) {
            config.enabled = true;
            config.headless = true;
            config.clockMode = SimulationClockMode::Instant;
        }
    }

    void applyScenarioDocument(ServerOptions& options, const nlohmann::json& document, bool forceInstant) {
        if (!document.is_object()) {
            throw std::runtime_error("Scenario file must contain a JSON object");
        }

        nlohmann::json merged = document;
        if (document.contains("simulation") && document.at("simulation").is_object()) {
            for (const auto& [key, value] : document.at("simulation").items()) {
                merged[key] = value;
            }
        }
        applyRunOverrides(options, merged, forceInstant);
    }

    nlohmann::json buildReplayCalibrationReport(const std::string& replayFile,
                                                const BacktestEventLog& events) {
        CSVParser parser;
        const auto orders = parser.parseFile(replayFile);

        uint64_t replayLimit = 0;
        uint64_t replayMarket = 0;
        uint64_t replayCancel = 0;
        uint64_t replayModify = 0;
        uint64_t replayBuy = 0;
        uint64_t replaySell = 0;
        uint64_t replayQuantity = 0;
        for (const auto& order : orders) {
            replayQuantity += order.quantity;
            if (order.side == Side::Buy) {
                ++replayBuy;
            } else {
                ++replaySell;
            }
            switch (order.orderType) {
                case OrderType::Limit: ++replayLimit; break;
                case OrderType::Market: ++replayMarket; break;
                case OrderType::Cancel: ++replayCancel; break;
                case OrderType::Modify: ++replayModify; break;
            }
        }

        uint64_t observedVolume = 0;
        for (const auto& trade : events.trades) {
            observedVolume += trade.quantity;
        }

        double spreadTotal = 0.0;
        double midTotal = 0.0;
        uint64_t spreadSamples = 0;
        uint64_t midSamples = 0;
        uint64_t depthSamples = 0;
        uint64_t orderCountTotal = 0;
        for (const auto& stats : events.stats) {
            if (stats.spread > 0) {
                spreadTotal += static_cast<double>(stats.spread);
                ++spreadSamples;
            }
            if (stats.midPrice > 0) {
                midTotal += static_cast<double>(stats.midPrice);
                ++midSamples;
            }
            orderCountTotal += stats.orderCount;
            ++depthSamples;
        }

        uint64_t latestSubmitted = 0;
        uint64_t latestCancels = 0;
        uint64_t latestFilledQuantity = 0;
        std::map<std::pair<std::string, uint64_t>, AgentMetricsEvent> latestAgentMetrics;
        for (const auto& metrics : events.agentMetrics) {
            latestAgentMetrics[{metrics.symbol, metrics.clientId}] = metrics;
        }
        for (const auto& [key, metrics] : latestAgentMetrics) {
            (void) key;
            latestSubmitted += metrics.submittedCount;
            latestCancels += metrics.cancelCount;
            latestFilledQuantity += metrics.filledQuantity;
        }

        const double replayAvgQty = orders.empty()
            ? 0.0
            : static_cast<double>(replayQuantity) / static_cast<double>(orders.size());
        const double observedAvgTradeSize = events.trades.empty()
            ? 0.0
            : static_cast<double>(observedVolume) / static_cast<double>(events.trades.size());
        const double avgSpread = spreadSamples == 0
            ? 0.0
            : spreadTotal / static_cast<double>(spreadSamples);
        const double avgMid = midSamples == 0
            ? 0.0
            : midTotal / static_cast<double>(midSamples);
        const double avgOrderCount = depthSamples == 0
            ? 0.0
            : static_cast<double>(orderCountTotal) / static_cast<double>(depthSamples);

        return nlohmann::json{
            {"replayFile", replayFile},
            {"target", {
                {"orderCount", orders.size()},
                {"parseErrors", parser.getParseErrorCount()},
                {"limitOrders", replayLimit},
                {"marketOrders", replayMarket},
                {"cancelOrders", replayCancel},
                {"modifyOrders", replayModify},
                {"buyOrders", replayBuy},
                {"sellOrders", replaySell},
                {"totalQuantity", replayQuantity},
                {"averageOrderQuantity", replayAvgQty}
            }},
            {"observed", {
                {"tradeCount", events.trades.size()},
                {"totalVolume", observedVolume},
                {"averageTradeSize", observedAvgTradeSize},
                {"averageSpread", avgSpread},
                {"averageMidPrice", avgMid},
                {"averageOrderCount", avgOrderCount},
                {"agentSubmittedCount", latestSubmitted},
                {"agentCancelCount", latestCancels},
                {"agentFilledQuantity", latestFilledQuantity}
            }},
            {"comparison", {
                {"volumeToReplayQuantity", replayQuantity > 0
                    ? static_cast<double>(observedVolume) / static_cast<double>(replayQuantity)
                    : 0.0},
                {"tradeCountToReplayOrders", !orders.empty()
                    ? static_cast<double>(events.trades.size()) / static_cast<double>(orders.size())
                    : 0.0},
                {"agentCancelToSubmitRatio", latestSubmitted > 0
                    ? static_cast<double>(latestCancels) / static_cast<double>(latestSubmitted)
                    : 0.0}
            }}
        };
    }

    void writeBacktestArtifacts(const std::filesystem::path& outputDir,
                                const ServerOptions& options,
                                const BacktestSummary& summary,
                                const BacktestEventLog& events) {
        fs::create_directories(outputDir);

        writeTextFile(pathString(outputDir / "summary.json"),
                      backtestSummaryToJson(summary).dump(2));

        nlohmann::json configJson{
            {"symbols", options.symbols},
            {"replayFile", options.replayFile ? nlohmann::json(*options.replayFile) : nlohmann::json(nullptr)},
            {"replaySpeed", options.replaySpeed},
            {"replayLoop", options.replayLoop},
            {"replayLoopPauseMs", options.replayLoopPauseMs},
            {"scenarioFile", options.scenarioFile ? nlohmann::json(*options.scenarioFile) : nlohmann::json(nullptr)},
            {"marketMakerConfigFile", options.marketMakerConfigFile ? nlohmann::json(*options.marketMakerConfigFile) : nlohmann::json(nullptr)},
            {"calibrationReplayFile", options.calibrationReplayFile ? nlohmann::json(*options.calibrationReplayFile) : nlohmann::json(nullptr)},
            {"simulation", {
                {"clockMode", simulationClockModeToString(options.simulation.clockMode)},
                {"seed", options.simulation.seed},
                {"volatility", simulationVolatilityToString(options.simulation.volatility)},
                {"speed", options.simulation.speed},
                {"durationMs", options.simulation.headlessDurationMs},
                {"marketMakerCount", options.simulation.marketMakerCount},
                {"momentumCount", options.simulation.momentumCount},
                {"meanReversionCount", options.simulation.meanReversionCount},
                {"noiseTraderCount", options.simulation.noiseTraderCount},
                {"stepMs", options.simulation.stepMs},
                {"publishIntervalMs", options.simulation.publishIntervalMs},
                {"marketMaker", {
                    {"levels", options.simulation.marketMaker.levels},
                    {"quoteQuantity", options.simulation.marketMaker.quoteQuantity},
                    {"minQuantity", options.simulation.marketMaker.minQuantity},
                    {"baseSpreadTicks", options.simulation.marketMaker.baseSpreadTicks},
                    {"toxicitySensitivity", options.simulation.marketMaker.toxicitySensitivity},
                    {"wakeIntervalMs", options.simulation.marketMaker.wakeIntervalMs},
                    {"inventorySkewDivisor", options.simulation.marketMaker.inventorySkewDivisor}
                }}
            }}
        };
        writeTextFile(pathString(outputDir / "config.json"), configJson.dump(2));
        writeTradesCsv(pathString(outputDir / "trades.csv"), events.trades);
        writeStatsCsv(pathString(outputDir / "stats.csv"), events.stats);
        writePnlCsv(pathString(outputDir / "pnl.csv"), events.pnl);
        writeSimStateCsv(pathString(outputDir / "sim_state.csv"), events.simStates);
        writeAgentMetricsCsv(pathString(outputDir / "agent_metrics.csv"), events.agentMetrics);
        writeAgentSummaryCsv(pathString(outputDir / "agent_summary.csv"), summary.agents);
        if (options.calibrationReplayFile) {
            writeTextFile(pathString(outputDir / "calibration.json"),
                          buildReplayCalibrationReport(*options.calibrationReplayFile, events).dump(2));
        }
    }

    BacktestRunResult runBacktestOnce(ServerOptions options,
                                      const std::string& runName,
                                      bool printSummary,
                                      const std::optional<std::filesystem::path>& outputDir) {
        SimulationConfig config = options.simulation;
        config.enabled = true;
        config.headless = true;
        options.simulation = config;

        MarketRuntime runtime(options.symbols, config);
        BacktestRecorder recorder;
        runtime.addSubscriber(&recorder);
        runtime.start();

        if (options.replayFile) {
            if (!runtime.startReplay(*options.replayFile, options.replaySpeed,
                                     options.replayLoop, options.replayLoopPauseMs)) {
                runtime.stop();
                throw std::runtime_error("Failed to start replay from " + *options.replayFile);
            }
        }

        const auto wallStart = std::chrono::steady_clock::now();
        const uint64_t targetSimTime = std::max<uint64_t>(1000, config.headlessDurationMs);
        while (runtime.isRunning()) {
            auto state = runtime.getState();
            if (state.simulationTimestamp >= targetSimTime) {
                break;
            }
            const auto pollSleep = config.clockMode == SimulationClockMode::Instant
                ? std::chrono::milliseconds(1)
                : std::chrono::milliseconds(20);
            std::this_thread::sleep_for(pollSleep);
        }

        const auto summary = runtime.getHeadlessSummary();
        const auto events = recorder.snapshot();
        const auto wallEnd = std::chrono::steady_clock::now();
        const auto wallMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(wallEnd - wallStart).count());
        runtime.stopReplay();
        runtime.stop();

        auto resultSummary = buildBacktestSummary(
            runName,
            config,
            options.symbols,
            summary,
            events,
            targetSimTime,
            wallMs);

        nlohmann::json calibration = nullptr;
        if (options.calibrationReplayFile) {
            calibration = buildReplayCalibrationReport(*options.calibrationReplayFile, events);
        }

        if (outputDir) {
            writeBacktestArtifacts(*outputDir, options, resultSummary, events);
        }

        if (printSummary) {
            printSingleRunSummary(options, resultSummary, outputDir);
        }

        return BacktestRunResult{std::move(resultSummary), std::move(events), std::move(calibration)};
    }

    BacktestSweepResult runBacktestSweepResults(ServerOptions baseOptions, bool printSummary) {
        if (!baseOptions.sweepFile) {
            throw std::runtime_error("Missing --sweep file");
        }

        const fs::path sweepPath(*baseOptions.sweepFile);
        const auto document = readJsonFile(sweepPath);
        const nlohmann::json* runs = nullptr;
        if (document.is_array()) {
            runs = &document;
        } else if (document.is_object() && document.contains("runs") && document.at("runs").is_array()) {
            runs = &document.at("runs");
        }

        if (runs == nullptr || runs->empty()) {
            throw std::runtime_error("Sweep file must contain a non-empty runs array");
        }

        std::vector<BacktestSummary> summaries;
        summaries.reserve(runs->size());

        if (printSummary) {
            std::cout << "\nMercury Backtest Sweep\n";
            std::cout << std::left
                      << std::setw(22) << "Run"
                      << std::right
                      << std::setw(10) << "Seed"
                      << std::setw(12) << "Vol"
                      << std::setw(10) << "Trades"
                      << std::setw(12) << "Volume"
                      << std::setw(12) << "Mid"
                      << std::setw(12) << "DD bps"
                      << std::setw(12) << "Eff x"
                      << "\n";
        }

        for (size_t i = 0; i < runs->size(); ++i) {
            const auto& run = runs->at(i);
            if (!run.is_object()) {
                throw std::runtime_error("Sweep run " + std::to_string(i) + " must be an object");
            }

            ServerOptions options = baseOptions;
            options.sweepFile = std::nullopt;
            applyRunOverrides(options, run, true);

            const std::string runName = run.value("name", "run_" + std::to_string(i + 1));
            const auto outputDir =
                options.backtestOutputDir
                    ? std::optional<fs::path>(fs::path(*options.backtestOutputDir) / sanitizeRunName(runName))
                    : std::nullopt;
            auto result = runBacktestOnce(options, runName, false, outputDir);
            summaries.push_back(result.summary);

            if (printSummary) {
                std::cout << std::left
                          << std::setw(22) << result.summary.name.substr(0, 21)
                          << std::right
                          << std::setw(10) << result.summary.seed
                          << std::setw(12) << result.summary.volatility
                          << std::setw(10) << result.summary.tradeCount
                          << std::setw(12) << result.summary.totalVolume
                          << std::setw(12) << result.summary.lastMidPrice
                          << std::setw(12) << std::fixed << std::setprecision(2) << result.summary.maxDrawdownBps
                          << std::setw(12) << std::fixed << std::setprecision(2) << result.summary.effectiveSpeed
                          << "\n";
            }
        }

        if (baseOptions.backtestOutputDir) {
            const fs::path outputRoot(*baseOptions.backtestOutputDir);
            writeSweepSummary(outputRoot, summaries);
            if (printSummary) {
                std::cout << "Artifacts: " << pathString(outputRoot) << "\n";
            }
        }

        return BacktestSweepResult{std::move(summaries)};
    }

    int runHeadlessSimulation(const ServerOptions& options) {
        try {
            const std::optional<fs::path> outputDir =
                options.backtestOutputDir
                    ? std::optional<fs::path>(fs::path(*options.backtestOutputDir))
                    : std::nullopt;
            runBacktestOnce(options, "backtest", true, outputDir);
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 1;
        }
    }

    int runBacktestSweep(ServerOptions baseOptions) {
        try {
            runBacktestSweepResults(std::move(baseOptions), true);
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 1;
        }
    }

    nlohmann::json backtestRunResultToJson(const BacktestRunResult& result) {
        auto agentSummary = nlohmann::json::array();
        for (const auto& agent : result.summary.agents) {
            agentSummary.push_back(agentSummaryToJson(agent));
        }

        return {
            {"summary", backtestSummaryToJson(result.summary)},
            {"artifacts", {
                {"trades", mapJsonArray(result.events.trades, tradeEventToJson)},
                {"stats", mapJsonArray(result.events.stats, statsEventToJson)},
                {"pnl", mapJsonArray(result.events.pnl, pnlEventToJson)},
                {"simState", mapJsonArray(result.events.simStates, simStateEventToJson)},
                {"agentMetrics", mapJsonArray(result.events.agentMetrics, agentMetricsEventToJson)},
                {"agentSummary", std::move(agentSummary)}
            }},
            {"calibration", result.calibration}
        };
    }

    nlohmann::json backtestSweepResultToJson(const BacktestSweepResult& result) {
        auto summaries = nlohmann::json::array();
        auto sweepRows = nlohmann::json::array();
        for (const auto& summary : result.summaries) {
            summaries.push_back(backtestSummaryToJson(summary));
            sweepRows.push_back({
                {"name", summary.name},
                {"seed", summary.seed},
                {"volatility", summary.volatility},
                {"marketMakerCount", summary.marketMakerCount},
                {"momentumCount", summary.momentumCount},
                {"meanReversionCount", summary.meanReversionCount},
                {"noiseTraderCount", summary.noiseTraderCount},
                {"simulationTimestampMs", summary.simulationTimestampMs},
                {"wallTimeMs", summary.wallTimeMs},
                {"effectiveSpeed", summary.effectiveSpeed},
                {"tradeCount", summary.tradeCount},
                {"totalVolume", summary.totalVolume},
                {"orderCount", summary.orderCount},
                {"lastMidPrice", summary.lastMidPrice},
                {"realizedVolatilityBps", summary.realizedVolatilityBps},
                {"averageSpread", summary.averageSpread},
                {"maxDrawdownTicks", summary.maxDrawdownTicks},
                {"maxDrawdownBps", summary.maxDrawdownBps},
                {"finalRegime", summary.finalRegime},
                {"finalToxicityScore", summary.finalToxicityScore},
                {"finalTotalPnL", summary.finalTotalPnL},
                {"averageQueuePosition", summary.queueAnalytics.averageQueuePosition},
                {"averageFillProbability", summary.queueAnalytics.averageFillProbability},
                {"averageTimeToFillMs", summary.queueAnalytics.averageTimeToFillMs}
            });
        }

        return {
            {"summaries", std::move(summaries)},
            {"sweepSummary", std::move(sweepRows)}
        };
    }

}  // namespace Mercury
