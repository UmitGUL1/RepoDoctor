#include "repodoctor/config/configuration_manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace repodoctor {

namespace {

bool wildcardMatch(std::string_view pattern, std::string_view str) {
    // Support simple globbing including "**/", "*", and "?"
    if (pattern == "**" || pattern == "**/*") {
        return true;
    }
    if (pattern.size() >= 3 && pattern.substr(pattern.size() - 3) == "/**") {
        std::string_view prefix = pattern.substr(0, pattern.size() - 3);
        if (str == prefix ||
            (str.size() > prefix.size() && str.substr(0, prefix.size()) == prefix &&
             str[prefix.size()] == '/')) {
            return true;
        }
    }

    std::size_t pIdx = 0;
    std::size_t sIdx = 0;
    std::size_t starIdx = std::string_view::npos;
    std::size_t matchIdx = 0;

    while (sIdx < str.size()) {
        if (pIdx < pattern.size() && (pattern[pIdx] == '?' || pattern[pIdx] == str[sIdx])) {
            ++pIdx;
            ++sIdx;
        } else if (pIdx < pattern.size() && pattern[pIdx] == '*') {
            starIdx = pIdx;
            matchIdx = sIdx;
            ++pIdx;
        } else if (starIdx != std::string_view::npos) {
            pIdx = starIdx + 1;
            ++matchIdx;
            sIdx = matchIdx;
        } else {
            return false;
        }
    }

    while (pIdx < pattern.size() && pattern[pIdx] == '*') {
        ++pIdx;
    }

    return pIdx == pattern.size();
}

std::optional<RuleCategory> parseCategoryKey(std::string_view key) {
    if (key == "Documentation" || key == "documentation") {
        return RuleCategory::Documentation;
    }
    if (key == "RepositoryHygiene" || key == "repository_hygiene") {
        return RuleCategory::RepositoryHygiene;
    }
    if (key == "SecurityHygiene" || key == "security_hygiene") {
        return RuleCategory::SecurityHygiene;
    }
    if (key == "BuildAndCI" || key == "build_and_ci") {
        return RuleCategory::BuildAndCI;
    }
    if (key == "Testing" || key == "testing") {
        return RuleCategory::Testing;
    }
    if (key == "DependencyManagement" || key == "dependency_management") {
        return RuleCategory::DependencyManagement;
    }
    return std::nullopt;
}

}  // namespace

ScanConfiguration ConfigurationManager::loadFromRepository(
    const std::filesystem::path& repoRoot,
    std::vector<std::string>* warningsOut) const {
    const std::filesystem::path primaryPath = repoRoot / ".repodoctor.json";
    std::error_code ec;
    if (std::filesystem::exists(primaryPath, ec) && std::filesystem::is_regular_file(primaryPath, ec)) {
        return loadFromFile(primaryPath, warningsOut);
    }
    return ScanConfiguration{};
}

ScanConfiguration ConfigurationManager::loadFromFile(
    const std::filesystem::path& configFilePath,
    std::vector<std::string>* warningsOut) const {
    ScanConfiguration config{};
    std::ifstream in(configFilePath);
    if (!in.is_open()) {
        if (warningsOut != nullptr) {
            warningsOut->push_back("Could not open configuration file: " + pathToUtf8(configFilePath));
        }
        return config;
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const std::exception& ex) {
        if (warningsOut != nullptr) {
            warningsOut->push_back(
                "Invalid JSON in configuration file '" + pathToUtf8(configFilePath) + "': " + ex.what());
        }
        return config;
    }

    if (!doc.is_object()) {
        if (warningsOut != nullptr) {
            warningsOut->push_back("Configuration root must be a JSON object.");
        }
        return config;
    }

    if (doc.contains("max_file_size_bytes") && doc["max_file_size_bytes"].is_number_unsigned()) {
        config.maxFileSizeBytes = doc["max_file_size_bytes"].get<std::uint64_t>();
    }
    if (doc.contains("large_file_threshold_bytes") &&
        doc["large_file_threshold_bytes"].is_number_unsigned()) {
        config.largeFileThresholdBytes = doc["large_file_threshold_bytes"].get<std::uint64_t>();
    }
    if (doc.contains("max_repository_scan_files") &&
        doc["max_repository_scan_files"].is_number_unsigned()) {
        config.maxRepositoryScanFiles = doc["max_repository_scan_files"].get<std::uint64_t>();
    }
    if (doc.contains("max_clone_size_bytes") && doc["max_clone_size_bytes"].is_number_unsigned() &&
        doc["max_clone_size_bytes"].get<std::uint64_t>() > 0) {
        config.maxCloneSizeBytes = doc["max_clone_size_bytes"].get<std::uint64_t>();
    }
    if (doc.contains("clone_timeout_seconds") && doc["clone_timeout_seconds"].is_number_integer()) {
        const auto& timeout = doc["clone_timeout_seconds"];
        if (timeout > 0 && timeout <= 86400) config.cloneTimeoutSeconds = timeout.get<int>();
        else if (warningsOut) warningsOut->push_back("clone_timeout_seconds must be between 1 and 86400.");
    }
    if (doc.contains("fail_on_severity") && doc["fail_on_severity"].is_string()) {
        const auto parsed = parseSeverity(doc["fail_on_severity"].get<std::string>());
        if (parsed.has_value()) {
            config.failOnSeverity = *parsed;
        } else if (warningsOut != nullptr) {
            warningsOut->push_back(
                "Unrecognized fail_on_severity value: " + doc["fail_on_severity"].get<std::string>());
        }
    }
    if (doc.contains("enable_advisory_lookup") && doc["enable_advisory_lookup"].is_boolean()) {
        config.enableAdvisoryLookup = doc["enable_advisory_lookup"].get<bool>();
    }
    if (doc.contains("respect_gitignore") && doc["respect_gitignore"].is_boolean()) {
        config.respectGitignore = doc["respect_gitignore"].get<bool>();
    }

    if (doc.contains("exclude") && doc["exclude"].is_array()) {
        for (const auto& item : doc["exclude"]) {
            if (item.is_string()) {
                config.excludePatterns.push_back(item.get<std::string>());
            }
        }
    }

    if (doc.contains("disabled_rules") && doc["disabled_rules"].is_object()) {
        for (auto it = doc["disabled_rules"].begin(); it != doc["disabled_rules"].end(); ++it) {
            const std::string ruleId = it.key();
            std::string justification;
            if (it.value().is_string()) {
                justification = it.value().get<std::string>();
            } else if (it.value().is_object() && it.value().contains("justification") &&
                       it.value()["justification"].is_string()) {
                justification = it.value()["justification"].get<std::string>();
            }

            if (justification.empty() && warningsOut != nullptr) {
                warningsOut->push_back(
                    "Rule '" + ruleId +
                    "' is disabled in configuration without a documented justification.");
            }
            config.disabledRules[ruleId] = DisabledRuleConfig{ruleId, justification};
        }
    }

    if (doc.contains("category_weights") && doc["category_weights"].is_object()) {
        for (auto it = doc["category_weights"].begin(); it != doc["category_weights"].end(); ++it) {
            const auto cat = parseCategoryKey(it.key());
            if (cat.has_value() && it.value().is_number()) {
                const double w = it.value().get<double>();
                if (w >= 0.0) {
                    config.categoryWeights[*cat] = w;
                }
            }
        }
    }

    return config;
}

bool ConfigurationManager::saveToFile(
    const ScanConfiguration& config,
    const std::filesystem::path& configFilePath,
    std::string* errorOut) const {
    nlohmann::json doc = nlohmann::json::object();
    doc["max_file_size_bytes"] = config.maxFileSizeBytes;
    doc["large_file_threshold_bytes"] = config.largeFileThresholdBytes;
    doc["max_repository_scan_files"] = config.maxRepositoryScanFiles;
    doc["max_clone_size_bytes"] = config.maxCloneSizeBytes;
    doc["clone_timeout_seconds"] = config.cloneTimeoutSeconds;
    doc["fail_on_severity"] = toString(config.failOnSeverity);
    doc["enable_advisory_lookup"] = config.enableAdvisoryLookup;
    doc["respect_gitignore"] = config.respectGitignore;
    doc["exclude"] = config.excludePatterns;

    nlohmann::json disabledObj = nlohmann::json::object();
    for (const auto& [ruleId, info] : config.disabledRules) {
        disabledObj[ruleId] = {{"justification", info.justification}};
    }
    doc["disabled_rules"] = disabledObj;

    nlohmann::json weightsObj = nlohmann::json::object();
    for (const auto& [cat, weight] : config.categoryWeights) {
        weightsObj[toString(cat)] = weight;
    }
    doc["category_weights"] = weightsObj;

    std::ofstream out(configFilePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (errorOut != nullptr) {
            *errorOut = "Unable to open configuration file for writing: " + pathToUtf8(configFilePath);
        }
        return false;
    }
    out << doc.dump(2) << "\n";
    return out.good();
}

bool ConfigurationManager::isRuleEnabled(
    const ScanConfiguration& config,
    const std::string& ruleId) {
    return config.disabledRules.find(ruleId) == config.disabledRules.end();
}

bool ConfigurationManager::matchesExclusion(
    const ScanConfiguration& config,
    const std::string& relativePath) {
    for (const auto& pattern : config.excludePatterns) {
        if (wildcardMatch(pattern, relativePath)) {
            return true;
        }
    }
    return false;
}

}  // namespace repodoctor
