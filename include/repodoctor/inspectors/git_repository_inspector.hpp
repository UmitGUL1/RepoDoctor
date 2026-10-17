#pragma once

#include <cstdint>
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

class GitRepositoryInspector {
public:
    GitRepositoryInspector() = default;

    [[nodiscard]] GitRepoMetadata inspect(const std::filesystem::path& repoRoot) const;

    [[nodiscard]] static bool isPathMatchedByGitignore(
        const std::vector<std::string>& gitignorePatterns,
        const std::string& relativePath);
};

}  // namespace repodoctor
