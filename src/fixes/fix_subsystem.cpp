#include "repodoctor/fixes/fix_subsystem.hpp"

#include "exclusive_write.hpp"
#include <algorithm>
#include <random>
#include <cctype>
#include <chrono>
#include <fstream>
#include <sstream>

namespace repodoctor {

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string readExistingFileOrEmpty(const std::filesystem::path& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool findingPresentForRule(const ScanResult& scanResult, std::string_view ruleId) {
    for (const auto& f : scanResult.findings) {
        if (f.ruleId == ruleId) {
            return true;
        }
    }
    return false;
}

// 1. .gitignore Fix Provider (RD-GIT-001)
class GitignoreFixProvider final : public IFixProvider {
public:
    std::string fixId() const override { return "FIX-GIT-GITIGNORE-TEMPLATE"; }
    std::string handledRuleId() const override { return "RD-GIT-001"; }

    std::optional<PlannedFix> planFix(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const override {
        if (!findingPresentForRule(scanResult, handledRuleId())) {
            return std::nullopt;
        }

        const std::filesystem::path targetPath = repoRoot / ".gitignore";
        std::error_code ec;
        const bool exists = std::filesystem::exists(targetPath, ec);
        const std::string original = exists ? readExistingFileOrEmpty(targetPath) : "";

        const std::string templateBlock =
            "# Build & CMake artifacts\n"
            "build/\n"
            "build-*/\n"
            "out/\n"
            "CMakeFiles/\n"
            "CMakeCache.txt\n"
            "cmake_install.cmake\n"
            "compile_commands.json\n\n"
            "# Compiled binaries & objects\n"
            "*.o\n"
            "*.obj\n"
            "*.exe\n"
            "*.dll\n"
            "*.so\n"
            "*.dylib\n"
            "*.pdb\n\n"
            "# Node / Python / Package managers\n"
            "node_modules/\n"
            "__pycache__/\n"
            "*.pyc\n"
            ".venv/\n"
            "venv/\n\n"
            "# Environment & OS files\n"
            ".env\n"
            ".env.local\n"
            ".DS_Store\n"
            "Thumbs.db\n"
            "*.swp\n"
            "*.tmp\n"
            "*.bak\n";

        std::string proposed = original;
        if (!proposed.empty() && proposed.back() != '\n') {
            proposed.push_back('\n');
        }
        proposed.append(templateBlock);

        PlannedFix fix{};
        fix.fixId = fixId();
        fix.ruleId = handledRuleId();
        fix.title = exists ? "Append standard ignore patterns to .gitignore"
                           : "Create standard multi-language .gitignore";
        fix.rationale =
            "Prevents accidentally committing build directories, local .env secrets, and OS junk files.";
        fix.targetRelativePath = ".gitignore";
        fix.operation =
            exists ? FixOperationType::AppendOrModifyFile : FixOperationType::CreateNewFile;
        fix.originalContent = original;
        fix.proposedContent = proposed;
        fix.unifiedDiff = FixPlanner::generateUnifiedDiff(
            ".gitignore", original, proposed, !exists);
        return fix;
    }
};

// 2. README Template Fix Provider (RD-DOC-001)
class ReadmeTemplateFixProvider final : public IFixProvider {
public:
    std::string fixId() const override { return "FIX-DOC-README-TEMPLATE"; }
    std::string handledRuleId() const override { return "RD-DOC-001"; }

    std::optional<PlannedFix> planFix(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const override {
        if (!findingPresentForRule(scanResult, handledRuleId())) {
            return std::nullopt;
        }

        const std::filesystem::path targetPath = repoRoot / "README.md";
        std::error_code ec;
        const bool exists = std::filesystem::exists(targetPath, ec);
        const std::string original = exists ? readExistingFileOrEmpty(targetPath) : "";

        const std::string projectName =
            scanResult.repositoryName.empty() ? "Project" : scanResult.repositoryName;

        std::ostringstream oss;
        oss << "# " << projectName << "\n\n"
            << "Brief overview of **" << projectName << "** and the engineering problem it solves.\n\n"
            << "## Installation\n\n"
            << "```bash\n"
            << "cmake -B build -DCMAKE_BUILD_TYPE=Release\n"
            << "cmake --build build\n"
            << "```\n\n"
            << "## Usage\n\n"
            << "```bash\n"
            << "./build/" << projectName << " --help\n"
            << "```\n\n"
            << "## Contributing\n\n"
            << "Contributions are welcome! Please read `CONTRIBUTING.md` and ensure all tests pass before opening a pull request.\n";

        PlannedFix fix{};
        fix.fixId = fixId();
        fix.ruleId = handledRuleId();
        fix.title = "Create structured README.md template";
        fix.rationale =
            "Provides a structured starting point with Installation, Usage, and Contributing sections.";
        fix.targetRelativePath = "README.md";
        fix.operation = FixOperationType::CreateNewFile;
        fix.originalContent = original;
        fix.proposedContent = oss.str();
        if (exists && !original.empty()) {
            fix.hasConflict = true;
            fix.conflictReason =
                "README.md already exists with non-empty content; refusing to overwrite.";
        }
        fix.unifiedDiff = FixPlanner::generateUnifiedDiff(
            "README.md", original, fix.proposedContent, !exists);
        return fix;
    }
};

// 3. Non-destructive README Section Appender (RD-DOC-002 / RD-DOC-003 / RD-DOC-004)
class ReadmeSectionsFixProvider final : public IFixProvider {
public:
    std::string fixId() const override { return "FIX-DOC-README-SECTIONS"; }
    std::string handledRuleId() const override { return "RD-DOC-003"; }

    std::optional<PlannedFix> planFix(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const override {
        const bool needsInstall = findingPresentForRule(scanResult, "RD-DOC-003");
        const bool needsUsage = findingPresentForRule(scanResult, "RD-DOC-004");
        const bool isStub = findingPresentForRule(scanResult, "RD-DOC-002");

        if (!needsInstall && !needsUsage && !isStub) {
            return std::nullopt;
        }

        const std::filesystem::path targetPath = repoRoot / "README.md";
        std::error_code ec;
        if (!std::filesystem::exists(targetPath, ec)) {
            return std::nullopt;  // Handled by FIX-DOC-README-TEMPLATE
        }

        const std::string original = readExistingFileOrEmpty(targetPath);
        std::string proposed = original;
        if (!proposed.empty() && proposed.back() != '\n') {
            proposed.push_back('\n');
        }

        if (isStub && proposed.size() < 80) {
            proposed.append(
                "\nOverview of the repository architecture, build prerequisites, and primary workflow.\n");
        }
        if (needsInstall) {
            proposed.append(
                "\n## Installation\n\n"
                "Document prerequisite tools and build/installation commands here:\n\n"
                "```bash\n"
                "# Example build steps\n"
                "cmake -B build && cmake --build build\n"
                "```\n");
        }
        if (needsUsage) {
            proposed.append(
                "\n## Usage\n\n"
                "Provide common CLI or library usage examples here:\n\n"
                "```bash\n"
                "# Example invocation\n"
                "./build/app --help\n"
                "```\n");
        }

        PlannedFix fix{};
        fix.fixId = fixId();
        fix.ruleId = "RD-DOC-003";
        fix.title = "Append missing Installation / Usage sections to README.md";
        fix.rationale =
            "Non-destructively appends missing documentation section scaffolds while preserving all existing README content.";
        fix.targetRelativePath = "README.md";
        fix.operation = FixOperationType::AppendOrModifyFile;
        fix.originalContent = original;
        fix.proposedContent = proposed;
        fix.unifiedDiff = FixPlanner::generateUnifiedDiff(
            "README.md", original, proposed, false);
        return fix;
    }
};

// 4. CONTRIBUTING.md Template Fix Provider (RD-DOC-005)
class ContributingTemplateFixProvider final : public IFixProvider {
public:
    std::string fixId() const override { return "FIX-DOC-CONTRIBUTING-TEMPLATE"; }
    std::string handledRuleId() const override { return "RD-DOC-005"; }

    std::optional<PlannedFix> planFix(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const override {
        if (!findingPresentForRule(scanResult, handledRuleId())) {
            return std::nullopt;
        }

        const std::filesystem::path targetPath = repoRoot / "CONTRIBUTING.md";
        std::error_code ec;
        const bool exists = std::filesystem::exists(targetPath, ec);
        const std::string original = exists ? readExistingFileOrEmpty(targetPath) : "";

        const std::string content =
            "# Contributing Guidelines\n\n"
            "Thank you for taking the time to contribute!\n\n"
            "## Development Workflow\n\n"
            "1. Fork the repository and create a feature branch from `main`.\n"
            "2. Build the project and ensure existing tests pass.\n"
            "3. Add unit or integration tests covering your change.\n"
            "4. Verify formatting and static analysis checks.\n"
            "5. Open a pull request with a clear description of the problem and solution.\n\n"
            "## Reporting Issues\n\n"
            "Please include your operating system, compiler/runtime version, and minimal reproduction steps.\n";

        PlannedFix fix{};
        fix.fixId = fixId();
        fix.ruleId = handledRuleId();
        fix.title = "Create CONTRIBUTING.md template";
        fix.rationale =
            "Provides contributor onboarding instructions, workflow steps, and issue reporting guidelines.";
        fix.targetRelativePath = "CONTRIBUTING.md";
        fix.operation = FixOperationType::CreateNewFile;
        fix.originalContent = original;
        fix.proposedContent = content;
        if (exists && !original.empty()) {
            fix.hasConflict = true;
            fix.conflictReason = "CONTRIBUTING.md already exists.";
        }
        fix.unifiedDiff = FixPlanner::generateUnifiedDiff(
            "CONTRIBUTING.md", original, content, !exists);
        return fix;
    }
};

// 5. GitHub Actions CI Starter Workflow Fix Provider (RD-BLD-001)
class GitHubCiWorkflowFixProvider final : public IFixProvider {
public:
    std::string fixId() const override { return "FIX-BLD-GITHUB-CI"; }
    std::string handledRuleId() const override { return "RD-BLD-001"; }

    std::optional<PlannedFix> planFix(
        const ScanResult& scanResult,
        const std::filesystem::path& repoRoot) const override {
        if (!findingPresentForRule(scanResult, handledRuleId())) {
            return std::nullopt;
        }

        const std::string relTarget = ".github/workflows/ci.yml";
        const std::filesystem::path targetPath = repoRoot / relTarget;
        std::error_code ec;
        const bool exists = std::filesystem::exists(targetPath, ec);
        const std::string original = exists ? readExistingFileOrEmpty(targetPath) : "";

        const std::string workflowYaml =
            "name: CI\n\n"
            "on:\n"
            "  push:\n"
            "    branches: [ main, master ]\n"
            "  pull_request:\n"
            "    branches: [ main, master ]\n\n"
            "permissions:\n"
            "  contents: read\n\n"
            "jobs:\n"
            "  build-and-test:\n"
            "    runs-on: ubuntu-latest\n"
            "    steps:\n"
            "      - name: Checkout repository\n"
            "        uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11 # v4.1.1\n\n"
            "      - name: Configure CMake\n"
            "        run: cmake -B build -DCMAKE_BUILD_TYPE=Release\n\n"
            "      - name: Build\n"
            "        run: cmake --build build --parallel\n\n"
            "      - name: Run Tests\n"
            "        run: ctest --test-dir build --output-on-failure\n";

        PlannedFix fix{};
        fix.fixId = fixId();
        fix.ruleId = handledRuleId();
        fix.title = "Create least-privilege GitHub Actions CI workflow (.github/workflows/ci.yml)";
        fix.rationale =
            "Establishes automated build and test verification with SHA-pinned checkout and read-only token permissions.";
        fix.targetRelativePath = relTarget;
        fix.operation = FixOperationType::CreateNewFile;
        fix.originalContent = original;
        fix.proposedContent = workflowYaml;
        if (exists && !original.empty()) {
            fix.hasConflict = true;
            fix.conflictReason = ".github/workflows/ci.yml already exists.";
        }
        fix.unifiedDiff = FixPlanner::generateUnifiedDiff(
            relTarget, original, workflowYaml, !exists);
        return fix;
    }
};

}  // namespace

FixPlanner::FixPlanner() {
    registerProvider(std::make_unique<GitignoreFixProvider>());
    registerProvider(std::make_unique<ReadmeTemplateFixProvider>());
    registerProvider(std::make_unique<ReadmeSectionsFixProvider>());
    registerProvider(std::make_unique<ContributingTemplateFixProvider>());
    registerProvider(std::make_unique<GitHubCiWorkflowFixProvider>());
}

void FixPlanner::registerProvider(std::unique_ptr<IFixProvider> provider) {
    if (provider != nullptr) {
        providers_.push_back(std::move(provider));
    }
}

FixPlan FixPlanner::createPlan(
    const ScanResult& scanResult,
    const std::filesystem::path& repoRoot) const {
    FixPlan plan{};
    plan.repositoryRoot = repoRoot;

    std::set<std::string> plannedTargetPaths;
    for (const auto& provider : providers_) {
        auto itemOpt = provider->planFix(scanResult, repoRoot);
        if (!itemOpt.has_value()) {
            continue;
        }
        if (plannedTargetPaths.count(itemOpt->targetRelativePath) > 0) {
            itemOpt->hasConflict = true;
            itemOpt->conflictReason =
                "Another planned fix already targets '" + itemOpt->targetRelativePath + "'.";
        }
        plannedTargetPaths.insert(itemOpt->targetRelativePath);
        plan.fixes.push_back(std::move(*itemOpt));
    }
    return plan;
}

std::string FixPlanner::generateUnifiedDiff(
    const std::string& relativePath,
    const std::string& oldContent,
    const std::string& newContent,
    bool isNewFile) {
    std::ostringstream diff;
    diff << "--- " << (isNewFile ? "/dev/null" : ("a/" + relativePath)) << "\n";
    diff << "+++ b/" << relativePath << "\n";

    const auto oldLines = splitLines(oldContent);
    const auto newLines = splitLines(newContent);

    // Find common prefix
    std::size_t prefixLen = 0;
    while (prefixLen < oldLines.size() && prefixLen < newLines.size() &&
           oldLines[prefixLen] == newLines[prefixLen]) {
        ++prefixLen;
    }

    diff << "@@ -" << (oldLines.empty() ? 0 : 1) << "," << oldLines.size()
         << " +1," << newLines.size() << " @@\n";

    for (std::size_t i = 0; i < prefixLen; ++i) {
        diff << " " << oldLines[i] << "\n";
    }
    for (std::size_t i = prefixLen; i < oldLines.size(); ++i) {
        diff << "-" << oldLines[i] << "\n";
    }
    for (std::size_t i = prefixLen; i < newLines.size(); ++i) {
        diff << "+" << newLines[i] << "\n";
    }
    return diff.str();
}

bool FixExecutor::isSafeRelativeTargetPath(
    const std::filesystem::path& repoRoot,
    const std::string& targetRelativePath,
    std::filesystem::path* resolvedOut,
    std::string* errorReasonOut) {
    if (targetRelativePath.empty()) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "Target relative path must not be empty.";
        }
        return false;
    }

    const std::filesystem::path relPath(targetRelativePath);
    if (relPath.is_absolute() || relPath.has_root_name() || relPath.has_root_directory()) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "Target path must be relative, not absolute.";
        }
        return false;
    }

    for (const auto& part : relPath) {
        const std::string s = pathToUtf8(part);
        std::string segment = s;
#ifdef _WIN32
        std::transform(segment.begin(), segment.end(), segment.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (segment.find(':') != std::string::npos || (!segment.empty() &&
            (segment.back() == '.' || segment.back() == ' '))) return false;
#endif
        if (segment == ".." || segment == ".git") {
            if (errorReasonOut != nullptr) {
                *errorReasonOut =
                    "Target path contains disallowed traversal or .git segment: " + s;
            }
            return false;
        }
    }

    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(repoRoot, ec);
    if (ec) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "Unable to canonicalize repository root: " + ec.message();
        }
        return false;
    }

    // Check every existing ancestor component for symlinks to block symlink escape attacks
    std::filesystem::path cur = canonicalRoot;
    for (const auto& part : relPath) {
        cur /= part;
        std::error_code symEc;
        if (std::filesystem::exists(std::filesystem::symlink_status(cur, symEc))) {
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(cur, symEc))) {
                if (errorReasonOut != nullptr) {
                    *errorReasonOut =
                        "Refusing to traverse or overwrite symbolic link: " + pathToUtf8(cur);
                }
                return false;
            }
        }
    }

    const auto canonicalTarget = std::filesystem::weakly_canonical(canonicalRoot / relPath, ec);
    if (ec) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "Failed to canonicalize target path: " + ec.message();
        }
        return false;
    }

    const std::string rootStr = pathToGenericUtf8(canonicalRoot);
    const std::string targetStr = pathToGenericUtf8(canonicalTarget);
    if (targetStr.size() <= rootStr.size() ||
        targetStr.substr(0, rootStr.size()) != rootStr ||
        (targetStr[rootStr.size()] != '/' && rootStr.back() != '/')) {
        if (errorReasonOut != nullptr) {
            *errorReasonOut = "Resolved target path escapes repository root.";
        }
        return false;
    }

    if (resolvedOut != nullptr) {
        *resolvedOut = canonicalTarget;
    }
    return true;
}

FixExecutionReport FixExecutor::execute(
    const FixPlan& plan,
    const std::set<std::string>& selectedFixIds,
    bool dryRun,
    bool createBackups) const {
    FixExecutionReport report{};
    report.dryRun = dryRun;
    report.allSucceeded = true;

    for (const auto& fix : plan.fixes) {
        if (selectedFixIds.count(fix.fixId) == 0) {
            FixExecutionItemResult item{};
            item.fixId = fix.fixId;
            item.targetRelativePath = fix.targetRelativePath;
            item.skipped = true;
            item.message = "Not selected.";
            report.skippedCount++;
            report.results.push_back(std::move(item));
            continue;
        }

        FixExecutionItemResult item{};
        item.fixId = fix.fixId;
        item.targetRelativePath = fix.targetRelativePath;

        if (fix.hasConflict) {
            item.applied = false;
            item.message = "Aborted due to conflict: " + fix.conflictReason;
            report.failedCount++;
            report.allSucceeded = false;
            report.results.push_back(std::move(item));
            continue;
        }

        std::filesystem::path resolvedTarget;
        std::string safetyError;
        if (!isSafeRelativeTargetPath(
                plan.repositoryRoot, fix.targetRelativePath, &resolvedTarget, &safetyError)) {
            item.applied = false;
            item.message = "Rejected unsafe target path: " + safetyError;
            report.failedCount++;
            report.allSucceeded = false;
            report.results.push_back(std::move(item));
            continue;
        }

        // Verify file did not change since planning
        std::error_code ec;
        const bool existsNow = std::filesystem::exists(resolvedTarget, ec);
        if (fix.operation == FixOperationType::CreateNewFile && existsNow) {
            item.applied = false;
            item.message =
                "Conflict: target file '" + fix.targetRelativePath +
                "' already exists on disk; refusing to overwrite.";
            report.failedCount++;
            report.allSucceeded = false;
            report.results.push_back(std::move(item));
            continue;
        }
        if (fix.operation == FixOperationType::AppendOrModifyFile && !existsNow) {
            item.message = "Conflict: target was removed after planning.";
            ++report.failedCount;
            report.allSucceeded = false;
            report.results.push_back(std::move(item));
            continue;
        }
        if (fix.operation == FixOperationType::AppendOrModifyFile && existsNow) {
            const std::string currentDiskContent = readExistingFileOrEmpty(resolvedTarget);
            if (currentDiskContent != fix.originalContent) {
                item.applied = false;
                item.message =
                    "Conflict: target file '" + fix.targetRelativePath +
                    "' was modified after fix plan was created.";
                report.failedCount++;
                report.allSucceeded = false;
                report.results.push_back(std::move(item));
                continue;
            }
        }

        if (dryRun) {
            item.applied = false;
            item.skipped = false;
            item.message = "[DRY-RUN] Validated safe change for " + fix.targetRelativePath;
            report.results.push_back(std::move(item));
            continue;
        }

        // Create backup if modifying an existing file
        if (createBackups && existsNow) {
            std::string backupRel =
                ".repodoctor-backup/" + fix.targetRelativePath + ".bak";
            std::filesystem::path backupAbs;
            std::string backupError;
            std::size_t version = 0;
            const std::string baseBackupRel = backupRel;
            while (isSafeRelativeTargetPath(plan.repositoryRoot, backupRel, &backupAbs, &backupError) &&
                   std::filesystem::exists(backupAbs, ec)) {
                backupRel = baseBackupRel + "." + std::to_string(++version);
            }
            if (!isSafeRelativeTargetPath(plan.repositoryRoot, backupRel, &backupAbs, &backupError)) {
                item.message = "Unsafe backup path: " + backupError;
                ++report.failedCount;
                report.allSucceeded = false;
                report.results.push_back(std::move(item));
                continue;
            }
            std::filesystem::create_directories(backupAbs.parent_path(), ec);
            std::filesystem::copy_file(
                resolvedTarget,
                backupAbs,
                std::filesystem::copy_options::none,
                ec);
            if (ec) {
                item.applied = false;
                item.message = "Failed to create backup before modifying file: " + ec.message();
                report.failedCount++;
                report.allSucceeded = false;
                report.results.push_back(std::move(item));
                continue;
            }
            item.backupRelativePath = backupRel;
        }

        const auto parentDir = resolvedTarget.parent_path();
        if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
            std::filesystem::create_directories(parentDir, ec);
            if (ec) {
                item.applied = false;
                item.message = "Failed to create parent directory: " + ec.message();
                report.failedCount++;
                report.allSucceeded = false;
                report.results.push_back(std::move(item));
                continue;
            }
        }

        // Exclusive staging prevents a pre-existing .tmp symlink being followed.
        std::random_device entropy;
        auto tempFile = resolvedTarget;
        tempFile += ".repodoctor." + std::to_string(entropy()) + ".tmp";
        if (!detail::writeNewFile(tempFile, fix.proposedContent)) {
            item.message = "Failed to create or write an exclusive staging file.";
            ++report.failedCount;
            report.allSucceeded = false;
            report.results.push_back(std::move(item));
            continue;
        }
#ifndef _WIN32
        if (existsNow) {
            const auto permissions = std::filesystem::status(resolvedTarget, ec).permissions();
            if (!ec) std::filesystem::permissions(tempFile, permissions, ec);
            if (ec) {
                const auto permissionError = ec.message();
                std::filesystem::remove(tempFile, ec);
                item.message = "Cannot preserve original file permissions: " + permissionError;
                ++report.failedCount;
                report.allSucceeded = false;
                report.results.push_back(std::move(item));
                continue;
            }
        }
#endif
        // Recheck the target after staging. Never turn a create into an overwrite.
        bool conflict = false;
        if (fix.operation == FixOperationType::CreateNewFile) {
            std::filesystem::create_hard_link(tempFile, resolvedTarget, ec);
        } else {
            std::filesystem::path rechecked;
            std::string reason;
            conflict = !isSafeRelativeTargetPath(plan.repositoryRoot, fix.targetRelativePath, &rechecked, &reason) ||
                !std::filesystem::exists(resolvedTarget, ec) ||
                readExistingFileOrEmpty(resolvedTarget) != fix.originalContent;
            if (!conflict) {
#ifdef _WIN32
                if (!MoveFileExW(tempFile.c_str(), resolvedTarget.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
                } else ec.clear();
#else
                std::filesystem::rename(tempFile, resolvedTarget, ec);
#endif
            }
        }
        const auto commitError = ec;
        std::error_code cleanupError;
        std::filesystem::remove(tempFile, cleanupError);
        if (conflict || commitError) {
            item.message = conflict ? "Conflict: target changed while preparing the fix." :
                "Failed to commit updated file: " + commitError.message();
            ++report.failedCount;
            report.allSucceeded = false;
            report.results.push_back(std::move(item));
            continue;
        }

        item.applied = true;
        item.message = "Successfully applied fix to " + fix.targetRelativePath;
        report.appliedCount++;
        report.results.push_back(std::move(item));
    }

    return report;
}

}  // namespace repodoctor
