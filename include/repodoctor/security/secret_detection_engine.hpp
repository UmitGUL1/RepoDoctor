#pragma once

#include "repodoctor/core/types.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace repodoctor {

enum class SecretKind : std::uint8_t {
    CloudOrApiToken = 0,
    PrivateKeyMaterial = 1,
    EnvConfigSecret = 2,
    HardcodedCredential = 3
};

struct SecretMatch {
    SecretKind kind{SecretKind::CloudOrApiToken};
    std::string providerOrPatternName;
    std::string relativeFilePath;
    std::size_t lineNumber{0};
    std::size_t columnNumber{0};
    std::string redactedSnippet;
    std::string redactedTokenPreview;
    Severity severity{Severity::Critical};
    double confidenceScore{0.9};
    std::string commitHash;
    std::string commitAuthor;
    std::string contentFingerprint{}; // Non-reversible identity, excluded from reports
    std::string commitMessage;
};

class SecretDetectionEngine {
public:
    SecretDetectionEngine() = default;

    [[nodiscard]] std::vector<SecretMatch> scanFileContent(
        const std::string& relativeFilePath,
        std::string_view content,
        std::size_t* suppressedCountOut = nullptr,
        bool allowSuppressions = true) const;

    [[nodiscard]] static bool hasInlineSuppression(
        std::string_view line,
        std::string_view ruleId);

    [[nodiscard]] static std::string redactSecretValue(std::string_view rawSecret);

    [[nodiscard]] static std::string redactLineContainingSecret(
        std::string_view fullLine,
        std::string_view rawSecret);

    [[nodiscard]] static bool isLikelyPlaceholderOrExample(
        std::string_view candidateValue,
        std::string_view fullLine,
        std::string_view relativeFilePath);

    [[nodiscard]] static double computeShannonEntropy(std::string_view text);
};

}  // namespace repodoctor
