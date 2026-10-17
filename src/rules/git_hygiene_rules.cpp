#include "repodoctor/rules/builtin_rules.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
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

// RD-GIT-001: Missing or Ineffective .gitignore
class GitignoreRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-001"; }
    std::string title() const override { return "Missing or Ineffective .gitignore"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Verifies that a top-level .gitignore file exists and contains effective ignore rules to prevent committing build outputs and local environment artifacts.";
    }
    std::string remediationGuidance() const override {
        return "Create or update .gitignore at the repository root with patterns matching your build directories (e.g., build/, node_modules/, __pycache__/, *.o, *.exe).";
    }
    std::string falsePositiveConsiderations() const override {
        return "Pure text/markdown repositories with no build outputs may require fewer .gitignore entries.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-GIT-GITIGNORE-TEMPLATE"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto& gitMeta = context.gitMetadata();
        if (!gitMeta.hasGitignoreFile) {
            RuleEvaluationResult res{};
            res.ruleId = id();
            res.ruleTitle = title();
            res.category = category();
            res.defaultSeverity = defaultSeverity();
            res.status = CheckStatus::FAIL;
            res.statusReason = "Missing .gitignore at repository root.";

            Finding f = makeFinding("Repository is missing a top-level .gitignore file.");
            f.evidence.push_back(Evidence{
                ".gitignore", 0, 0, "(file missing)", "Expected .gitignore at repository root", false});
            res.findings.push_back(std::move(f));
            return res;
        }

        if (gitMeta.gitignorePatterns.size() < 2) {
            RuleEvaluationResult res{};
            res.ruleId = id();
            res.ruleTitle = title();
            res.category = category();
            res.defaultSeverity = defaultSeverity();
            res.status = CheckStatus::WARNING;
            res.statusReason = ".gitignore contains fewer than 2 active ignore rules.";

            Finding f = makeFinding(
                ".gitignore is empty or ineffective (" +
                    std::to_string(gitMeta.gitignorePatterns.size()) + " active pattern(s)).",
                CheckStatus::WARNING,
                Severity::Low);
            f.evidence.push_back(Evidence{
                ".gitignore",
                1,
                1,
                gitMeta.gitignorePatterns.empty() ? "(no active rules)"
                                                  : gitMeta.gitignorePatterns.front(),
                "Add standard build/IDE ignore patterns.",
                false});
            res.findings.push_back(std::move(f));
            return res;
        }

        return makePassResult(".gitignore present with " +
                              std::to_string(gitMeta.gitignorePatterns.size()) +
                              " active rule(s).");
    }
};

// RD-GIT-002: Accidentally Tracked Build Artifacts
class TrackedBuildArtifactsRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-002"; }
    std::string title() const override { return "Build Artifacts or Dependency Directories in Tree"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Detects build outputs, compiled object files, CMake cache directories, or unignored dependency folders (e.g., CMakeFiles/, node_modules/, __pycache__/, *.o, *.obj, *.pdb).";
    }
    std::string remediationGuidance() const override {
        return "Remove committed build artifacts with 'git rm --cached <path>' and add corresponding directories/extensions to .gitignore.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Vendored precompiled libraries in an explicit third_party/ or vendor/ directory may be intentional.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> foundArtifacts;
        const auto& patterns = context.gitMetadata().gitignorePatterns;

        for (const auto& file : context.files()) {
            const std::string& rel = file.relativePath;
            const std::string lowerRel = toLower(rel);
            const std::string ext =
                toLower(pathToUtf8(utf8ToPath(rel).extension()));

            const bool isIgnoredInGitignore =
                GitRepositoryInspector::isPathMatchedByGitignore(patterns, rel);
            if (isIgnoredInGitignore && !file.isTrackedInGit) {
                continue;
            }

            bool isBuildArtifact = false;
            std::string reason;

            if (lowerRel.find("cmakefiles/") != std::string::npos ||
                lowerRel.find("cmakecache.txt") != std::string::npos) {
                isBuildArtifact = true;
                reason = "Generated CMake build cache/directory";
            } else if (lowerRel.rfind("node_modules/", 0) == 0 ||
                       lowerRel.find("/node_modules/") != std::string::npos) {
                isBuildArtifact = true;
                reason = "Node.js node_modules directory in repository tree";
            } else if (lowerRel.rfind("__pycache__/", 0) == 0 ||
                       lowerRel.find("/__pycache__/") != std::string::npos ||
                       ext == ".pyc" || ext == ".pyo") {
                isBuildArtifact = true;
                reason = "Python bytecode cache artifact";
            } else if (ext == ".o" || ext == ".obj" || ext == ".pdb" || ext == ".ilk" ||
                       ext == ".idb") {
                isBuildArtifact = true;
                reason = "Compiled intermediate object/debug artifact (" + ext + ")";
            }

            if (isBuildArtifact) {
                foundArtifacts.push_back(Evidence{
                    rel,
                    0,
                    0,
                    rel,
                    reason + (file.isTrackedInGit ? " [tracked in Git index]" : ""),
                    false});
            }
        }

        if (foundArtifacts.empty()) {
            return makePassResult("No unignored or tracked build artifacts detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(foundArtifacts.size()) + " build artifact(s) in repository.";

        Finding f = makeFinding(
            "Detected " + std::to_string(foundArtifacts.size()) +
            " build or dependency artifact(s) in the repository tree.");
        f.evidence = std::move(foundArtifacts);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-GIT-003: Large Committed Files
class LargeFilesRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-003"; }
    std::string title() const override { return "Large Files in Repository"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Detects files exceeding the configured large-file threshold (default 5 MB), which bloat Git clone history.";
    }
    std::string remediationGuidance() const override {
        return "Move large media, datasets, or archives to Git LFS, release assets, or external artifact storage.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Repositories using Git LFS or intentionally bundling offline datasets may configure 'large_file_threshold_bytes' in .repodoctor.json.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const std::uint64_t threshold = context.config().largeFileThresholdBytes;
        std::vector<Evidence> largeFiles;

        for (const auto& file : context.files()) {
            if (file.sizeBytes >= threshold) {
                const double sizeMb =
                    static_cast<double>(file.sizeBytes) / (1024.0 * 1024.0);
                std::ostringstream oss;
                oss.precision(2);
                oss << std::fixed << sizeMb << " MB";
                largeFiles.push_back(Evidence{
                    file.relativePath,
                    0,
                    0,
                    file.relativePath + " (" + oss.str() + ")",
                    "Exceeds configured threshold of " +
                        std::to_string(threshold / (1024ULL * 1024ULL)) + " MB",
                    false});
            }
        }

        if (context.config().scanHistory) {
            for (const auto& hist : context.historicalAdditions()) {
                if (hist.addedSizeBytes >= threshold) {
                    const double sizeMb =
                        static_cast<double>(hist.addedSizeBytes) / (1024.0 * 1024.0);
                    std::ostringstream oss;
                    oss.precision(2);
                    oss << std::fixed << sizeMb << " MB";
                    const std::string shortHash = hist.commitHash.substr(0, 8);
                    largeFiles.push_back(Evidence{
                        hist.relativeFilePath,
                        0,
                        0,
                        hist.relativeFilePath + " (" + oss.str() + ")",
                        "Historical large file in commit " + shortHash + " by " +
                            hist.commitAuthor + ": \"" + hist.commitMessage + "\"",
                        false,
                        hist.commitHash,
                        hist.commitAuthor,
                        hist.commitMessage});
                }
            }
        }

        if (largeFiles.empty()) {
            return makePassResult("No files exceed the large-file threshold.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(largeFiles.size()) + " file(s) exceeding size threshold.";

        Finding f = makeFinding(
            "Detected " + std::to_string(largeFiles.size()) +
                " oversized file(s) in repository.",
            CheckStatus::WARNING);
        f.evidence = std::move(largeFiles);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-GIT-004: Suspicious Binary Executables or Archives
class SuspiciousBinariesRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-004"; }
    std::string title() const override { return "Suspicious Binary Executables or Libraries in Source Tree"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Detects executable binaries or shared libraries (.exe, .dll, .so, .dylib, .jar, .class) committed into the source tree.";
    }
    std::string remediationGuidance() const override {
        return "Publish compiled binaries through GitHub Releases or package registries rather than committing executables directly into source control.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Gradle wrapper jars (gradle-wrapper.jar) or signed redistributable fixtures may be intentionally present.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        static const std::array<std::string_view, 7> kSuspiciousExts = {
            ".exe", ".dll", ".so", ".dylib", ".msi", ".deb", ".rpm"};
        std::vector<Evidence> suspicious;

        for (const auto& file : context.files()) {
            const std::string ext =
                toLower(pathToUtf8(utf8ToPath(file.relativePath).extension()));
            for (std::string_view badExt : kSuspiciousExts) {
                if (ext == badExt) {
                    suspicious.push_back(Evidence{
                        file.relativePath,
                        0,
                        0,
                        file.relativePath,
                        "Compiled binary executable/library (" + ext + ") in source tree",
                        false});
                    break;
                }
            }
        }

        if (suspicious.empty()) {
            return makePassResult("No suspicious compiled executables/libraries found in source tree.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(suspicious.size()) + " binary executable/library file(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(suspicious.size()) +
                " compiled executable or shared library file(s) in repository.",
            CheckStatus::WARNING);
        f.evidence = std::move(suspicious);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-GIT-005: Temporary, Backup, or OS Junk Files
class TemporaryFilesRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-005"; }
    std::string title() const override { return "Temporary, Backup, or OS Metadata Files"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Low; }
    std::string description() const override {
        return "Detects editor swap files, backup files, and OS metadata (.DS_Store, Thumbs.db, *.tmp, *.bak, *.swp, *~).";
    }
    std::string remediationGuidance() const override {
        return "Delete temporary/OS junk files and add '.DS_Store', 'Thumbs.db', '*.swp', '*.tmp', and '*.bak' to .gitignore.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Test fixtures specifically testing backup or temp file handling may intentionally use these extensions.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> tempFiles;

        for (const auto& file : context.files()) {
            const std::filesystem::path p = utf8ToPath(file.relativePath);
            const std::string filename = toLower(pathToUtf8(p.filename()));
            const std::string ext = toLower(pathToUtf8(p.extension()));

            if (filename == ".ds_store" || filename == "thumbs.db" || filename == "desktop.ini" ||
                ext == ".tmp" || ext == ".bak" || ext == ".swp" || ext == ".swo" ||
                ext == ".orig" || (!filename.empty() && filename.back() == '~')) {
                tempFiles.push_back(Evidence{
                    file.relativePath,
                    0,
                    0,
                    file.relativePath,
                    "Temporary, backup, or OS metadata artifact",
                    false});
            }
        }

        if (tempFiles.empty()) {
            return makePassResult("No temporary or OS junk files detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(tempFiles.size()) + " temporary or OS metadata file(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(tempFiles.size()) +
                " temporary, editor swap, or OS metadata file(s).",
            CheckStatus::WARNING,
            Severity::Low);
        f.evidence = std::move(tempFiles);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-GIT-006: Unresolved Merge Conflict Markers
class MergeConflictMarkersRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-006"; }
    std::string title() const override { return "Unresolved Git Merge Conflict Markers"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Scans text files for leftover Git merge conflict markers ('<<<<<<< ', '=======', '>>>>>>> ').";
    }
    std::string remediationGuidance() const override {
        return "Resolve the conflicting sections manually and remove all '<<<<<<<', '=======', and '>>>>>>>' markers before committing.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Markdown horizontal rules or AsciiDoc headers that use '=======' without matching '<<<<<<<' and '>>>>>>>' markers are ignored to prevent false positives.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> conflicts;

        for (const auto& file : context.files()) {
            if (file.isBinary) {
                continue;
            }
            const auto contentOpt = context.readTextFile(file.relativePath);
            if (!contentOpt.has_value()) {
                continue;
            }

            std::istringstream stream(*contentOpt);
            std::string line;
            std::size_t lineNum = 0;
            std::size_t openMarkerLine = 0;
            std::string openMarkerSnippet;
            bool seenSeparator = false;

            while (std::getline(stream, line)) {
                ++lineNum;
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (context.checkAndRecordInlineSuppression(line, id())) {
                    continue;
                }
                if (line.rfind("<<<<<<< ", 0) == 0) {
                    openMarkerLine = lineNum;
                    openMarkerSnippet = line;
                    seenSeparator = false;
                } else if (openMarkerLine > 0 && line == "=======") {
                    seenSeparator = true;
                } else if (openMarkerLine > 0 && seenSeparator &&
                           line.rfind(">>>>>>> ", 0) == 0) {
                    conflicts.push_back(Evidence{
                        file.relativePath,
                        openMarkerLine,
                        1,
                        openMarkerSnippet,
                        "Unresolved merge conflict block spanning lines " +
                            std::to_string(openMarkerLine) + "-" + std::to_string(lineNum),
                        false});
                    openMarkerLine = 0;
                    seenSeparator = false;
                }
            }
        }

        if (conflicts.empty()) {
            return makePassResult("No unresolved Git merge conflict markers detected.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(conflicts.size()) + " unresolved merge conflict block(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(conflicts.size()) +
            " unresolved Git merge conflict marker block(s).");
        f.evidence = std::move(conflicts);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-GIT-007: Broken or Missing Git Metadata
class GitMetadataRule final : public IRule {
public:
    std::string id() const override { return "RD-GIT-007"; }
    std::string title() const override { return "Missing or Corrupted Git Repository Metadata"; }
    RuleCategory category() const noexcept override { return RuleCategory::RepositoryHygiene; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Verifies that the directory contains valid Git metadata (.git/HEAD, objects, refs) without corruption.";
    }
    std::string remediationGuidance() const override {
        return "Initialize Git with 'git init' if this is an uninitialized source archive, or run 'git fsck' to diagnose corrupted repository metadata.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Exported source tarballs or zip archives intentionally omit the .git directory.";
    }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto& meta = context.gitMetadata();
        if (meta.isValidRepository && !meta.headIsCorrupted) {
            return makePassResult("Valid Git repository metadata verified.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = meta.headIsCorrupted ? CheckStatus::FAIL : CheckStatus::WARNING;
        res.statusReason = meta.validationError;

        Finding f = makeFinding(
            meta.headIsCorrupted
                ? ("Corrupted Git repository metadata: " + meta.validationError)
                : ("Directory is not a valid Git repository: " + meta.validationError),
            res.status,
            meta.headIsCorrupted ? Severity::High : Severity::Medium);
        f.evidence.push_back(Evidence{
            ".git/HEAD", 0, 0, meta.validationError, "Git metadata inspection", false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

}  // namespace

void registerGitHygieneRules(RuleRegistry& registry) {
    registry.registerRule(std::make_unique<GitignoreRule>());
    registry.registerRule(std::make_unique<TrackedBuildArtifactsRule>());
    registry.registerRule(std::make_unique<LargeFilesRule>());
    registry.registerRule(std::make_unique<SuspiciousBinariesRule>());
    registry.registerRule(std::make_unique<TemporaryFilesRule>());
    registry.registerRule(std::make_unique<MergeConflictMarkersRule>());
    registry.registerRule(std::make_unique<GitMetadataRule>());
}

}  // namespace repodoctor
