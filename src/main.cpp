#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include "MarketRuntime.h"
#include "ServerApp.h"

namespace {

void printUsage() {
    std::cout << "Usage: mercury [mode] [options]\n\n";
    std::cout << "Modes:\n";
    std::cout << "  --server           Run the localhost HTTP/WebSocket server\n";
    std::cout << "  --sim              Enable the living simulation runtime\n";
    std::cout << "  --headless         Run the simulation headlessly (accelerated)\n\n";
    std::cout << "Simulation options:\n";
    std::cout << "  --sim-speed <x>    Simulation speed multiplier\n";
    std::cout << "  --sim-seed <n>     RNG seed for deterministic runs\n";
    std::cout << "  --sim-volatility <low|normal|high>  Simulation volatility preset\n";
    std::cout << "  --mm-count <n>     Passive market maker count\n";
    std::cout << "  --mom-count <n>    Aggressive momentum trader count\n";
    std::cout << "  --mr-count <n>     Mean-reversion bot count\n";
    std::cout << "  --noise-count <n>  Poisson-flow noise trader count\n";
    std::cout << "  --sim-duration-ms <ms>  Headless simulation duration (default 30000)\n\n";
    std::cout << "Replay options:\n";
    std::cout << "  --replay <file>    Feed a replay CSV into server/headless mode\n";
    std::cout << "  --replay-speed <x> Replay speed multiplier\n";
    std::cout << "  --replay-loop      Loop the replay file continuously\n";
    std::cout << "  --replay-loop-pause <ms>  Pause between replay loops (default 1000)\n\n";
    std::cout << "Server options:\n";
    std::cout << "  --port <port>      Server port (default: 9001)\n";
    std::cout << "  --host <host>      Server host (default: 127.0.0.1)\n";
    std::cout << "  --symbol <name>    API-level symbol label(s), comma-separated\n";
}

int runHeadlessSimulation(const Mercury::ServerOptions& options) {
    Mercury::SimulationConfig config = options.simulation;
    config.enabled = true;
    config.headless = true;

    Mercury::MarketRuntime runtime(options.symbols, config);
    runtime.start();

    if (options.replayFile) {
        if (!runtime.startReplay(*options.replayFile, options.replaySpeed,
                                 options.replayLoop, options.replayLoopPauseMs)) {
            std::cerr << "Failed to start replay from " << *options.replayFile << "\n";
            runtime.stop();
            return 1;
        }
    }

    const uint64_t targetSimTime = std::max<uint64_t>(1000, config.headlessDurationMs);
    while (runtime.isRunning()) {
        auto state = runtime.getState();
        if (state.simulationTimestamp >= targetSimTime) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    const auto summary = runtime.getHeadlessSummary();
    runtime.stopReplay();
    runtime.stop();

    std::cout << "\n========================================\n";
    std::cout << "   Mercury Headless Simulation\n";
    std::cout << "========================================\n";
    std::cout << "Sim Time:      " << summary.simulationTimestamp << " ms\n";
    std::cout << "Trades:        " << summary.tradeCount << "\n";
    std::cout << "Volume:        " << summary.totalVolume << "\n";
    std::cout << "Orders in Book:" << summary.orderCount << "\n";
    std::cout << "Mid Price:     " << summary.lastMidPrice << "\n";
    std::cout << "Realized Vol:  " << std::fixed << std::setprecision(2)
              << summary.realizedVolatilityBps << " bps\n";
    std::cout << "Avg Spread:    " << std::fixed << std::setprecision(2)
              << summary.averageSpread << "\n";
    std::cout << "========================================\n";

    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    std::cout << "Initializing Mercury Trading Engine...\n";
    std::cout << "Hardware concurrency: " << std::thread::hardware_concurrency() << " threads\n";

    Mercury::ServerOptions serverOptions;
    bool runServerModeFlag = false;
    bool headlessSimulation = false;
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
        } else if (arg == "--sim-speed" && i + 1 < argc) {
            serverOptions.simulation.speed = std::stod(argv[++i]);
            serverOptions.simulation.clockMode = serverOptions.simulation.speed > 1.0
                ? Mercury::SimulationClockMode::Accelerated
                : Mercury::SimulationClockMode::Realtime;
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
        } else if (arg == "--replay" && i + 1 < argc) {
            serverOptions.replayFile = argv[++i];
        } else if (arg == "--replay-speed" && i + 1 < argc) {
            serverOptions.replaySpeed = std::stod(argv[++i]);
        } else if (arg == "--replay-loop") {
            serverOptions.replayLoop = true;
        } else if (arg == "--replay-loop-pause" && i + 1 < argc) {
            serverOptions.replayLoopPauseMs = static_cast<uint64_t>(std::stoull(argv[++i]));
        } else if (arg == "--port" && i + 1 < argc) {
            serverOptions.port = std::stoi(argv[++i]);
        } else if (arg == "--host" && i + 1 < argc) {
            serverOptions.host = argv[++i];
        } else if (arg == "--symbol" && i + 1 < argc) {
            std::string syms = argv[++i];
            serverOptions.symbols.clear();
            size_t pos = 0;
            while ((pos = syms.find(',')) != std::string::npos) {
                if (pos > 0) serverOptions.symbols.push_back(syms.substr(0, pos));
                syms.erase(0, pos + 1);
            }
            if (!syms.empty()) {
                serverOptions.symbols.push_back(syms);
            }
        } else if (arg == "--help" || arg == "-h") {
            showHelp = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage();
            return 1;
        }
    }

    if (showHelp && !runServerModeFlag && !headlessSimulation && !serverOptions.simulation.enabled) {
        printUsage();
        return 0;
    }

    if (runServerModeFlag) {
        return Mercury::runServer(serverOptions);
    }

    if (serverOptions.simulation.enabled || headlessSimulation) {
        return runHeadlessSimulation(serverOptions);
    }

    printUsage();
    return 0;
}
