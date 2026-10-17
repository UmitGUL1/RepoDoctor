#pragma once

#include "repodoctor/core/types.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace repodoctor {

struct DisabledRuleConfig {
    std::string ruleId;
    std::string justification;
};

struct ScanConfiguration {
    std::uint64_t maxFileSizeBytes{2ULL * 1024ULL * 1024ULL};       // 2 MB max text scan per file
    std::uint64_t largeFileThresholdBytes{5ULL * 1024ULL * 1024ULL}; // 5 MB large file warning
    std::uint64_t maxRepositoryScanFiles{25000};
    std::uint64_t maxCloneSizeBytes{150ULL * 1024ULL * 1024ULL};     // 150 MB remote clone limit
    int cloneTimeoutSeconds{60};
    Severity failOnSeverity{Severity::High};
    bool enableAdvisoryLookup{false};
    bool respectGitignore{false};
    bool scanHistory{false};
    std::size_t maxHistoryCommits{50};
    bool stagedOnly{false};
    std::string sinceGitRef;
    std::string baselineFilePath;
    std::string writeBaselineFilePath;
    std::vector<std::string> excludePatterns{
        ".git/**",
        ".repodoctor-backup/**",
        "build/**",
        "dist/**"
    };
    std::map<std::string, DisabledRuleConfig> disabledRules;
    std::map<RuleCategory, double> categoryWeights{
        {RuleCategory::Documentation, 20.0},
        {RuleCategory::SecurityHygiene, 25.0},
        {RuleCategory::BuildAndCI, 15.0},
        {RuleCategory::Testing, 15.0},
        {RuleCategory::RepositoryHygiene, 15.0},
        {RuleCategory::DependencyManagement, 10.0}
    };
};

class ConfigurationManager {
public:
    ConfigurationManager() = default;

    [[nodiscard]] ScanConfiguration loadFromRepository(
        const std::filesystem::path& repoRoot,
        std::vector<std::string>* warningsOut = nullptr) const;

    [[nodiscard]] ScanConfiguration loadFromFile(
        const std::filesystem::path& configFilePath,
        std::vector<std::string>* warningsOut = nullptr) const;

    bool saveToFile(
        const ScanConfiguration& config,
        const std::filesystem::path& configFilePath,
        std::string* errorOut = nullptr) const;

    [[nodiscard]] static bool isRuleEnabled(
        const ScanConfiguration& config,
        const std::string& ruleId);

    [[nodiscard]] static bool matchesExclusion(
        const ScanConfiguration& config,
        const std::string& relativePath);
};

}  // namespace repodoctor
