#include "repodoctor/rules/builtin_rules.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace repodoctor {

namespace {

constexpr const char* kGitHistoryPersistenceWarning =
    "IMPORTANT: Deleting a secret from the working tree or a new commit does NOT remove it from Git history. "
    "Anyone who clones the repository can still recover historical commits. Immediately revoke and rotate the exposed credential, "
    "move secrets to environment variables or a secret manager, and purge Git history using 'git filter-repo' or BFG Repo-Cleaner.";

std::string toLower(std::string_view sv) {
    std::string out(sv);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

// RD-SEC-001: Potential Hardcoded API Token or Cloud Credential
class HardcodedApiTokenRule final : public IRule {
public:
    std::string id() const override { return "RD-SEC-001"; }
    std::string title() const override { return "Potential Hardcoded Cloud or API Token"; }
    RuleCategory category() const noexcept override { return RuleCategory::SecurityHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Critical; }
    std::string description() const override {
        return "Detects high-confidence API tokens and cloud provider credentials (AWS Access Keys, GitHub PATs, Slack tokens, Stripe live keys, Google API keys) with automatic secret redaction.";
    }
    std::string remediationGuidance() const override {
        return kGitHistoryPersistenceWarning;
    }
    std::string falsePositiveConsiderations() const override {
        return "Synthetic tokens in unit tests or documentation using non-standard prefixes may trigger alerts. Use clear 'EXAMPLE' or 'placeholder' markers in test data.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> hits;
        for (const auto& match : context.secretMatches()) {
            if (match.kind == SecretKind::CloudOrApiToken) {
                std::string note =
                    match.providerOrPatternName + " (" + match.redactedTokenPreview + ")";
                if (!match.commitHash.empty()) {
                    note += " [Commit " + match.commitHash.substr(0, 8) + " by " +
                            match.commitAuthor + ": \"" + match.commitMessage + "\"]";
                }
                hits.push_back(Evidence{
                    match.relativeFilePath,
                    match.lineNumber,
                    match.columnNumber,
                    match.redactedSnippet,
                    std::move(note),
                    true,
                    match.commitHash,
                    match.commitAuthor,
                    match.commitMessage,
                    match.contentFingerprint});
            }
        }

        if (hits.empty()) {
            return makePassResult("No hardcoded API or cloud provider tokens detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(hits.size()) + " potential hardcoded token(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(hits.size()) +
            " potential hardcoded cloud/API credential(s) (redacted).");
        f.evidence = std::move(hits);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-SEC-002: Private Key Material in Repository
class PrivateKeyMaterialRule final : public IRule {
public:
    std::string id() const override { return "RD-SEC-002"; }
    std::string title() const override { return "Private Key Material in Repository"; }
    RuleCategory category() const noexcept override { return RuleCategory::SecurityHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Critical; }
    std::string description() const override {
        return "Detects PEM, OpenSSH, RSA, EC, DSA, or PGP private key blocks or private key files (.pem, .key, id_rsa, id_ed25519) in the repository.";
    }
    std::string remediationGuidance() const override {
        return kGitHistoryPersistenceWarning;
    }
    std::string falsePositiveConsiderations() const override {
        return "Self-signed localhost development TLS keys used purely for offline integration tests should be generated dynamically at test runtime rather than committed.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> hits;
        for (const auto& match : context.secretMatches()) {
            if (match.kind == SecretKind::PrivateKeyMaterial) {
                std::string note = match.providerOrPatternName;
                if (!match.commitHash.empty()) {
                    note += " [Commit " + match.commitHash.substr(0, 8) + " by " +
                            match.commitAuthor + ": \"" + match.commitMessage + "\"]";
                }
                hits.push_back(Evidence{
                    match.relativeFilePath,
                    match.lineNumber,
                    match.columnNumber,
                    match.redactedSnippet,
                    std::move(note),
                    true,
                    match.commitHash,
                    match.commitAuthor,
                    match.commitMessage,
                    match.contentFingerprint});
            }
        }

        for (const auto& file : context.files()) {
            const std::filesystem::path p = utf8ToPath(file.relativePath);
            const std::string filename = toLower(pathToUtf8(p.filename()));
            const std::string ext = toLower(pathToUtf8(p.extension()));
            if (filename == "id_rsa" || filename == "id_ed25519" || filename == "id_ecdsa" ||
                filename == "id_dsa" || ext == ".p12" || ext == ".pfx" || ext == ".keystore") {
                hits.push_back(Evidence{
                    file.relativePath,
                    0,
                    0,
                    "[REDACTED:PRIVATE_KEY_FILE]",
                    "Sensitive private key or keystore file name (" + filename + ")",
                    true});
            }
        }

        if (hits.empty()) {
            return makePassResult("No private key material or keystore files detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(hits.size()) + " private key material instance(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(hits.size()) +
            " private key block(s) or keystore file(s) in repository.");
        f.evidence = std::move(hits);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-SEC-003: Suspicious Environment (.env) Files
class SuspiciousEnvFileRule final : public IRule {
public:
    std::string id() const override { return "RD-SEC-003"; }
    std::string title() const override { return "Exposed Environment (.env) Configuration File"; }
    RuleCategory category() const noexcept override { return RuleCategory::SecurityHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Detects non-template .env files (.env, .env.local, .env.production) present or tracked in the repository.";
    }
    std::string remediationGuidance() const override {
        return "Remove live .env files from version control ('git rm --cached .env'), add '.env*' to .gitignore, and provide a sanitized '.env.example' template instead. " +
               std::string(kGitHistoryPersistenceWarning);
    }
    std::string falsePositiveConsiderations() const override {
        return "Template files named '.env.example', '.env.sample', or '.env.template' are automatically allowed and do not trigger this rule.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> envHits;

        for (const auto& file : context.files()) {
            const std::string filename =
                toLower(pathToUtf8(utf8ToPath(file.relativePath).filename()));
            if (filename == ".env" ||
                (filename.rfind(".env.", 0) == 0 && filename != ".env.example" &&
                 filename != ".env.sample" && filename != ".env.template")) {
                envHits.push_back(Evidence{
                    file.relativePath,
                    1,
                    1,
                    "[REDACTED:ENV_FILE]",
                    "Environment file present in repository (use .env.example for templates)",
                    true});
            }
        }

        for (const auto& match : context.secretMatches()) {
            if (match.kind == SecretKind::EnvConfigSecret) {
                std::string note = match.providerOrPatternName;
                if (!match.commitHash.empty()) {
                    note += " [Commit " + match.commitHash.substr(0, 8) + " by " +
                            match.commitAuthor + ": \"" + match.commitMessage + "\"]";
                }
                envHits.push_back(Evidence{
                    match.relativeFilePath,
                    match.lineNumber,
                    match.columnNumber,
                    match.redactedSnippet,
                    std::move(note),
                    true,
                    match.commitHash,
                    match.commitAuthor,
                    match.commitMessage,
                    match.contentFingerprint});
            }
        }

        if (envHits.empty()) {
            return makePassResult("No exposed .env files detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(envHits.size()) + " exposed .env file/secret entry(s).";

        Finding f = makeFinding(
            "Detected exposed environment configuration (.env) file or secret assignment.");
        f.evidence = std::move(envHits);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-SEC-004: Hardcoded Passwords or Connection Strings
class HardcodedPasswordRule final : public IRule {
public:
    std::string id() const override { return "RD-SEC-004"; }
    std::string title() const override { return "Hardcoded Password or Connection String Secret"; }
    RuleCategory category() const noexcept override { return RuleCategory::SecurityHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Detects hardcoded high-entropy passwords, client secrets, JWT secrets, or database connection URIs with embedded credentials.";
    }
    std::string remediationGuidance() const override {
        return kGitHistoryPersistenceWarning;
    }
    std::string falsePositiveConsiderations() const override {
        return "Low-entropy strings and known placeholder values ('changeme', 'password', '${DB_PASS}', 'process.env.*') are filtered out automatically.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> hits;
        for (const auto& match : context.secretMatches()) {
            if (match.kind == SecretKind::HardcodedCredential) {
                std::string note =
                    match.providerOrPatternName + " (" + match.redactedTokenPreview + ")";
                if (!match.commitHash.empty()) {
                    note += " [Commit " + match.commitHash.substr(0, 8) + " by " +
                            match.commitAuthor + ": \"" + match.commitMessage + "\"]";
                }
                hits.push_back(Evidence{
                    match.relativeFilePath,
                    match.lineNumber,
                    match.columnNumber,
                    match.redactedSnippet,
                    std::move(note),
                    true,
                    match.commitHash,
                    match.commitAuthor,
                    match.commitMessage,
                    match.contentFingerprint});
            }
        }

        if (hits.empty()) {
            return makePassResult("No hardcoded passwords or credential URIs detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(hits.size()) + " hardcoded credential assignment(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(hits.size()) +
            " potential hardcoded password(s) or connection string credential(s) (redacted).");
        f.evidence = std::move(hits);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-SEC-005: Unsafe File Permissions
class UnsafePermissionsRule final : public IRule {
public:
    std::string id() const override { return "RD-SEC-005"; }
    std::string title() const override { return "Unsafe File Permissions on Sensitive or Script Files"; }
    RuleCategory category() const noexcept override { return RuleCategory::SecurityHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Checks POSIX file permissions (where supported by the host filesystem) for world-writable files (0002) or overly permissive key/credential files.";
    }
    std::string remediationGuidance() const override {
        return "Restrict world-writable files with 'chmod o-w <file>' and restrict private key/config files to owner-only ('chmod 600 <file>').";
    }
    std::string falsePositiveConsiderations() const override {
        return "Filesystems mounted via Windows DrvFS (/mnt/c) or FAT/NTFS volumes may synthesize uniform POSIX permission bits; on such mounts this check reports NOT_VERIFIED rather than false positives.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
#ifdef _WIN32
        (void)context;
        return makeNotVerifiedResult(
            "Fine-grained POSIX permission bit verification is not applicable on native Windows NTFS paths.");
#else
        const std::string rootStr = pathToGenericUtf8(context.repoRoot());
        if (rootStr.rfind("/mnt/", 0) == 0) {
            return makeNotVerifiedResult(
                "Repository resides on a mounted Windows filesystem (" + rootStr +
                ") where POSIX mode bits are synthesized by DrvFS.");
        }

        std::vector<Evidence> unsafeFiles;
        for (const auto& file : context.files()) {
            const std::filesystem::path p = utf8ToPath(file.relativePath);
            const std::string ext = toLower(pathToUtf8(p.extension()));
            if (file.isWorldWritable) {
                unsafeFiles.push_back(Evidence{
                    file.relativePath,
                    0,
                    0,
                    file.relativePath,
                    "File is world-writable (POSIX mode " + std::to_string(file.posixMode) + ")",
                    false});
            } else if ((ext == ".pem" || ext == ".key") && file.isGroupOrWorldReadable) {
                unsafeFiles.push_back(Evidence{
                    file.relativePath,
                    0,
                    0,
                    file.relativePath,
                    "Key file is readable by group/others (recommend chmod 600)",
                    false});
            }
        }

        if (unsafeFiles.empty()) {
            return makePassResult("No unsafe POSIX file permissions detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(unsafeFiles.size()) + " file(s) with overly permissive modes.";

        Finding f = makeFinding(
            "Detected " + std::to_string(unsafeFiles.size()) +
                " file(s) with unsafe POSIX permissions.",
            CheckStatus::WARNING);
        f.evidence = std::move(unsafeFiles);
        res.findings.push_back(std::move(f));
        return res;
#endif
    }
};

}  // namespace

void registerSecurityRules(RuleRegistry& registry) {
    registry.registerRule(std::make_unique<HardcodedApiTokenRule>());
    registry.registerRule(std::make_unique<PrivateKeyMaterialRule>());
    registry.registerRule(std::make_unique<SuspiciousEnvFileRule>());
    registry.registerRule(std::make_unique<HardcodedPasswordRule>());
    registry.registerRule(std::make_unique<UnsafePermissionsRule>());
}

}  // namespace repodoctor
