#include "repodoctor/core/types.hpp"

#include <algorithm>
#include <cctype>

namespace repodoctor {

std::string toString(Severity severity) {
    switch (severity) {
        case Severity::Info:
            return "INFO";
        case Severity::Low:
            return "LOW";
        case Severity::Medium:
            return "MEDIUM";
        case Severity::High:
            return "HIGH";
        case Severity::Critical:
            return "CRITICAL";
    }
    return "MEDIUM";
}

std::optional<Severity> parseSeverity(std::string_view text) {
    std::string upper(text);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    if (upper == "INFO" || upper == "INFORMATIONAL") {
        return Severity::Info;
    }
    if (upper == "LOW") {
        return Severity::Low;
    }
    if (upper == "MEDIUM" || upper == "WARN" || upper == "WARNING") {
        return Severity::Medium;
    }
    if (upper == "HIGH" || upper == "ERROR") {
        return Severity::High;
    }
    if (upper == "CRITICAL" || upper == "FATAL") {
        return Severity::Critical;
    }
    return std::nullopt;
}

std::string toString(CheckStatus status) {
    switch (status) {
        case CheckStatus::PASS:
            return "PASS";
        case CheckStatus::WARNING:
            return "WARNING";
        case CheckStatus::FAIL:
            return "FAIL";
        case CheckStatus::NOT_APPLICABLE:
            return "NOT_APPLICABLE";
        case CheckStatus::NOT_VERIFIED:
            return "NOT_VERIFIED";
    }
    return "NOT_VERIFIED";
}

std::string toString(RuleCategory category) {
    switch (category) {
        case RuleCategory::Documentation:
            return "Documentation";
        case RuleCategory::RepositoryHygiene:
            return "RepositoryHygiene";
        case RuleCategory::SecurityHygiene:
            return "SecurityHygiene";
        case RuleCategory::BuildAndCI:
            return "BuildAndCI";
        case RuleCategory::Testing:
            return "Testing";
        case RuleCategory::DependencyManagement:
            return "DependencyManagement";
    }
    return "Documentation";
}

std::string toDisplayLabel(RuleCategory category) {
    switch (category) {
        case RuleCategory::Documentation:
            return "Documentation";
        case RuleCategory::RepositoryHygiene:
            return "Repository Hygiene";
        case RuleCategory::SecurityHygiene:
            return "Security Hygiene";
        case RuleCategory::BuildAndCI:
            return "Build and CI";
        case RuleCategory::Testing:
            return "Testing";
        case RuleCategory::DependencyManagement:
            return "Dependency Management";
    }
    return "Documentation";
}

}  // namespace repodoctor
