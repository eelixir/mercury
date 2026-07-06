#pragma once

#include "MarketData.h"
#include "MarketRuntime.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Mercury {

    struct BacktestEventLog {
        std::vector<TradeEvent> trades;
        std::vector<StatsEvent> stats;
        std::vector<PnLEvent> pnl;
        std::vector<SimulationStateEvent> simStates;
    };

    struct BacktestSummary {
        std::string name = "backtest";
        std::string clockMode = "instant";
        std::vector<std::string> symbols;
        std::string volatility = "normal";
        uint32_t seed = 42;
        uint64_t requestedDurationMs = 0;
        uint64_t simulationTimestampMs = 0;
        uint64_t wallTimeMs = 0;
        double effectiveSpeed = 0.0;
        uint64_t tradeCount = 0;
        uint64_t totalVolume = 0;
        size_t orderCount = 0;
        int64_t lastMidPrice = 0;
        double realizedVolatilityBps = 0.0;
        double averageSpread = 0.0;
        int64_t minMidPrice = 0;
        int64_t maxMidPrice = 0;
        int64_t maxDrawdownTicks = 0;
        double maxDrawdownBps = 0.0;
        std::string finalRegime = "normal";
        double finalToxicityScore = 0.0;
        size_t marketMakerCount = 0;
        size_t momentumCount = 0;
        size_t meanReversionCount = 0;
        size_t noiseTraderCount = 0;
    };

    inline nlohmann::json backtestSummaryToJson(const BacktestSummary& summary) {
        return {
            {"name", summary.name},
            {"clockMode", summary.clockMode},
            {"symbols", summary.symbols},
            {"volatility", summary.volatility},
            {"seed", summary.seed},
            {"requestedDurationMs", summary.requestedDurationMs},
            {"simulationTimestampMs", summary.simulationTimestampMs},
            {"wallTimeMs", summary.wallTimeMs},
            {"effectiveSpeed", summary.effectiveSpeed},
            {"tradeCount", summary.tradeCount},
            {"totalVolume", summary.totalVolume},
            {"orderCount", summary.orderCount},
            {"lastMidPrice", summary.lastMidPrice},
            {"realizedVolatilityBps", summary.realizedVolatilityBps},
            {"averageSpread", summary.averageSpread},
            {"minMidPrice", summary.minMidPrice},
            {"maxMidPrice", summary.maxMidPrice},
            {"maxDrawdownTicks", summary.maxDrawdownTicks},
            {"maxDrawdownBps", summary.maxDrawdownBps},
            {"finalRegime", summary.finalRegime},
            {"finalToxicityScore", summary.finalToxicityScore},
            {"agents", {
                {"marketMakerCount", summary.marketMakerCount},
                {"momentumCount", summary.momentumCount},
                {"meanReversionCount", summary.meanReversionCount},
                {"noiseTraderCount", summary.noiseTraderCount}
            }}
        };
    }

    inline BacktestSummary buildBacktestSummary(const std::string& name,
                                                const SimulationConfig& config,
                                                const std::vector<std::string>& symbols,
                                                const HeadlessSummary& runtimeSummary,
                                                const BacktestEventLog& events,
                                                uint64_t requestedDurationMs,
                                                uint64_t wallTimeMs) {
        BacktestSummary summary;
        summary.name = name.empty() ? "backtest" : name;
        summary.clockMode = simulationClockModeToString(config.clockMode);
        summary.symbols = symbols;
        summary.volatility = simulationVolatilityToString(config.volatility);
        summary.seed = config.seed;
        summary.requestedDurationMs = requestedDurationMs;
        summary.simulationTimestampMs = runtimeSummary.simulationTimestamp;
        summary.wallTimeMs = wallTimeMs;
        summary.effectiveSpeed = wallTimeMs > 0
            ? static_cast<double>(runtimeSummary.simulationTimestamp) / static_cast<double>(wallTimeMs)
            : 0.0;
        summary.tradeCount = runtimeSummary.tradeCount;
        summary.totalVolume = runtimeSummary.totalVolume;
        summary.orderCount = runtimeSummary.orderCount;
        summary.lastMidPrice = runtimeSummary.lastMidPrice;
        summary.realizedVolatilityBps = runtimeSummary.realizedVolatilityBps;
        summary.averageSpread = runtimeSummary.averageSpread;
        summary.marketMakerCount = config.marketMakerCount;
        summary.momentumCount = config.momentumCount;
        summary.meanReversionCount = config.meanReversionCount;
        summary.noiseTraderCount = config.noiseTraderCount;

        int64_t peak = 0;
        for (const auto& stats : events.stats) {
            if (stats.midPrice <= 0) {
                continue;
            }
            if (summary.minMidPrice == 0 || stats.midPrice < summary.minMidPrice) {
                summary.minMidPrice = stats.midPrice;
            }
            summary.maxMidPrice = std::max(summary.maxMidPrice, stats.midPrice);
            peak = std::max(peak, stats.midPrice);
            if (peak > 0) {
                const int64_t drawdown = peak - stats.midPrice;
                if (drawdown > summary.maxDrawdownTicks) {
                    summary.maxDrawdownTicks = drawdown;
                    summary.maxDrawdownBps = static_cast<double>(drawdown) / static_cast<double>(peak) * 10000.0;
                }
            }
        }

        if (!events.simStates.empty()) {
            const auto& state = events.simStates.back();
            summary.finalRegime = state.regime;
            summary.finalToxicityScore = state.toxicityScore;
        }

        return summary;
    }

    inline void writeTextFile(const std::string& path, const std::string& contents) {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to write " + path);
        }
        out << contents;
    }

    inline std::string csvEscape(const std::string& value) {
        if (value.find_first_of(",\"\n\r") == std::string::npos) {
            return value;
        }
        std::string escaped = "\"";
        for (char ch : value) {
            if (ch == '"') {
                escaped += "\"\"";
            } else {
                escaped += ch;
            }
        }
        escaped += '"';
        return escaped;
    }

    inline void writeTradesCsv(const std::string& path, const std::vector<TradeEvent>& trades) {
        std::ostringstream out;
        out << "sequence,symbol,tradeId,price,quantity,buyOrderId,sellOrderId,buyClientId,sellClientId,timestamp,engineLatencyNs\n";
        for (const auto& trade : trades) {
            out << trade.sequence << ','
                << csvEscape(trade.symbol) << ','
                << trade.tradeId << ','
                << trade.price << ','
                << trade.quantity << ','
                << trade.buyOrderId << ','
                << trade.sellOrderId << ','
                << trade.buyClientId << ','
                << trade.sellClientId << ','
                << trade.timestamp << ','
                << trade.engineLatencyNs << '\n';
        }
        writeTextFile(path, out.str());
    }

    inline void writeStatsCsv(const std::string& path, const std::vector<StatsEvent>& statsEvents) {
        std::ostringstream out;
        out << "sequence,symbol,tradeCount,totalVolume,orderCount,bidLevels,askLevels,bestBid,bestAsk,spread,midPrice,timestamp,messagesPerSecond\n";
        for (const auto& stats : statsEvents) {
            out << stats.sequence << ','
                << csvEscape(stats.symbol) << ','
                << stats.tradeCount << ','
                << stats.totalVolume << ','
                << stats.orderCount << ','
                << stats.bidLevels << ','
                << stats.askLevels << ','
                << (stats.bestBid ? std::to_string(*stats.bestBid) : "") << ','
                << (stats.bestAsk ? std::to_string(*stats.bestAsk) : "") << ','
                << stats.spread << ','
                << stats.midPrice << ','
                << stats.timestamp << ','
                << stats.messagesPerSecond << '\n';
        }
        writeTextFile(path, out.str());
    }

    inline void writePnlCsv(const std::string& path, const std::vector<PnLEvent>& pnlEvents) {
        std::ostringstream out;
        out << "sequence,symbol,clientId,netPosition,realizedPnL,unrealizedPnL,totalPnL,timestamp\n";
        for (const auto& pnl : pnlEvents) {
            out << pnl.sequence << ','
                << csvEscape(pnl.symbol) << ','
                << pnl.clientId << ','
                << pnl.netPosition << ','
                << pnl.realizedPnL << ','
                << pnl.unrealizedPnL << ','
                << pnl.totalPnL << ','
                << pnl.timestamp << '\n';
        }
        writeTextFile(path, out.str());
    }

    inline void writeSimStateCsv(const std::string& path, const std::vector<SimulationStateEvent>& states) {
        std::ostringstream out;
        out << "sequence,symbol,enabled,running,paused,clockMode,speed,volatility,simulationTimestamp,marketMakerCount,momentumCount,meanReversionCount,noiseTraderCount,realizedVolatilityBps,averageSpread,toxicityScore,regime,limitLambda,cancelLambda,marketableLambda\n";
        for (const auto& state : states) {
            out << state.sequence << ','
                << csvEscape(state.symbol) << ','
                << state.enabled << ','
                << state.running << ','
                << state.paused << ','
                << csvEscape(state.clockMode) << ','
                << state.speed << ','
                << csvEscape(state.volatility) << ','
                << state.simulationTimestamp << ','
                << state.marketMakerCount << ','
                << state.momentumCount << ','
                << state.meanReversionCount << ','
                << state.noiseTraderCount << ','
                << state.realizedVolatilityBps << ','
                << state.averageSpread << ','
                << state.toxicityScore << ','
                << csvEscape(state.regime) << ','
                << state.limitLambda << ','
                << state.cancelLambda << ','
                << state.marketableLambda << '\n';
        }
        writeTextFile(path, out.str());
    }

}  // namespace Mercury
