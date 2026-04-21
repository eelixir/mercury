#pragma once

#include "MarketRuntime.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Mercury {

    struct ServerOptions {
        std::string host = "127.0.0.1";
        int port = 9001;
        std::optional<std::string> replayFile;
        double replaySpeed = 1.0;
        bool replayLoop = false;
        uint64_t replayLoopPauseMs = 1000;
        std::vector<std::string> symbols = {"SIM"};
        SimulationConfig simulation;
    };

    int runServer(const ServerOptions& options);

}
