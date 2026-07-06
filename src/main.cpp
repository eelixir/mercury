#include <iostream>
#include <iomanip>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include "BacktestReport.h"
#include "MarketRuntime.h"
#include "ServerApp.h"

namespace {

namespace fs = std::filesystem;

class BacktestRecorder final : public Mercury::MarketDataSink {
public:
    void onBookDelta(const Mercury::BookDelta& /*delta*/) override {}

    void onTradeEvent(const Mercury::TradeEvent& trade) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.trades.push_back(trade);
    }

    void onStatsEvent(const Mercury::StatsEvent& stats) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.stats.push_back(stats);
    }

    void onPnLEvent(const Mercury::PnLEvent& pnl) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.pnl.push_back(pnl);
    }

    void onSimulationState(const Mercury::SimulationStateEvent& state) override {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.simStates.push_back(state);
    }

    Mercury::BacktestEventLog snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_;
    }

private:
    mutable std::mutex mutex_;
    Mercury::BacktestEventLog events_;
};

struct BacktestRunResult {
    Mercury::BacktestSummary summary;
    Mercury::BacktestEventLog events;
};

std::string sanitizeName(const std::string& raw) {
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

std::string pathString(const fs::path& path) {
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

void writeBacktestArtifacts(const fs::path& outputDir,
                            const Mercury::ServerOptions& options,
                            const Mercury::BacktestSummary& summary,
                            const Mercury::BacktestEventLog& events) {
    fs::create_directories(outputDir);

    Mercury::writeTextFile(pathString(outputDir / "summary.json"),
                           Mercury::backtestSummaryToJson(summary).dump(2));

    nlohmann::json configJson{
        {"symbols", options.symbols},
        {"replayFile", options.replayFile ? nlohmann::json(*options.replayFile) : nlohmann::json(nullptr)},
        {"replaySpeed", options.replaySpeed},
        {"replayLoop", options.replayLoop},
        {"replayLoopPauseMs", options.replayLoopPauseMs},
        {"simulation", {
            {"clockMode", Mercury::simulationClockModeToString(options.simulation.clockMode)},
            {"seed", options.simulation.seed},
            {"volatility", Mercury::simulationVolatilityToString(options.simulation.volatility)},
            {"speed", options.simulation.speed},
            {"durationMs", options.simulation.headlessDurationMs},
            {"marketMakerCount", options.simulation.marketMakerCount},
            {"momentumCount", options.simulation.momentumCount},
            {"meanReversionCount", options.simulation.meanReversionCount},
            {"noiseTraderCount", options.simulation.noiseTraderCount},
            {"stepMs", options.simulation.stepMs},
            {"publishIntervalMs", options.simulation.publishIntervalMs}
        }}
    };
    Mercury::writeTextFile(pathString(outputDir / "config.json"), configJson.dump(2));
    Mercury::writeTradesCsv(pathString(outputDir / "trades.csv"), events.trades);
    Mercury::writeStatsCsv(pathString(outputDir / "stats.csv"), events.stats);
    Mercury::writePnlCsv(pathString(outputDir / "pnl.csv"), events.pnl);
    Mercury::writeSimStateCsv(pathString(outputDir / "sim_state.csv"), events.simStates);
}

nlohmann::json readJsonFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to read " + pathString(path));
    }
    nlohmann::json value;
    in >> value;
    return value;
}

template <typename T>
void applyNumericOverride(const nlohmann::json& object, const char* key, T& target) {
    if (object.contains(key)) {
        target = object.at(key).get<T>();
    }
}

void applySweepRunOverrides(Mercury::ServerOptions& options, const nlohmann::json& run) {
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
        config.volatility = Mercury::simulationVolatilityFromString(run.at("volatility").get<std::string>());
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

    config.enabled = true;
    config.headless = true;
    config.clockMode = Mercury::SimulationClockMode::Instant;
}

void writeSweepSummary(const fs::path& outputDir,
                       const std::vector<Mercury::BacktestSummary>& summaries) {
    fs::create_directories(outputDir);

    nlohmann::json json = nlohmann::json::array();
    std::ostringstream csv;
    csv << "name,seed,volatility,marketMakerCount,momentumCount,meanReversionCount,noiseTraderCount,"
        << "simulationTimestampMs,wallTimeMs,effectiveSpeed,tradeCount,totalVolume,orderCount,lastMidPrice,"
        << "realizedVolatilityBps,averageSpread,maxDrawdownTicks,maxDrawdownBps,finalRegime,finalToxicityScore\n";

    for (const auto& summary : summaries) {
        json.push_back(Mercury::backtestSummaryToJson(summary));
        csv << Mercury::csvEscape(summary.name) << ','
            << summary.seed << ','
            << Mercury::csvEscape(summary.volatility) << ','
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
            << Mercury::csvEscape(summary.finalRegime) << ','
            << summary.finalToxicityScore << '\n';
    }

    Mercury::writeTextFile(pathString(outputDir / "sweep_summary.json"), json.dump(2));
    Mercury::writeTextFile(pathString(outputDir / "sweep_summary.csv"), csv.str());
}

void printUsage() {
    std::cout << "Usage: mercury [mode] [options]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  --server           Run the localhost HTTP/WebSocket server\n";
    std::cout << "  --sim              Enable the living simulation runtime\n";
    std::cout << "  --headless         Run the simulation headlessly (accelerated)\n";
    std::cout << "  --backtest         Run headless simulation as fast as possible\n\n";
    std::cout << "Simulation options:\n";
    std::cout << "  --sim-speed <x>    Simulation speed multiplier\n";
    std::cout << "  --sim-seed <n>     RNG seed for deterministic runs\n";
    std::cout << "  --sim-volatility <low|normal|high>  Simulation volatility preset\n";
    std::cout << "  --mm-count <n>     Passive market maker count\n";
    std::cout << "  --mom-count <n>    Aggressive momentum trader count\n";
    std::cout << "  --mr-count <n>     Mean-reversion bot count\n";
    std::cout << "  --noise-count <n>  Poisson-flow noise trader count\n";
    std::cout << "  --sim-duration-ms <ms>  Headless simulation duration (default 30000)\n\n";
    std::cout << "Backtest lab options:\n";
    std::cout << "  --backtest-output <dir>  Write summary/config/trade/stat/PnL artifacts\n";
    std::cout << "  --sweep <file>     Run multiple instant backtests from a JSON sweep file\n\n";
    std::cout << "Replay options:\n";
    std::cout << "  --replay <file>    Feed a replay CSV into server/headless/backtest mode\n";
    std::cout << "  --replay-speed <x> Replay speed multiplier\n";
    std::cout << "  --replay-loop      Loop the replay file continuously\n";
    std::cout << "  --replay-loop-pause <ms>  Pause between replay loops (default 1000)\n\n";
    std::cout << "Server options:\n";
    std::cout << "  --port <port>      Server port (default: 9001)\n";
    std::cout << "  --host <host>      Server host (default: 127.0.0.1)\n";
    std::cout << "  --symbol <name>    API-level symbol label(s), comma-separated\n";
}

BacktestRunResult runBacktestOnce(Mercury::ServerOptions options,
                                  const std::string& runName,
                                  bool printSummary,
                                  const std::optional<fs::path>& outputDir) {
    Mercury::SimulationConfig config = options.simulation;
    config.enabled = true;
    config.headless = true;
    options.simulation = config;

    Mercury::MarketRuntime runtime(options.symbols, config);
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
        const auto pollSleep = config.clockMode == Mercury::SimulationClockMode::Instant
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

    auto resultSummary = Mercury::buildBacktestSummary(
        runName,
        config,
        options.symbols,
        summary,
        events,
        targetSimTime,
        wallMs);

    if (outputDir) {
        writeBacktestArtifacts(*outputDir, options, resultSummary, events);
    }

    if (printSummary) {
        std::cout << "\n========================================\n";
        std::cout << (config.clockMode == Mercury::SimulationClockMode::Instant
            ? "   Mercury Instant Backtest\n"
            : "   Mercury Headless Simulation\n");
        std::cout << "========================================\n";
        std::cout << "Run:           " << resultSummary.name << "\n";
        std::cout << "Clock Mode:    " << resultSummary.clockMode << "\n";
        std::cout << "Sim Time:      " << resultSummary.simulationTimestampMs << " ms\n";
        std::cout << "Wall Time:     " << resultSummary.wallTimeMs << " ms\n";
        std::cout << "Effective x:   " << std::fixed << std::setprecision(2) << resultSummary.effectiveSpeed << "\n";
        std::cout << "Trades:        " << resultSummary.tradeCount << "\n";
        std::cout << "Volume:        " << resultSummary.totalVolume << "\n";
        std::cout << "Orders in Book:" << resultSummary.orderCount << "\n";
        std::cout << "Mid Price:     " << resultSummary.lastMidPrice << "\n";
        std::cout << "Max Drawdown:  " << resultSummary.maxDrawdownTicks << " ticks ("
                  << std::fixed << std::setprecision(2) << resultSummary.maxDrawdownBps << " bps)\n";
        std::cout << "Final Regime:  " << resultSummary.finalRegime << "\n";
        std::cout << "Realized Vol:  " << std::fixed << std::setprecision(2)
                  << resultSummary.realizedVolatilityBps << " bps\n";
        std::cout << "Avg Spread:    " << std::fixed << std::setprecision(2)
                  << resultSummary.averageSpread << "\n";
        if (outputDir) {
            std::cout << "Artifacts:     " << pathString(*outputDir) << "\n";
        }
        std::cout << "========================================\n";
    }

    return BacktestRunResult{std::move(resultSummary), std::move(events)};
}

int runHeadlessSimulation(const Mercury::ServerOptions& options) {
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

int runBacktestSweep(Mercury::ServerOptions baseOptions) {
    try {
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

        std::vector<Mercury::BacktestSummary> summaries;
        summaries.reserve(runs->size());

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

        for (size_t i = 0; i < runs->size(); ++i) {
            const auto& run = runs->at(i);
            if (!run.is_object()) {
                throw std::runtime_error("Sweep run " + std::to_string(i) + " must be an object");
            }

            Mercury::ServerOptions options = baseOptions;
            options.sweepFile = std::nullopt;
            applySweepRunOverrides(options, run);

            const std::string runName = run.value("name", "run_" + std::to_string(i + 1));
            const auto outputDir =
                options.backtestOutputDir
                    ? std::optional<fs::path>(fs::path(*options.backtestOutputDir) / sanitizeName(runName))
                    : std::nullopt;
            auto result = runBacktestOnce(options, runName, false, outputDir);
            summaries.push_back(result.summary);

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

        if (baseOptions.backtestOutputDir) {
            const fs::path outputRoot(*baseOptions.backtestOutputDir);
            writeSweepSummary(outputRoot, summaries);
            std::cout << "Artifacts: " << pathString(outputRoot) << "\n";
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    std::cout << "Initializing Mercury Trading Engine...\n";
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << " threads\n";

    Mercury::ServerOptions serverOptions;
    bool runServerModeFlag = false;
    bool headlessSimulation = false;
    bool instantBacktest = false;
    bool replaySpeedProvided = false;
    bool showHelp = (argc <= 1);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            runServerModeFlag = true;
        } else if (arg == "--sim") {
            serverOptions.simulation.enabled = true;
        } else if (arg == "--headless") {
            headlessSimulation = true;
            serverOptions.simulation.enabled = true;
            serverOptions.simulation.headless = true;
            serverOptions.simulation.clockMode = Mercury::SimulationClockMode::Accelerated;
        } else if (arg == "--backtest" || arg == "--instant-backtest") {
            instantBacktest = true;
            headlessSimulation = true;
            serverOptions.simulation.enabled = true;
            serverOptions.simulation.headless = true;
        } else if (arg == "--sim-speed" && i + 1 < argc) {
            serverOptions.simulation.speed = std::stod(argv[++i]);
            if (!instantBacktest) {
                serverOptions.simulation.clockMode = serverOptions.simulation.speed > 1.0
                    ? Mercury::SimulationClockMode::Accelerated
                    : Mercury::SimulationClockMode::Realtime;
            }
        } else if (arg == "--sim-seed" && i + 1 < argc) {
            serverOptions.simulation.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--sim-volatility" && i + 1 < argc) {
            serverOptions.simulation.volatility =
                Mercury::simulationVolatilityFromString(argv[++i]);
        } else if (arg == "--mm-count" && i + 1 < argc) {
            serverOptions.simulation.marketMakerCount = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--mom-count" && i + 1 < argc) {
            serverOptions.simulation.momentumCount = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--mr-count" && i + 1 < argc) {
            serverOptions.simulation.meanReversionCount = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--noise-count" && i + 1 < argc) {
            serverOptions.simulation.noiseTraderCount = static_cast<size_t>(std::stoull(argv[++i]));
        } else if (arg == "--sim-duration-ms" && i + 1 < argc) {
            serverOptions.simulation.headlessDurationMs = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else if (arg == "--backtest-output" && i + 1 < argc) {
            serverOptions.backtestOutputDir = argv[++i];
        } else if (arg == "--sweep" && i + 1 < argc) {
            serverOptions.sweepFile = argv[++i];
            instantBacktest = true;
            headlessSimulation = true;
            serverOptions.simulation.enabled = true;
            serverOptions.simulation.headless = true;
        } else if (arg == "--replay" && i + 1 < argc) {
            serverOptions.replayFile = argv[++i];
        } else if (arg == "--replay-speed" && i + 1 < argc) {
            serverOptions.replaySpeed = std::stod(argv[++i]);
            replaySpeedProvided = true;
        } else if (arg == "--replay-loop") {
            serverOptions.replayLoop = true;
        } else if (arg == "--replay-loop-pause" && i + 1 < argc) {
            serverOptions.replayLoopPauseMs = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else if (arg == "--port" && i + 1 < argc) {
            serverOptions.port = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            serverOptions.host = argv[++i];
        } else if (arg == "--symbol" && i + 1 < argc) {
            serverOptions.symbols = parseSymbolList(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            showHelp = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    if (instantBacktest) {
        serverOptions.simulation.clockMode = Mercury::SimulationClockMode::Instant;
        if (serverOptions.replayFile && !replaySpeedProvided) {
            serverOptions.replaySpeed = 1.0e12;
        }
    }

    if (showHelp && !runServerModeFlag && !headlessSimulation && !serverOptions.simulation.enabled) {
        printUsage();
        return 0;
    }

    if (runServerModeFlag && (instantBacktest || serverOptions.sweepFile)) {
        std::cerr << "--backtest and --sweep are headless-only; use --server --sim for live real-time simulation\n";
        return 1;
    }

    if (runServerModeFlag) {
        return Mercury::runServer(serverOptions);
    }

    if (serverOptions.sweepFile) {
        return runBacktestSweep(serverOptions);
    }

    if (serverOptions.simulation.enabled || headlessSimulation) {
        return runHeadlessSimulation(serverOptions);
    }

    printUsage();
    return 0;
}
