#include "repodoctor/engine/scan_engine.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace repodoctor {

namespace {

std::string currentUtcTimestampIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTm{};
#ifdef _WIN32
    gmtime_s(&utcTm, &nowTime);
#else
    gmtime_r(&nowTime, &utcTm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

}  // namespace

ScanEngine::ScanEngine(RuleRegistry registry, RiskScorer scorer)
    : registry_(std::move(registry)), scorer_(std::move(scorer)) {}

ScanResult ScanEngine::scan(
    IRepositorySource& source,
    const std::optional<ScanConfiguration>& configOverride,
    const std::atomic<bool>* cancelToken,
    ScanProgressCallback progressCallback) const {
    ScanResult result{};
    result.sourceDescription = source.sourceDescription();
    result.scanTimestampIso8601 = currentUtcTimestampIso8601();

    const auto startClock = std::chrono::steady_clock::now();

    ScanConfiguration initialConfig = configOverride.value_or(ScanConfiguration{});
    if (cancelToken && cancelToken->load()) {
        result.errorMessage = "Scan cancelled by user.";
        result.statistics.wasCancelled = true;
        return result;
    }
    source.setCancellationToken(cancelToken);
    PreparedRepository prep = source.prepare(initialConfig);
    if (!prep.success) {
        result.scanCompleted = false;
        result.errorMessage = prep.errorMessage;
        result.statistics.wasCancelled = cancelToken && cancelToken->load();
        return result;
    }

    result.repositoryName = prep.displayIdentifier;
    result.repositoryPath = pathToGenericUtf8(prep.localPath);

    ConfigurationManager configMgr;
    ScanConfiguration effectiveConfig =
        configOverride.has_value()
            ? *configOverride
            : configMgr.loadFromRepository(prep.localPath, &result.configurationWarnings);

    ScanContext context(prep.localPath, effectiveConfig, cancelToken, progressCallback);
    context.indexRepository();

    if (context.isCancelled()) {
        result.scanCompleted = false;
        result.errorMessage = "Scan cancelled by user.";
        result.statistics = context.statistics();
        result.statistics.wasCancelled = true;
        return result;
    }

    if (context.statistics().fileLimitReached) {
        result.scanCompleted = false;
        result.errorMessage = "Repository file limit reached. Increase max_repository_scan_files or add exclusions.";
        result.statistics = context.statistics();
        return result;
    }

    result.gitMetadata = context.gitMetadata();
    result.dependencyReport = context.dependencyReport();

    if (progressCallback) {
        progressCallback("Evaluating repository health rules", 3, 4);
    }

    for (const auto& rulePtr : registry_.rules()) {
        if (context.isCancelled()) {
            result.scanCompleted = false;
            result.errorMessage = "Scan cancelled by user.";
            result.statistics = context.statistics();
            result.statistics.wasCancelled = true;
            return result;
        }

        if (!ConfigurationManager::isRuleEnabled(effectiveConfig, rulePtr->id())) {
            RuleEvaluationResult skipped{};
            skipped.ruleId = rulePtr->id();
            skipped.ruleTitle = rulePtr->title();
            skipped.category = rulePtr->category();
            skipped.defaultSeverity = rulePtr->defaultSeverity();
            skipped.status = CheckStatus::NOT_APPLICABLE;
            const auto it = effectiveConfig.disabledRules.find(rulePtr->id());
            skipped.statusReason =
                "Disabled by configuration" +
                ((it != effectiveConfig.disabledRules.end() && !it->second.justification.empty())
                     ? (": " + it->second.justification)
                     : ".");
            result.ruleEvaluations.push_back(std::move(skipped));
            continue;
        }

        RuleEvaluationResult eval = rulePtr->evaluate(context);
        for (const auto& f : eval.findings) {
            result.findings.push_back(f);
        }
        result.ruleEvaluations.push_back(std::move(eval));
    }

    result.healthScore = scorer_.computeScore(result.ruleEvaluations, effectiveConfig);

    const auto endClock = std::chrono::steady_clock::now();
    result.statistics = context.statistics();
    result.statistics.durationMilliseconds =
        std::chrono::duration<double, std::milli>(endClock - startClock).count();
    result.scanCompleted = true;

    if (progressCallback) {
        progressCallback("Scan complete", 4, 4);
    }

    return result;
}

}  // namespace repodoctor
