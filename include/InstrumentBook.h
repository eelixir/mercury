#pragma once

#include "MatchingEngine.h"
#include "PnLTracker.h"

#include <string>

namespace Mercury {

    // Per-symbol bundle of matching engine and PnL tracker.
    // EngineService holds one of these for each registered instrument.
    struct InstrumentBook {
        std::string symbol;
        MatchingEngine engine;
        PnLTracker pnlTracker;

        explicit InstrumentBook(std::string sym)
            : symbol(std::move(sym)) {}
    };

}
