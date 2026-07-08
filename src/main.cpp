#include "BacktestRunner.h"
#include "ServerApp.h"

#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

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
    std::cout << "Scenario and calibration options:\n";
    std::cout << "  --scenario <file>  Apply a scenario JSON file\n";
    std::cout << "  --mm-config <file> Apply a market-maker config JSON file\n";
    std::cout << "  --calibrate-replay <file>  Run instant replay calibration and write calibration.json\n\n";
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

}  // namespace

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
        } else if (arg == "--scenario" && i + 1 < argc) {
            serverOptions.scenarioFile = argv[++i];
            try {
                Mercury::applyScenarioDocument(
                    serverOptions,
                    Mercury::readJsonFile(*serverOptions.scenarioFile),
                    instantBacktest || serverOptions.sweepFile.has_value());
            } catch (const std::exception& ex) {
                std::cerr << ex.what() << "\n";
                return 1;
            }
        } else if (arg == "--mm-config" && i + 1 < argc) {
            serverOptions.marketMakerConfigFile = argv[++i];
            try {
                const auto mmConfig = Mercury::readJsonFile(*serverOptions.marketMakerConfigFile);
                if (mmConfig.contains("marketMaker") && mmConfig.at("marketMaker").is_object()) {
                    Mercury::applyMarketMakerOverrides(serverOptions.simulation.marketMaker, mmConfig.at("marketMaker"));
                } else {
                    Mercury::applyMarketMakerOverrides(serverOptions.simulation.marketMaker, mmConfig);
                }
            } catch (const std::exception& ex) {
                std::cerr << ex.what() << "\n";
                return 1;
            }
        } else if (arg == "--calibrate-replay" && i + 1 < argc) {
            serverOptions.calibrationReplayFile = argv[++i];
            serverOptions.replayFile = *serverOptions.calibrationReplayFile;
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
            serverOptions.symbols = Mercury::parseSymbolList(argv[++i]);
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
        return Mercury::runBacktestSweep(serverOptions);
    }

    if (serverOptions.simulation.enabled || headlessSimulation) {
        return Mercury::runHeadlessSimulation(serverOptions);
    }

    printUsage();
    return 0;
}
