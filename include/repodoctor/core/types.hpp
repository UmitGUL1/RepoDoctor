#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace repodoctor {

enum class Severity : std::uint8_t {
    Info = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    Critical = 4
};

enum class CheckStatus : std::uint8_t {
    PASS = 0,
    WARNING = 1,
    FAIL = 2,
    NOT_APPLICABLE = 3,
    NOT_VERIFIED = 4
};

enum class RuleCategory : std::uint8_t {
    Documentation = 0,
    RepositoryHygiene = 1,
    SecurityHygiene = 2,
    BuildAndCI = 3,
    Testing = 4,
    DependencyManagement = 5
};

[[nodiscard]] std::string toString(Severity severity);
[[nodiscard]] std::optional<Severity> parseSeverity(std::string_view text);

[[nodiscard]] std::string toString(CheckStatus status);
[[nodiscard]] std::string toString(RuleCategory category);
[[nodiscard]] std::string toDisplayLabel(RuleCategory category);

struct Evidence {
    std::string relativeFilePath;
    std::size_t lineNumber{0};
    std::size_t columnNumber{0};
    std::string snippet;          // Always redacted if derived from secret detection
    std::string contextNote;
    bool isRedacted{false};
};

struct Finding {
    std::string ruleId;
    std::string title;
    RuleCategory category{RuleCategory::Documentation};
    Severity severity{Severity::Medium};
    CheckStatus status{CheckStatus::FAIL};
    std::string summary;
    std::string description;
    std::vector<Evidence> evidence;
    std::string remediationGuidance;
    std::string falsePositiveNotes;
    bool fixable{false};
    std::string fixId;
};

struct RuleEvaluationResult {
    std::string ruleId;
    std::string ruleTitle;
    RuleCategory category{RuleCategory::Documentation};
    Severity defaultSeverity{Severity::Medium};
    CheckStatus status{CheckStatus::PASS};
    std::string statusReason;
    std::vector<Finding> findings;
};

struct ScanStatistics {
    std::size_t totalFilesDiscovered{0};
    std::size_t textFilesAnalyzed{0};
    std::size_t binaryFilesDetected{0};
    std::size_t skippedByExclusion{0};
    std::size_t skippedBySizeLimit{0};
    std::size_t symlinksSkipped{0};
    std::uint64_t totalBytesRead{0};
    double durationMilliseconds{0.0};
    bool wasCancelled{false};
    bool fileLimitReached{false};
};

[[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] inline std::string pathToGenericUtf8(const std::filesystem::path& p) {
    const auto u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] inline std::filesystem::path utf8ToPath(std::string_view utf8Str) {
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8Str.data()), utf8Str.size()));
}

}  // namespace repodoctor

