#pragma once

#include "MarketData.h"
#include "MarketRuntime.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Mercury {

    struct BacktestEventLog {
        std::vector<TradeEvent> trades;
        std::vector<StatsEvent> stats;
        std::vector<PnLEvent> pnl;
        std::vector<SimulationStateEvent> simStates;
        std::vector<AgentMetricsEvent> agentMetrics;
    };

    struct AgentAttributionSummary {
        std::string symbol;
        uint64_t clientId = 0;
        std::string agentName;
        std::string agentType;
        uint64_t wakeCount = 0;
        uint64_t submittedCount = 0;
        uint64_t limitOrderCount = 0;
        uint64_t marketOrderCount = 0;
        uint64_t cancelCount = 0;
        uint64_t modifyCount = 0;
        uint64_t fillCount = 0;
        uint64_t filledQuantity = 0;
        uint64_t restingQuantity = 0;
        size_t liveOrderCount = 0;
        int64_t netPosition = 0;
        int64_t realizedPnL = 0;
        int64_t unrealizedPnL = 0;
        int64_t totalPnL = 0;
        double averageQueuePosition = 0.0;
        double averageQuantityAhead = 0.0;
        double averageFillProbability = 0.0;
        double averageTimeToFillMs = 0.0;
        double adverseSelectionTicks = 0.0;
        double adverseSelectionBps = 0.0;
    };

    struct QueueAnalyticsSummary {
        uint64_t samples = 0;
        uint64_t liveOrderSamples = 0;
        double averageQueuePosition = 0.0;
        double averageQuantityAhead = 0.0;
        double averageFillProbability = 0.0;
        double averageTimeToFillMs = 0.0;
        uint64_t averageRestingQuantity = 0;
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
        int64_t finalTotalPnL = 0;
        QueueAnalyticsSummary queueAnalytics;
        std::vector<AgentAttributionSummary> agents;
    };

    inline nlohmann::json agentSummaryToJson(const AgentAttributionSummary& agent) {
        return {
            {"symbol", agent.symbol},
            {"clientId", agent.clientId},
            {"agentName", agent.agentName},
            {"agentType", agent.agentType},
            {"wakeCount", agent.wakeCount},
            {"submittedCount", agent.submittedCount},
            {"limitOrderCount", agent.limitOrderCount},
            {"marketOrderCount", agent.marketOrderCount},
            {"cancelCount", agent.cancelCount},
            {"modifyCount", agent.modifyCount},
            {"fillCount", agent.fillCount},
            {"filledQuantity", agent.filledQuantity},
            {"restingQuantity", agent.restingQuantity},
            {"liveOrderCount", agent.liveOrderCount},
            {"netPosition", agent.netPosition},
            {"realizedPnL", agent.realizedPnL},
            {"unrealizedPnL", agent.unrealizedPnL},
            {"totalPnL", agent.totalPnL},
            {"averageQueuePosition", agent.averageQueuePosition},
            {"averageQuantityAhead", agent.averageQuantityAhead},
            {"averageFillProbability", agent.averageFillProbability},
            {"averageTimeToFillMs", agent.averageTimeToFillMs},
            {"adverseSelectionTicks", agent.adverseSelectionTicks},
            {"adverseSelectionBps", agent.adverseSelectionBps}
        };
    }

    inline nlohmann::json backtestSummaryToJson(const BacktestSummary& summary) {
        nlohmann::json agents = nlohmann::json::array();
        for (const auto& agent : summary.agents) {
            agents.push_back(agentSummaryToJson(agent));
        }

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
            {"finalTotalPnL", summary.finalTotalPnL},
            {"agentCounts", {
                {"marketMakerCount", summary.marketMakerCount},
                {"momentumCount", summary.momentumCount},
                {"meanReversionCount", summary.meanReversionCount},
                {"noiseTraderCount", summary.noiseTraderCount}
            }},
            {"queueAnalytics", {
                {"samples", summary.queueAnalytics.samples},
                {"liveOrderSamples", summary.queueAnalytics.liveOrderSamples},
                {"averageQueuePosition", summary.queueAnalytics.averageQueuePosition},
                {"averageQuantityAhead", summary.queueAnalytics.averageQuantityAhead},
                {"averageFillProbability", summary.queueAnalytics.averageFillProbability},
                {"averageTimeToFillMs", summary.queueAnalytics.averageTimeToFillMs},
                {"averageRestingQuantity", summary.queueAnalytics.averageRestingQuantity}
            }},
            {"agents", std::move(agents)}
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

        std::map<std::pair<std::string, uint64_t>, AgentAttributionSummary> byAgent;
        double queuePositionTotal = 0.0;
        double quantityAheadTotal = 0.0;
        double fillProbabilityTotal = 0.0;
        double fillAgeTotal = 0.0;
        uint64_t restingQuantityTotal = 0;

        for (const auto& metrics : events.agentMetrics) {
            auto& agent = byAgent[{metrics.symbol, metrics.clientId}];
            agent.symbol = metrics.symbol;
            agent.clientId = metrics.clientId;
            agent.agentName = metrics.agentName;
            agent.agentType = metrics.agentType;
            agent.wakeCount = metrics.wakeCount;
            agent.submittedCount = metrics.submittedCount;
            agent.limitOrderCount = metrics.limitOrderCount;
            agent.marketOrderCount = metrics.marketOrderCount;
            agent.cancelCount = metrics.cancelCount;
            agent.modifyCount = metrics.modifyCount;
            agent.fillCount = metrics.fillCount;
            agent.filledQuantity = metrics.filledQuantity;
            agent.restingQuantity = metrics.restingQuantity;
            agent.liveOrderCount = metrics.liveOrderCount;
            agent.netPosition = metrics.netPosition;
            agent.realizedPnL = metrics.realizedPnL;
            agent.unrealizedPnL = metrics.unrealizedPnL;
            agent.totalPnL = metrics.totalPnL;
            agent.averageQueuePosition = metrics.averageQueuePosition;
            agent.averageQuantityAhead = metrics.averageQuantityAhead;
            agent.averageFillProbability = metrics.averageFillProbability;
            agent.averageTimeToFillMs = metrics.averageTimeToFillMs;

            ++summary.queueAnalytics.samples;
            if (metrics.liveOrderCount > 0) {
                ++summary.queueAnalytics.liveOrderSamples;
                queuePositionTotal += metrics.averageQueuePosition;
                quantityAheadTotal += metrics.averageQuantityAhead;
                fillProbabilityTotal += metrics.averageFillProbability;
                fillAgeTotal += metrics.averageTimeToFillMs;
                restingQuantityTotal += metrics.restingQuantity;
            }
        }

        if (summary.queueAnalytics.liveOrderSamples > 0) {
            const double n = static_cast<double>(summary.queueAnalytics.liveOrderSamples);
            summary.queueAnalytics.averageQueuePosition = queuePositionTotal / n;
            summary.queueAnalytics.averageQuantityAhead = quantityAheadTotal / n;
            summary.queueAnalytics.averageFillProbability = fillProbabilityTotal / n;
            summary.queueAnalytics.averageTimeToFillMs = fillAgeTotal / n;
            summary.queueAnalytics.averageRestingQuantity =
                restingQuantityTotal / summary.queueAnalytics.liveOrderSamples;
        }

        std::map<std::pair<std::string, uint64_t>, double> adverseTicksByAgent;
        const int64_t referenceMid = summary.lastMidPrice > 0 ? summary.lastMidPrice : summary.maxMidPrice;
        if (referenceMid > 0) {
            for (const auto& trade : events.trades) {
                adverseTicksByAgent[{trade.symbol, trade.buyClientId}] +=
                    static_cast<double>(trade.price - referenceMid) * static_cast<double>(trade.quantity);
                adverseTicksByAgent[{trade.symbol, trade.sellClientId}] +=
                    static_cast<double>(referenceMid - trade.price) * static_cast<double>(trade.quantity);
            }
        }

        for (auto& [key, agent] : byAgent) {
            agent.adverseSelectionTicks = adverseTicksByAgent[key];
            if (agent.filledQuantity > 0 && referenceMid > 0) {
                const double avgTicks = agent.adverseSelectionTicks / static_cast<double>(agent.filledQuantity);
                agent.adverseSelectionBps = avgTicks / static_cast<double>(referenceMid) * 10000.0;
            }
            summary.finalTotalPnL += agent.totalPnL;
            summary.agents.push_back(agent);
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
        out << "sequence,symbol,enabled,running,paused,clockMode,speed,volatility,simulationTimestamp,marketMakerCount,momentumCount,meanReversionCount,noiseTraderCount,realizedVolatilityBps,averageSpread,toxicityScore,regime,limitLambda,cancelLambda,marketableLambda,marketMakerLevels,marketMakerQuoteQuantity,marketMakerMinQuantity,marketMakerBaseSpreadTicks,marketMakerToxicitySensitivity,marketMakerWakeIntervalMs,marketMakerInventorySkewDivisor\n";
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
                << state.marketableLambda << ','
                << state.marketMakerLevels << ','
                << state.marketMakerQuoteQuantity << ','
                << state.marketMakerMinQuantity << ','
                << state.marketMakerBaseSpreadTicks << ','
                << state.marketMakerToxicitySensitivity << ','
                << state.marketMakerWakeIntervalMs << ','
                << state.marketMakerInventorySkewDivisor << '\n';
        }
        writeTextFile(path, out.str());
    }

    inline void writeAgentMetricsCsv(const std::string& path, const std::vector<AgentMetricsEvent>& metricsEvents) {
        std::ostringstream out;
        out << "sequence,symbol,clientId,agentName,agentType,timestamp,simulationTimestamp,wakeCount,intentCount,submittedCount,limitOrderCount,marketOrderCount,cancelCount,modifyCount,fillCount,filledQuantity,restingQuantity,liveOrderCount,netPosition,realizedPnL,unrealizedPnL,totalPnL,averageQueuePosition,averageQuantityAhead,averageFillProbability,averageTimeToFillMs\n";
        for (const auto& metrics : metricsEvents) {
            out << metrics.sequence << ','
                << csvEscape(metrics.symbol) << ','
                << metrics.clientId << ','
                << csvEscape(metrics.agentName) << ','
                << csvEscape(metrics.agentType) << ','
                << metrics.timestamp << ','
                << metrics.simulationTimestamp << ','
                << metrics.wakeCount << ','
                << metrics.intentCount << ','
                << metrics.submittedCount << ','
                << metrics.limitOrderCount << ','
                << metrics.marketOrderCount << ','
                << metrics.cancelCount << ','
                << metrics.modifyCount << ','
                << metrics.fillCount << ','
                << metrics.filledQuantity << ','
                << metrics.restingQuantity << ','
                << metrics.liveOrderCount << ','
                << metrics.netPosition << ','
                << metrics.realizedPnL << ','
                << metrics.unrealizedPnL << ','
                << metrics.totalPnL << ','
                << metrics.averageQueuePosition << ','
                << metrics.averageQuantityAhead << ','
                << metrics.averageFillProbability << ','
                << metrics.averageTimeToFillMs << '\n';
        }
        writeTextFile(path, out.str());
    }

    inline void writeAgentSummaryCsv(const std::string& path, const std::vector<AgentAttributionSummary>& agents) {
        std::ostringstream out;
        out << "symbol,clientId,agentName,agentType,wakeCount,submittedCount,limitOrderCount,marketOrderCount,cancelCount,modifyCount,fillCount,filledQuantity,restingQuantity,liveOrderCount,netPosition,realizedPnL,unrealizedPnL,totalPnL,averageQueuePosition,averageQuantityAhead,averageFillProbability,averageTimeToFillMs,adverseSelectionTicks,adverseSelectionBps\n";
        for (const auto& agent : agents) {
            out << csvEscape(agent.symbol) << ','
                << agent.clientId << ','
                << csvEscape(agent.agentName) << ','
                << csvEscape(agent.agentType) << ','
                << agent.wakeCount << ','
                << agent.submittedCount << ','
                << agent.limitOrderCount << ','
                << agent.marketOrderCount << ','
                << agent.cancelCount << ','
                << agent.modifyCount << ','
                << agent.fillCount << ','
                << agent.filledQuantity << ','
                << agent.restingQuantity << ','
                << agent.liveOrderCount << ','
                << agent.netPosition << ','
                << agent.realizedPnL << ','
                << agent.unrealizedPnL << ','
                << agent.totalPnL << ','
                << agent.averageQueuePosition << ','
                << agent.averageQuantityAhead << ','
                << agent.averageFillProbability << ','
                << agent.averageTimeToFillMs << ','
                << agent.adverseSelectionTicks << ','
                << agent.adverseSelectionBps << '\n';
        }
        writeTextFile(path, out.str());
    }

}  // namespace Mercury
