#pragma once

#include "repodoctor/config/configuration_manager.hpp"
#include "repodoctor/core/types.hpp"
#include "repodoctor/inspectors/dependency_inspector.hpp"
#include "repodoctor/inspectors/git_repository_inspector.hpp"
#include "repodoctor/security/secret_detection_engine.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace repodoctor {

struct IndexedFile {
    std::string relativePath;
    std::filesystem::path absolutePath;
    std::uint64_t sizeBytes{0};
    bool isBinary{false};
    bool isSymlink{false};
    bool isTrackedInGit{false};
    bool isWorldWritable{false};
    bool isGroupOrWorldReadable{false};
    bool isExecutable{false};
    std::uint32_t posixMode{0};
};

using ScanProgressCallback = std::function<void(
    const std::string& phaseDescription,
    std::size_t completedSteps,
    std::size_t totalSteps)>;

class ScanContext {
public:
    ScanContext(
        std::filesystem::path repoRoot,
        ScanConfiguration config,
        const std::atomic<bool>* cancelToken = nullptr,
        ScanProgressCallback progressCallback = nullptr);

    void indexRepository();

    [[nodiscard]] const std::filesystem::path& repoRoot() const noexcept { return repoRoot_; }
    [[nodiscard]] const ScanConfiguration& config() const noexcept { return config_; }
    [[nodiscard]] const GitRepoMetadata& gitMetadata() const noexcept { return gitMetadata_; }
    [[nodiscard]] const DependencyInspectionReport& dependencyReport() const noexcept {
        return dependencyReport_;
    }
    [[nodiscard]] const std::vector<SecretMatch>& secretMatches() const noexcept {
        return secretMatches_;
    }
    [[nodiscard]] const std::vector<GitHistoricalCommitFileAddition>& historicalAdditions() const noexcept {
        return historicalAdditions_;
    }
    [[nodiscard]] const std::vector<IndexedFile>& files() const noexcept { return files_; }
    [[nodiscard]] const ScanStatistics& statistics() const noexcept { return stats_; }
    [[nodiscard]] bool isCancelled() const noexcept;
    [[nodiscard]] bool isIncrementalScan() const noexcept {
        return config_.stagedOnly || !config_.sinceGitRef.empty();
    }

    [[nodiscard]] bool checkAndRecordInlineSuppression(
        std::string_view line,
        std::string_view ruleId) const;

    [[nodiscard]] bool hasFile(const std::string& relativePath) const;
    [[nodiscard]] std::optional<IndexedFile> findFileCaseInsensitive(
        const std::string& relativePath) const;

    [[nodiscard]] std::optional<std::string> readTextFile(
        const std::string& relativePath,
        std::size_t maxBytes = 0) const;

    [[nodiscard]] static bool detectBinaryBuffer(std::string_view buffer);

private:
    std::filesystem::path repoRoot_;
    ScanConfiguration config_;
    const std::atomic<bool>* cancelToken_{nullptr};
    ScanProgressCallback progressCallback_;

    GitRepoMetadata gitMetadata_;
    DependencyInspectionReport dependencyReport_;
    std::vector<SecretMatch> secretMatches_;
    std::vector<GitHistoricalCommitFileAddition> historicalAdditions_;
    std::vector<IndexedFile> files_;
    std::map<std::string, std::size_t> fileIndexByPath_;
    mutable ScanStatistics stats_;
    std::map<std::string, std::string> stagedContents_;
};

}  // namespace repodoctor
