#pragma once

#include "BacktestReport.h"
#include "ServerApp.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Mercury {

    struct BacktestRunResult {
        BacktestSummary summary;
        BacktestEventLog events;
        nlohmann::json calibration;
    };

    struct BacktestSweepResult {
        std::vector<BacktestSummary> summaries;
    };

    std::string sanitizeRunName(const std::string& raw);
    std::string pathString(const std::filesystem::path& path);
    std::vector<std::string> parseSymbolList(std::string symbols);

    nlohmann::json readJsonFile(const std::filesystem::path& path);
    void applyMarketMakerOverrides(MarketMakerConfig& config, const nlohmann::json& object);
    void applyRunOverrides(ServerOptions& options, const nlohmann::json& run, bool forceInstant);
    void applyScenarioDocument(ServerOptions& options, const nlohmann::json& document, bool forceInstant);

    nlohmann::json buildReplayCalibrationReport(const std::string& replayFile,
                                                const BacktestEventLog& events);

    void writeBacktestArtifacts(const std::filesystem::path& outputDir,
                                const ServerOptions& options,
                                const BacktestSummary& summary,
                                const BacktestEventLog& events);

    BacktestRunResult runBacktestOnce(ServerOptions options,
                                      const std::string& runName,
                                      bool printSummary,
                                      const std::optional<std::filesystem::path>& outputDir);

    BacktestSweepResult runBacktestSweepResults(ServerOptions baseOptions, bool printSummary);
    int runHeadlessSimulation(const ServerOptions& options);
    int runBacktestSweep(ServerOptions baseOptions);

    nlohmann::json backtestRunResultToJson(const BacktestRunResult& result);
    nlohmann::json backtestSweepResultToJson(const BacktestSweepResult& result);

}  // namespace Mercury
