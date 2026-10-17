#include "repodoctor/security/secret_detection_engine.hpp"
#include "repodoctor/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>

namespace repodoctor {

namespace {

std::string toLower(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool isDocumentationOrExamplePath(std::string_view relativePath) {
    const std::string lower = toLower(relativePath);
    return lower.find(".example") != std::string::npos ||
           lower.find(".sample") != std::string::npos ||
           lower.find(".template") != std::string::npos;
}

}  // namespace

std::string SecretDetectionEngine::redactSecretValue(std::string_view rawSecret) {
    if (rawSecret.empty()) {
        return "[REDACTED]";
    }
    if (rawSecret.find("PRIVATE KEY") != std::string_view::npos) {
        return "[REDACTED:PRIVATE_KEY_HEADER]";
    }
    if (rawSecret.size() <= 8) {
        return "[REDACTED:" + std::to_string(rawSecret.size()) + "_CHARS]";
    }
    const std::size_t prefixLen = std::min<std::size_t>(4, rawSecret.size() / 4);
    const std::size_t suffixLen = std::min<std::size_t>(2, rawSecret.size() / 6);
    const std::size_t maskedCount =
        rawSecret.size() > (prefixLen + suffixLen)
            ? std::min<std::size_t>(16, rawSecret.size() - prefixLen - suffixLen)
            : 8;

    std::string out;
    out.append(rawSecret.substr(0, prefixLen));
    out.append(maskedCount, '*');
    if (suffixLen > 0) {
        out.append(rawSecret.substr(rawSecret.size() - suffixLen));
    }
    return out;
}

std::string SecretDetectionEngine::redactLineContainingSecret(
    std::string_view fullLine,
    std::string_view rawSecret) {
    // A second credential on the same line must never leak through evidence.
    // File, line, column, provider and a masked token remain available separately.
    (void)fullLine;
    (void)rawSecret;
    return "[REDACTED:SENSITIVE_LINE]";
}

double SecretDetectionEngine::computeShannonEntropy(std::string_view text) {
    if (text.empty()) {
        return 0.0;
    }
    std::array<std::size_t, 256> counts{};
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        counts[c]++;
    }
    double entropy = 0.0;
    const double len = static_cast<double>(text.size());
    for (std::size_t count : counts) {
        if (count > 0) {
            const double p = static_cast<double>(count) / len;
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

bool SecretDetectionEngine::isLikelyPlaceholderOrExample(
    std::string_view candidateValue,
    std::string_view fullLine,
    std::string_view relativeFilePath) {
    if (isDocumentationOrExamplePath(relativeFilePath)) {
        return true;
    }

    const std::string lowerVal = toLower(candidateValue);
    (void)fullLine;

    static const std::array<std::string_view, 22> kPlaceholderMarkers = {
        "example",       "your_",         "your-",      "placeholder", "changeme",
        "change_me",     "replace_me",    "todo",       "xxxx",        "00000000",
        "12345678",      "abcdef",        "<insert",    "<your",       "${",
        "process.env",   "os.environ",    "std::getenv","getenv(",     "sample_",
        "fake_",         "test_only_placeholder"
    };

    for (std::string_view marker : kPlaceholderMarkers) {
        if (lowerVal.find(marker) != std::string::npos) {
            return true;
        }
    }

    // AWS canonical documentation example key
    if (candidateValue == "AKIAIOSFODNN7EXAMPLE") {
        return true;
    }

    // All identical characters
    if (!candidateValue.empty()) {
        const char first = candidateValue.front();
        const bool allSame = std::all_of(
            candidateValue.begin(), candidateValue.end(), [first](char c) { return c == first; });
        if (allSame) {
            return true;
        }
    }

    return false;
}

bool SecretDetectionEngine::hasInlineSuppression(
    std::string_view line,
    std::string_view ruleId) {
    if (line.empty()) {
        return false;
    }
    const std::string lowerLine = toLower(line);
    const std::string marker = "repodoctor-ignore:";
    const auto pos = lowerLine.find(marker);
    if (pos == std::string::npos) {
        return false;
    }

    // A marker inside a quoted string is data, not an opt-out instruction.
    const std::string_view prefix = line.substr(0, pos);
    char quote = 0;
    bool escaped = false, comment = false, onlyWhitespace = true;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const char c = prefix[i];
        if (escaped) { escaped = false; continue; }
        if (quote) {
            if (c == '\\') escaped = true;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { quote = c; onlyWhitespace = false; continue; }
        if (c == '#' || prefix.substr(i, 2) == "//" || prefix.substr(i, 2) == "/*" ||
            prefix.substr(i, 4) == "<!--" || prefix.substr(i, 2) == "--" ||
            (c == ';' && onlyWhitespace)) { comment = true; break; }
        if (std::isspace(static_cast<unsigned char>(c)) == 0) onlyWhitespace = false;
    }
    if (!comment) return false;

    std::string rest = lowerLine.substr(pos + marker.size());
    const auto parenPos = rest.find('(');
    if (parenPos != std::string::npos) {
        rest = rest.substr(0, parenPos);
    }
    const auto endCommentPos = rest.find("-->");
    if (endCommentPos != std::string::npos) {
        rest = rest.substr(0, endCommentPos);
    }
    const auto endBlockPos = rest.find("*/");
    if (endBlockPos != std::string::npos) {
        rest = rest.substr(0, endBlockPos);
    }

    const std::string targetRule = toLower(ruleId);
    std::size_t idx = 0;
    while (idx < rest.size()) {
        while (idx < rest.size() &&
               (std::isspace(static_cast<unsigned char>(rest[idx])) != 0 || rest[idx] == ',')) {
            ++idx;
        }
        std::size_t tokenEnd = idx;
        while (tokenEnd < rest.size() &&
               std::isspace(static_cast<unsigned char>(rest[tokenEnd])) == 0 &&
               rest[tokenEnd] != ',') {
            ++tokenEnd;
        }
        if (tokenEnd > idx) {
            const std::string token = rest.substr(idx, tokenEnd - idx);
            if (token == "all" || token == "*" || token == targetRule) {
                return true;
            }
        }
        idx = tokenEnd;
    }
    return false;
}

std::vector<SecretMatch> SecretDetectionEngine::scanFileContent(
    const std::string& relativeFilePath,
    std::string_view content,
    std::size_t* suppressedCountOut,
    bool allowSuppressions) const {
    std::vector<SecretMatch> matches;

    // Skip template example files (.env.example, config.sample.yaml, etc.)
    if (isDocumentationOrExamplePath(relativeFilePath)) {
        return matches;
    }

    static const std::regex kPrivateKeyRegex(
        R"(-----BEGIN (?:RSA |DSA |EC |OPENSSH |PGP )?PRIVATE KEY-----)");
    static const std::regex kAwsAccessKeyRegex(R"(\b((?:AKIA|ASIA)[0-9A-Z]{16})\b)");
    static const std::regex kGitHubTokenRegex(
        R"(\b((?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,255}|github_pat_[A-Za-z0-9_]{22,255})\b)");
    static const std::regex kSlackTokenRegex(R"(\b(xox[baprs]-[0-9A-Za-z-]{10,120})\b)");
    static const std::regex kStripeKeyRegex(R"(\b((?:sk|rk)_live_[0-9a-zA-Z]{20,99})\b)");
    static const std::regex kGoogleApiKeyRegex(R"(\b(AIza[0-9A-Za-z\-_]{35})\b)");
    static const std::regex kGenericSecretAssignRegex(
        R"###((?:api[_-]?key|secret[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret|password|passwd|db[_-]?password|jwt[_-]?secret)\s*[:=]\s*["']?([A-Za-z0-9/+_\-=!@#$%^&*]{12,128})["']?)###",
        std::regex_constants::icase);
    static const std::regex kConnectionUriRegex(
        R"(\b((?:postgres|postgresql|mysql|mongodb|mongodb\+srv|redis|amqp)://[^:\s/]+:([^@\s"']{6,128})@[^\s"']+))",
        std::regex_constants::icase);

    const std::string filename =
        pathToUtf8(utf8ToPath(relativeFilePath).filename());
    const bool isDotEnvFile =
        (filename == ".env" || filename.rfind(".env.", 0) == 0) &&
        !isDocumentationOrExamplePath(filename);

    std::size_t lineStart = 0;
    std::size_t lineNumber = 0;
    std::string prevLineStr;

    auto isSuppressedForRule = [&](const std::string& curLine, std::string_view ruleId) {
        if (!allowSuppressions) return false;
        if (hasInlineSuppression(curLine, ruleId) || hasInlineSuppression(prevLineStr, ruleId)) {
            if (suppressedCountOut != nullptr) {
                ++(*suppressedCountOut);
            }
            return true;
        }
        return false;
    };

    while (lineStart <= content.size()) {
        const std::size_t lineEnd = content.find('\n', lineStart);
        std::string_view lineView =
            (lineEnd == std::string_view::npos)
                ? content.substr(lineStart)
                : content.substr(lineStart, lineEnd - lineStart);
        if (!lineView.empty() && lineView.back() == '\r') {
            lineView.remove_suffix(1);
        }
        ++lineNumber;

        // Bound maximum inspected line length to prevent regex backtracking on minified bundles
        if (lineView.size() <= 1024 && !lineView.empty()) {
            const std::string lineStr(lineView);
            std::smatch m;

            // 1. Private key header
            if (std::regex_search(lineStr, m, kPrivateKeyRegex)) {
                if (!isSuppressedForRule(lineStr, "RD-SEC-002")) {
                    SecretMatch match{};
                    match.kind = SecretKind::PrivateKeyMaterial;
                    match.providerOrPatternName = "PEM/OpenSSH Private Key";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(0)) + 1;
                    match.redactedTokenPreview = "[REDACTED:PRIVATE_KEY]";
                    match.redactedSnippet = "[REDACTED:PRIVATE_KEY_BLOCK_HEADER]";
                    match.severity = Severity::Critical;
                    match.confidenceScore = 0.99;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 2. AWS Access Key ID
            if (std::regex_search(lineStr, m, kAwsAccessKeyRegex)) {
                const std::string token = m[1].str();
                if (!isLikelyPlaceholderOrExample(token, lineStr, relativeFilePath) &&
                    !isSuppressedForRule(lineStr, "RD-SEC-001")) {
                    SecretMatch match{};
                    match.kind = SecretKind::CloudOrApiToken;
                    match.providerOrPatternName = "AWS Access Key ID";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(1)) + 1;
                    match.redactedTokenPreview = redactSecretValue(token);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, token);
                    match.severity = Severity::Critical;
                    match.confidenceScore = 0.95;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 3. GitHub Personal Access / OAuth Token
            if (std::regex_search(lineStr, m, kGitHubTokenRegex)) {
                const std::string token = m[1].str();
                if (!isLikelyPlaceholderOrExample(token, lineStr, relativeFilePath) &&
                    !isSuppressedForRule(lineStr, "RD-SEC-001")) {
                    SecretMatch match{};
                    match.kind = SecretKind::CloudOrApiToken;
                    match.providerOrPatternName = "GitHub Token";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(1)) + 1;
                    match.redactedTokenPreview = redactSecretValue(token);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, token);
                    match.severity = Severity::Critical;
                    match.confidenceScore = 0.98;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 4. Slack Token
            if (std::regex_search(lineStr, m, kSlackTokenRegex)) {
                const std::string token = m[1].str();
                if (!isLikelyPlaceholderOrExample(token, lineStr, relativeFilePath) &&
                    !isSuppressedForRule(lineStr, "RD-SEC-001")) {
                    SecretMatch match{};
                    match.kind = SecretKind::CloudOrApiToken;
                    match.providerOrPatternName = "Slack API Token";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(1)) + 1;
                    match.redactedTokenPreview = redactSecretValue(token);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, token);
                    match.severity = Severity::Critical;
                    match.confidenceScore = 0.96;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 5. Stripe Live Secret Key
            if (std::regex_search(lineStr, m, kStripeKeyRegex)) {
                const std::string token = m[1].str();
                if (!isLikelyPlaceholderOrExample(token, lineStr, relativeFilePath) &&
                    !isSuppressedForRule(lineStr, "RD-SEC-001")) {
                    SecretMatch match{};
                    match.kind = SecretKind::CloudOrApiToken;
                    match.providerOrPatternName = "Stripe Live Secret Key";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(1)) + 1;
                    match.redactedTokenPreview = redactSecretValue(token);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, token);
                    match.severity = Severity::Critical;
                    match.confidenceScore = 0.98;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 6. Google API Key
            if (std::regex_search(lineStr, m, kGoogleApiKeyRegex)) {
                const std::string token = m[1].str();
                if (!isLikelyPlaceholderOrExample(token, lineStr, relativeFilePath) &&
                    !isSuppressedForRule(lineStr, "RD-SEC-001")) {
                    SecretMatch match{};
                    match.kind = SecretKind::CloudOrApiToken;
                    match.providerOrPatternName = "Google API Key";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(1)) + 1;
                    match.redactedTokenPreview = redactSecretValue(token);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, token);
                    match.severity = Severity::High;
                    match.confidenceScore = 0.92;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 7. Database/Service Connection String with embedded password
            if (std::regex_search(lineStr, m, kConnectionUriRegex)) {
                const std::string secretPass = m[2].str();
                if (!isLikelyPlaceholderOrExample(secretPass, lineStr, relativeFilePath) &&
                    !isSuppressedForRule(lineStr, "RD-SEC-004")) {
                    SecretMatch match{};
                    match.kind = SecretKind::HardcodedCredential;
                    match.providerOrPatternName = "Database/Service Connection URI Password";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(2)) + 1;
                    match.redactedTokenPreview = redactSecretValue(secretPass);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, secretPass);
                    match.severity = Severity::High;
                    match.confidenceScore = 0.90;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            // 8. Generic secret/password assignment
            if (std::regex_search(lineStr, m, kGenericSecretAssignRegex)) {
                const std::string candidate = m[1].str();
                const double entropy = computeShannonEntropy(candidate);
                const std::string_view ruleIdForMatch =
                    isDotEnvFile ? "RD-SEC-003" : "RD-SEC-004";
                if (!isLikelyPlaceholderOrExample(candidate, lineStr, relativeFilePath) &&
                    entropy >= 3.1 && !isSuppressedForRule(lineStr, ruleIdForMatch)) {
                    SecretMatch match{};
                    match.kind =
                        isDotEnvFile ? SecretKind::EnvConfigSecret : SecretKind::HardcodedCredential;
                    match.providerOrPatternName =
                        isDotEnvFile ? "Environment File Secret" : "Hardcoded Secret/Password";
                    match.relativeFilePath = relativeFilePath;
                    match.lineNumber = lineNumber;
                    match.columnNumber = static_cast<std::size_t>(m.position(1)) + 1;
                    match.redactedTokenPreview = redactSecretValue(candidate);
                    match.redactedSnippet = redactLineContainingSecret(lineStr, candidate);
                    match.severity = Severity::High;
                    match.confidenceScore = 0.85;
                    match.contentFingerprint = detail::sha256(lineStr);
                    matches.push_back(std::move(match));
                }
            }
            prevLineStr = lineStr;
        } else {
            prevLineStr.clear();
        }

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    return matches;
}

}  // namespace repodoctor
