#pragma once

#include <cstdint>
#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace repodoctor {

struct GitRepoMetadata {
    bool hasGitDirectory{false};
    bool isValidRepository{false};
    bool isBareRepository{false};
    bool headIsDetached{false};
    bool headIsCorrupted{false};
    std::string currentBranch;
    std::string headCommitHash;
    std::string validationError;
    bool hasGitignoreFile{false};
    std::vector<std::string> gitignorePatterns;
    std::set<std::string> trackedFiles;
    std::vector<std::string> submodulePaths;
};

struct GitHistoricalCommitFileAddition {
    std::string commitHash;
    std::string commitAuthor;
    std::string commitMessage;
    std::string relativeFilePath;
    std::uint64_t addedSizeBytes{0};
    std::string addedContent;
};

class GitRepositoryInspector {
public:
    GitRepositoryInspector() = default;

    [[nodiscard]] GitRepoMetadata inspect(const std::filesystem::path& repoRoot) const;

    [[nodiscard]] std::vector<GitHistoricalCommitFileAddition> inspectCommitHistory(
        const std::filesystem::path& repoRoot,
        std::size_t maxCommits = 50,
        std::size_t* commitsScannedOut = nullptr,
        const std::atomic<bool>* cancel = nullptr,
        std::size_t maxFileBytes = 4 * 1024 * 1024) const;

    [[nodiscard]] std::set<std::string> getStagedFiles(
        const std::filesystem::path& repoRoot,
        const std::atomic<bool>* cancel = nullptr) const;

    [[nodiscard]] std::set<std::string> getChangedFilesSince(
        const std::filesystem::path& repoRoot,
        const std::string& gitRef,
        const std::atomic<bool>* cancel = nullptr) const;

    [[nodiscard]] std::string readStagedFile(const std::filesystem::path& root,
        const std::string& file, std::size_t maxBytes, const std::atomic<bool>* cancel = nullptr) const;
    [[nodiscard]] std::uint64_t stagedFileSize(const std::filesystem::path& root,
        const std::string& file, const std::atomic<bool>* cancel = nullptr) const;

    [[nodiscard]] static bool isPathMatchedByGitignore(
        const std::vector<std::string>& gitignorePatterns,
        const std::string& relativePath);
};

}  // namespace repodoctor
