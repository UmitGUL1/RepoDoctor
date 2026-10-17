#include "repodoctor/engine/scan_engine.hpp"

#include <nlohmann/json.hpp>
#include "repodoctor/core/sha256.hpp"
#include "../fixes/exclusive_write.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <set>
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

std::string ScanEngine::computeEvidenceFingerprint(
    const std::string& ruleId,
    const Evidence& ev) {
    const std::string relPath = ev.relativeFilePath.empty() ? "." : ev.relativeFilePath;
    if (ev.isRedacted && ev.contentFingerprint.empty()) return {}; // Fail closed for content-less sensitive evidence.
    nlohmann::json identity = {ruleId, relPath, ev.lineNumber, ev.columnNumber, ev.commitHash,
        ev.contentFingerprint.empty() ? detail::sha256(ev.snippet) : ev.contentFingerprint};
    return "v2:" + detail::sha256(identity.dump());
}

bool ScanEngine::writeBaselineFile(
    const ScanResult& result,
    const std::filesystem::path& baselinePath,
    std::string* errorOut) {
    nlohmann::json root = nlohmann::json::object();
    root["tool"] = "RepoDoctor";
    root["schema_version"] = 2;
    root["version"] = "1.0.0";
    root["generated_at_utc"] = result.scanTimestampIso8601;

    nlohmann::json fpArray = nlohmann::json::array();
    std::set<std::string> seen;

    for (const auto& f : result.findings) {
        if (f.evidence.empty()) {
            Evidence synthetic{};
            synthetic.relativeFilePath = ".";
            synthetic.snippet = f.ruleId;
            const std::string fp = computeEvidenceFingerprint(f.ruleId, synthetic);
            if (!fp.empty() && seen.insert(fp).second) {
                fpArray.push_back({
                    {"fingerprint", fp},
                    {"rule_id", f.ruleId},
                    {"relative_path", "."},
                    {"snippet_hash", detail::sha256(synthetic.snippet)}
                });
            }
        } else {
            for (const auto& ev : f.evidence) {
                const std::string fp = computeEvidenceFingerprint(f.ruleId, ev);
                if (!fp.empty() && seen.insert(fp).second) {
                    fpArray.push_back({
                        {"fingerprint", fp},
                        {"rule_id", f.ruleId},
                        {"relative_path", ev.relativeFilePath.empty() ? "." : ev.relativeFilePath},
                        {"snippet_hash", detail::sha256(ev.snippet)}
                    });
                }
            }
        }
    }

    root["fingerprints"] = std::move(fpArray);

    std::error_code ec;
    const auto parent = baselinePath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
    }

    if (ec || !detail::writeNewFile(baselinePath, root.dump(2))) {
        if (errorOut) *errorOut = "Cannot create baseline; choose a new path (existing files are never overwritten).";
        return false;
    }
    return true;
}

bool ScanEngine::applyBaselineFile(
    ScanResult& result,
    const std::filesystem::path& baselinePath,
    const ScanConfiguration& config,
    std::string* errorOut) {
    std::ifstream in(baselinePath, std::ios::binary);
    if (!in.is_open()) {
        if (errorOut != nullptr) {
            *errorOut = "Failed to open baseline file for reading: " + pathToUtf8(baselinePath);
        }
        return false;
    }

    std::error_code sizeError;
    if (std::filesystem::file_size(baselinePath, sizeError) > 16 * 1024 * 1024 || sizeError) {
        if (errorOut) *errorOut = "Baseline file is unreadable or exceeds 16 MB.";
        return false;
    }
    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        if (errorOut != nullptr) {
            *errorOut = std::string("Invalid JSON in baseline file: ") + ex.what();
        }
        return false;
    }

    if (!root.is_object() || !root.contains("schema_version") || root["schema_version"] != 2 ||
        !root.contains("fingerprints") || !root["fingerprints"].is_array()) {
        if (errorOut) *errorOut = "Unsupported baseline schema; regenerate a version 2 baseline.";
        return false;
    }
    std::set<std::string> baselineSet;
    for (const auto& item : root["fingerprints"]) {
        if (!item.is_object() || !item.contains("fingerprint") || !item["fingerprint"].is_string()) {
            if (errorOut) *errorOut = "Invalid baseline fingerprint entry.";
            return false;
        }
        const auto fp = item["fingerprint"].get<std::string>();
        if (fp.size() != 67 || fp.rfind("v2:", 0) != 0 ||
            fp.find_first_not_of("0123456789abcdef", 3) != std::string::npos) {
            if (errorOut) *errorOut = "Invalid baseline fingerprint.";
            return false;
        }
        baselineSet.insert(fp);
    }

    std::size_t suppressedCount = 0;

    for (auto& eval : result.ruleEvaluations) {
        if (eval.findings.empty()) {
            continue;
        }
        std::vector<Finding> remainingFindings;
        for (auto& f : eval.findings) {
            if (f.evidence.empty()) {
                Evidence synthetic{};
                synthetic.relativeFilePath = ".";
                synthetic.snippet = f.ruleId;
                const std::string fp = computeEvidenceFingerprint(f.ruleId, synthetic);
                if (baselineSet.count(fp) > 0) {
                    ++suppressedCount;
                    continue;
                }
                remainingFindings.push_back(std::move(f));
            } else {
                std::vector<Evidence> remainingEv;
                for (auto& ev : f.evidence) {
                    const std::string fp = computeEvidenceFingerprint(f.ruleId, ev);
                    if (baselineSet.count(fp) > 0) {
                        ++suppressedCount;
                    } else {
                        remainingEv.push_back(std::move(ev));
                    }
                }
                if (!remainingEv.empty()) {
                    f.evidence = std::move(remainingEv);
                    remainingFindings.push_back(std::move(f));
                }
            }
        }
        eval.findings = std::move(remainingFindings);
        if (eval.findings.empty() &&
            (eval.status == CheckStatus::FAIL || eval.status == CheckStatus::WARNING)) {
            eval.status = CheckStatus::NOT_VERIFIED;
            eval.statusReason = "Existing findings accepted by baseline; underlying issues remain.";
        }
    }

    result.findings.clear();
    for (const auto& eval : result.ruleEvaluations) {
        for (const auto& f : eval.findings) {
            result.findings.push_back(f);
        }
    }

    result.statistics.suppressedByBaseline += suppressedCount;
    result.healthScore = RiskScorer{}.computeScore(result.ruleEvaluations, config);
    return true;
}

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
    if ((effectiveConfig.stagedOnly && !effectiveConfig.sinceGitRef.empty()) ||
        (effectiveConfig.scanHistory && context.isIncrementalScan())) {
        result.errorMessage = "Use --staged, --since or --scan-history separately.";
        return result;
    }
    if (!effectiveConfig.baselineFilePath.empty() && !effectiveConfig.writeBaselineFilePath.empty()) {
        result.errorMessage = "Baseline reading and writing must be separate operations.";
        return result;
    }
    try { context.indexRepository(); }
    catch (const std::exception& ex) {
        result.errorMessage = ex.what(); result.statistics = context.statistics();
        result.statistics.wasCancelled = context.isCancelled(); return result;
    }

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

        if (context.isIncrementalScan() && (rulePtr->isRepositoryWideRule() ||
            rulePtr->category() == RuleCategory::DependencyManagement)) {
            RuleEvaluationResult skipped{};
            skipped.ruleId = rulePtr->id();
            skipped.ruleTitle = rulePtr->title();
            skipped.category = rulePtr->category();
            skipped.defaultSeverity = rulePtr->defaultSeverity();
            skipped.status = CheckStatus::NOT_APPLICABLE;
            skipped.statusReason =
                "Skipped repository-wide check during incremental (--staged / --since) scan.";
            result.ruleEvaluations.push_back(std::move(skipped));
            continue;
        }

        RuleEvaluationResult eval = rulePtr->evaluate(context);
        // Other rules can quote the same sensitive source line (for example a CI action).
        // Inline exceptions affect detection policy, never whether reports expose a secret.
        SecretDetectionEngine evidenceScanner;
        for (auto& finding : eval.findings) {
            for (auto& evidence : finding.evidence) {
                if (!evidenceScanner.scanFileContent("evidence.txt", evidence.snippet, nullptr, false).empty()) {
                    evidence.contentFingerprint = detail::sha256(evidence.snippet);
                    evidence.snippet = "[REDACTED:SENSITIVE_LINE]";
                    evidence.isRedacted = true;
                }
                if (!evidenceScanner.scanFileContent("evidence.txt", evidence.contextNote, nullptr, false).empty())
                    evidence.contextNote = "[REDACTED:SENSITIVE_CONTEXT]";
            }
        }
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

    if (!effectiveConfig.writeBaselineFilePath.empty()) {
        std::string baselineWriteErr;
        if (!writeBaselineFile(
                result, utf8ToPath(effectiveConfig.writeBaselineFilePath), &baselineWriteErr)) {
            result.scanCompleted = false; result.errorMessage = baselineWriteErr; return result;
        }
    }

    if (!effectiveConfig.baselineFilePath.empty()) {
        std::string baselineApplyErr;
        if (!applyBaselineFile(
                result, utf8ToPath(effectiveConfig.baselineFilePath), effectiveConfig, &baselineApplyErr)) {
            result.scanCompleted = false; result.errorMessage = baselineApplyErr; return result;
        }
    }

    if (progressCallback) {
        progressCallback("Scan complete", 4, 4);
    }

    return result;
}

}  // namespace repodoctor
