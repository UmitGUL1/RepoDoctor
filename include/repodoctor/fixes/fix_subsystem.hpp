#pragma once

#include "repodoctor/engine/scan_engine.hpp"

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace repodoctor {

enum class FixOperationType : std::uint8_t {
    CreateNewFile = 0,
    AppendOrModifyFile = 1
};

struct PlannedFix {
    std::string fixId;
    std::string ruleId;
    std::string title;
    std::string rationale;
    std::string targetRelativePath;
    FixOperationType operation{FixOperationType::CreateNewFile};
    std::string originalContent;
    std::string proposedContent;
    std::string unifiedDiff;
    bool hasConflict{false};
    std::string conflictReason;
    bool selectedByDefault{true};
};

struct FixPlan {
    std::filesystem::path repositoryRoot;
    std::vector<PlannedFix> fixes;
};

struct FixExecutionItemResult {
    std::string fixId;
    std::string targetRelativePath;
    bool applied{false};
    bool skipped{false};
    std::string backupRelativePath;
    std::string message;
};

struct FixExecutionReport {
    bool dryRun{true};
    bool allSucceeded{true};
    std::size_t appliedCount{0};
    std::size_t skippedCount{0};
    std::size_t failedCount{0};
    std::vector<FixExecutionItemResult> results;
};

class IFixProvider {
public:
    virtual ~IFixProvider() = default;

    [[nodiscard]] virtual std::string fixId() const = 0;
    [[nodiscard]] virtual std::string handledRuleId() const = 0;
    [[nodiscard]] virtual std::optional<PlannedFix> planFix(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const = 0;
};

class FixPlanner {
public:
    FixPlanner();

    void registerProvider(std::unique_ptr<IFixProvider> provider);

    [[nodiscard]] FixPlan createPlan(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const;

    [[nodiscard]] static std::string generateUnifiedDiff(
        const std::string& relativePath,
        const std::string& oldContent,
        const std::string& newContent,
        bool isNewFile);

private:
    std::vector<std::unique_ptr<IFixProvider>> providers_;
};

class FixExecutor {
public:
    FixExecutor() = default;

    // Empty selection means no changes. Callers must explicitly select every desired fix.
    [[nodiscard]] FixExecutionReport execute(
        const FixPlan& plan,
        const std::set<std::string>& selectedFixIds,
        bool dryRun = true,
        bool createBackups = true) const;

    [[nodiscard]] static bool isSafeRelativeTargetPath(
        const std::filesystem::path& repoRoot,
        const std::string& targetRelativePath,
        std::filesystem::path* resolvedOut = nullptr,
        std::string* errorReasonOut = nullptr);
};

}  // namespace repodoctor
