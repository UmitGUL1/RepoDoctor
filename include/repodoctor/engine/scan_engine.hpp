#pragma once

#include "repodoctor/config/configuration_manager.hpp"
#include "repodoctor/core/types.hpp"
#include "repodoctor/engine/scan_context.hpp"
#include "repodoctor/rules/rule_registry.hpp"
#include "repodoctor/scoring/risk_scorer.hpp"
#include "repodoctor/sources/irepository_source.hpp"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace repodoctor {

struct ScanResult {
    bool scanCompleted{false};
    std::string errorMessage;
    std::string repositoryName;
    std::string repositoryPath;
    std::string sourceDescription;
    std::string scanTimestampIso8601;
    ScanStatistics statistics;
    GitRepoMetadata gitMetadata;
    DependencyInspectionReport dependencyReport;
    std::vector<RuleEvaluationResult> ruleEvaluations;
    std::vector<Finding> findings;
    HealthScoreReport healthScore;
    std::vector<std::string> configurationWarnings;
};

class ScanEngine {
public:
    explicit ScanEngine(
        RuleRegistry registry = RuleRegistry::createDefaultRegistry(),
        RiskScorer scorer = RiskScorer{});

    [[nodiscard]] ScanResult scan(
        IRepositorySource& source,
        const std::optional<ScanConfiguration>& configOverride = std::nullopt,
        const std::atomic<bool>* cancelToken = nullptr,
        ScanProgressCallback progressCallback = nullptr) const;

    [[nodiscard]] const RuleRegistry& ruleRegistry() const noexcept { return registry_; }

private:
    RuleRegistry registry_;
    RiskScorer scorer_;
};

}  // namespace repodoctor
