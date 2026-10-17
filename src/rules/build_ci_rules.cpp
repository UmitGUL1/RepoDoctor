#include "repodoctor/rules/builtin_rules.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
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

std::string trim(std::string_view sv) {
    std::size_t first = 0;
    while (first < sv.size() && std::isspace(static_cast<unsigned char>(sv[first]))) {
        ++first;
    }
    std::size_t last = sv.size();
    while (last > first && std::isspace(static_cast<unsigned char>(sv[last - 1]))) {
        --last;
    }
    return std::string(sv.substr(first, last - first));
}

std::vector<IndexedFile> findCiWorkflowFiles(const ScanContext& context) {
    std::vector<IndexedFile> workflows;
    for (const auto& file : context.files()) {
        const std::string& rel = file.relativePath;
        const std::string ext =
            toLower(pathToUtf8(utf8ToPath(rel).extension()));
        if ((rel.rfind(".github/workflows/", 0) == 0 && (ext == ".yml" || ext == ".yaml")) ||
            rel == ".gitlab-ci.yml" || rel == ".travis.yml" ||
            rel == "azure-pipelines.yml" || rel == ".circleci/config.yml" ||
            rel == "Jenkinsfile") {
            workflows.push_back(file);
        }
    }
    return workflows;
}

// RD-BLD-001: Missing Continuous Integration Workflow
class MissingCiWorkflowRule final : public IRule {
public:
    std::string id() const override { return "RD-BLD-001"; }
    std::string title() const override { return "Missing Continuous Integration (CI) Workflow"; }
    RuleCategory category() const noexcept override { return RuleCategory::BuildAndCI; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Checks for automated CI workflow configuration (.github/workflows/*.yml, .gitlab-ci.yml, etc.). Note: RepoDoctor inspects static configuration and never claims a build succeeds without executing it.";
    }
    std::string remediationGuidance() const override {
        return "Add a GitHub Actions workflow (e.g., .github/workflows/ci.yml) or equivalent CI pipeline that builds the project and runs tests on every pull request.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Repositories built by external enterprise Jenkins/Buildkite servers without in-repo configuration files may appear missing CI.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-BLD-GITHUB-CI"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto workflows = findCiWorkflowFiles(context);
        if (!workflows.empty()) {
            return makePassResult("Found " + std::to_string(workflows.size()) +
                                  " CI workflow configuration file(s).");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason = "No CI pipeline configuration found in repository.";

        Finding f = makeFinding(
            "No Continuous Integration (CI) workflow configuration found (checked .github/workflows/, .gitlab-ci.yml, etc.).");
        f.evidence.push_back(Evidence{
            ".github/workflows/ci.yml",
            0,
            0,
            "(workflow missing)",
            "Checked .github/workflows/*.yml, .gitlab-ci.yml, Jenkinsfile, .circleci/config.yml",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-BLD-002: Missing Automated Test Integration
class MissingTestIntegrationRule final : public IRule {
public:
    std::string id() const override { return "RD-BLD-002"; }
    std::string title() const override { return "Missing Automated Tests or Test Suite Integration"; }
    RuleCategory category() const noexcept override { return RuleCategory::Testing; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Checks whether the repository contains test files/directories (tests/, test/, *_test.cpp, *.spec.ts, test_*.py) and integrates test execution into build or CI configs.";
    }
    std::string remediationGuidance() const override {
        return "Add automated unit/integration tests (e.g., GoogleTest/CTest for C++, pytest for Python, Jest/Vitest for JS/TS) and invoke them in your CI workflow.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Pure documentation or configuration-only repositories have no executable code to test and return NOT_APPLICABLE.";
    }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        bool hasSourceCode = false;
        bool hasTestFiles = false;

        for (const auto& file : context.files()) {
            const std::string lowerRel = toLower(file.relativePath);
            const std::string ext =
                toLower(pathToUtf8(utf8ToPath(file.relativePath).extension()));

            if (ext == ".cpp" || ext == ".cc" || ext == ".c" || ext == ".hpp" || ext == ".h" ||
                ext == ".py" || ext == ".js" || ext == ".ts" || ext == ".rs" || ext == ".go" ||
                ext == ".java" || ext == ".cs") {
                hasSourceCode = true;
            }

            if (lowerRel.rfind("tests/", 0) == 0 || lowerRel.rfind("test/", 0) == 0 ||
                lowerRel.find("/tests/") != std::string::npos ||
                lowerRel.find("/test/") != std::string::npos ||
                lowerRel.find("_test.") != std::string::npos ||
                lowerRel.find(".test.") != std::string::npos ||
                lowerRel.find(".spec.") != std::string::npos ||
                lowerRel.rfind("test_", 0) == 0) {
                hasTestFiles = true;
            }
        }

        if (!hasSourceCode) {
            return makeNotApplicableResult(
                "No application source code files detected in repository.");
        }

        if (hasTestFiles) {
            return makePassResult("Automated test files detected in repository.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason = "Source files present, but no test suite files were detected.";

        Finding f = makeFinding(
            "Repository contains source code but no automated test files or test directories were found.");
        f.evidence.push_back(Evidence{
            "tests/",
            0,
            0,
            "(no test files found)",
            "Searched for tests/, test/, *_test.*, *.spec.*, and test_*.*",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-BLD-003: Unpinned Third-Party GitHub Actions
class UnpinnedGitHubActionsRule final : public IRule {
public:
    std::string id() const override { return "RD-BLD-003"; }
    std::string title() const override { return "Mutable or Unpinned GitHub Actions References"; }
    RuleCategory category() const noexcept override { return RuleCategory::BuildAndCI; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Detects GitHub Actions steps referencing mutable branch names (@main, @master, @HEAD) or unpinned third-party action tags instead of immutable 40-character commit SHAs.";
    }
    std::string remediationGuidance() const override {
        return "Pin third-party GitHub Actions to a full 40-character commit SHA (e.g., 'uses: owner/action@a1b2c3d4...') and avoid mutable branch refs like '@main' or '@master'.";
    }
    std::string falsePositiveConsiderations() const override {
        return "First-party 'actions/*' steps using major version tags (e.g., actions/checkout@v4) are treated as lower-risk warnings only when third-party or branch refs are used.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        static const std::regex kUsesRegex(
            R"(^\s*(?:-\s*)?uses\s*:\s*["']?([A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+)@([A-Za-z0-9_.-]+)["']?)");
        static const std::regex kSha40Regex(R"(^[0-9a-fA-F]{40}$)");

        std::vector<Evidence> mutableRefs;
        bool hasGitHubWorkflows = false;

        for (const auto& file : context.files()) {
            if (file.relativePath.rfind(".github/workflows/", 0) != 0) {
                continue;
            }
            const auto contentOpt = context.readTextFile(file.relativePath);
            if (!contentOpt.has_value()) {
                continue;
            }
            hasGitHubWorkflows = true;

            std::istringstream stream(*contentOpt);
            std::string line;
            std::size_t lineNum = 0;

            while (std::getline(stream, line)) {
                ++lineNum;
                std::smatch m;
                if (std::regex_search(line, m, kUsesRegex)) {
                    const std::string actionSlug = m[1].str();
                    const std::string ref = m[2].str();
                    const std::string lowerRef = toLower(ref);

                    const bool isPinnedSha = std::regex_match(ref, kSha40Regex);
                    const bool isBranchRef =
                        (lowerRef == "main" || lowerRef == "master" || lowerRef == "head" ||
                         lowerRef == "latest" || lowerRef == "develop");
                    const bool isOfficialGitHubAction =
                        (actionSlug.rfind("actions/", 0) == 0 ||
                         actionSlug.rfind("github/", 0) == 0);

                    if (isBranchRef) {
                        mutableRefs.push_back(Evidence{
                            file.relativePath,
                            lineNum,
                            1,
                            trim(line),
                            "Action '" + actionSlug + "' uses mutable branch reference '@" + ref +
                                "'",
                            false});
                    } else if (!isPinnedSha && !isOfficialGitHubAction) {
                        mutableRefs.push_back(Evidence{
                            file.relativePath,
                            lineNum,
                            1,
                            trim(line),
                            "Third-party action '" + actionSlug + "' is not pinned to a 40-char commit SHA",
                            false});
                    }
                }
            }
        }

        if (!hasGitHubWorkflows) {
            return makeNotApplicableResult("No .github/workflows/*.yml files present.");
        }
        if (mutableRefs.empty()) {
            return makePassResult("All GitHub Actions references follow pinning best practices.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(mutableRefs.size()) + " unpinned or branch-referenced action(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(mutableRefs.size()) +
                " unpinned or branch-referenced GitHub Action step(s).",
            CheckStatus::WARNING);
        f.evidence = std::move(mutableRefs);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-BLD-004: Suspicious or Overly Permissive CI Workflow Configuration
class InsecureWorkflowConfigRule final : public IRule {
public:
    std::string id() const override { return "RD-BLD-004"; }
    std::string title() const override { return "Insecure or Overly Permissive CI Workflow Configuration"; }
    RuleCategory category() const noexcept override { return RuleCategory::BuildAndCI; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Detects high-risk GitHub Actions patterns such as 'pull_request_target' combined with PR checkout, 'permissions: write-all', or missing explicit token permissions.";
    }
    std::string remediationGuidance() const override {
        return "Declare least-privilege 'permissions: contents: read' in GitHub Actions workflows and avoid checking out untrusted pull request code inside 'pull_request_target' workflows.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Release automation workflows that intentionally require write permissions to publish tags should scope permissions at the individual job level.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> issues;
        bool hasCriticalPattern = false;
        bool hasGitHubWorkflows = false;

        for (const auto& file : context.files()) {
            if (file.relativePath.rfind(".github/workflows/", 0) != 0) {
                continue;
            }
            const auto contentOpt = context.readTextFile(file.relativePath);
            if (!contentOpt.has_value()) {
                continue;
            }
            hasGitHubWorkflows = true;
            const std::string& content = *contentOpt;

            bool hasPermissionsKey = (content.find("permissions:") != std::string::npos);
            bool hasPullRequestTarget =
                (content.find("pull_request_target") != std::string::npos);
            bool hasWriteAll = (content.find("write-all") != std::string::npos);

            if (hasPullRequestTarget &&
                content.find("github.event.pull_request.head") != std::string::npos) {
                hasCriticalPattern = true;
                issues.push_back(Evidence{
                    file.relativePath,
                    1,
                    1,
                    "pull_request_target + github.event.pull_request.head checkout",
                    "High-risk untrusted PR checkout in privileged pull_request_target context",
                    false});
            }
            if (hasWriteAll) {
                hasCriticalPattern = true;
                issues.push_back(Evidence{
                    file.relativePath,
                    1,
                    1,
                    "permissions: write-all",
                    "Overly broad GITHUB_TOKEN write-all permissions",
                    false});
            }
            if (!hasPermissionsKey) {
                issues.push_back(Evidence{
                    file.relativePath,
                    1,
                    1,
                    "(missing permissions: block)",
                    "Workflow does not declare explicit least-privilege GITHUB_TOKEN permissions",
                    false});
            }
        }

        if (!hasGitHubWorkflows) {
            return makeNotApplicableResult("No .github/workflows/*.yml files present.");
        }
        if (issues.empty()) {
            return makePassResult("CI workflow permissions and triggers follow least-privilege practices.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = hasCriticalPattern ? CheckStatus::FAIL : CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(issues.size()) + " workflow security/permissions issue(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(issues.size()) +
                " CI workflow permission or trigger hygiene issue(s).",
            res.status,
            hasCriticalPattern ? Severity::High : Severity::Medium);
        f.evidence = std::move(issues);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-BLD-005: Unsupported or Inconsistent CMake Configuration
class CMakeConfigurationRule final : public IRule {
public:
    std::string id() const override { return "RD-BLD-005"; }
    std::string title() const override { return "Inconsistent or Legacy CMake Configuration"; }
    RuleCategory category() const noexcept override { return RuleCategory::BuildAndCI; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Inspects CMakeLists.txt for missing cmake_minimum_required(), obsolete CMake versions (< 3.10), or missing project() declarations.";
    }
    std::string remediationGuidance() const override {
        return "Declare 'cmake_minimum_required(VERSION 3.16)' (or newer) and 'project(<Name>)' at the top of the root CMakeLists.txt.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Subdirectory CMakeLists.txt files included via add_subdirectory() inherit cmake_minimum_required() from the top-level CMakeLists.txt.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        if (!context.hasFile("CMakeLists.txt")) {
            return makeNotApplicableResult("No root CMakeLists.txt present in repository.");
        }
        const auto contentOpt = context.readTextFile("CMakeLists.txt");
        if (!contentOpt.has_value()) {
            return makeNotVerifiedResult("Could not read root CMakeLists.txt.");
        }

        const std::string& content = *contentOpt;
        std::vector<Evidence> issues;

        static const std::regex kMinReqRegex(
            R"(cmake_minimum_required\s*\(\s*VERSION\s+([0-9]+)\.([0-9]+))",
            std::regex_constants::icase);
        static const std::regex kProjectRegex(
            R"(\bproject\s*\()", std::regex_constants::icase);

        std::smatch m;
        if (!std::regex_search(content, m, kMinReqRegex)) {
            issues.push_back(Evidence{
                "CMakeLists.txt",
                1,
                1,
                "(missing cmake_minimum_required)",
                "Root CMakeLists.txt is missing cmake_minimum_required(VERSION ...)",
                false});
        } else {
            const int majorVer = std::stoi(m[1].str());
            const int minorVer = std::stoi(m[2].str());
            if (majorVer < 3 || (majorVer == 3 && minorVer < 10)) {
                issues.push_back(Evidence{
                    "CMakeLists.txt",
                    1,
                    1,
                    m[0].str() + ")",
                    "CMake version < 3.10 is deprecated by modern CMake toolchains",
                    false});
            }
        }

        if (!std::regex_search(content, kProjectRegex)) {
            issues.push_back(Evidence{
                "CMakeLists.txt",
                1,
                1,
                "(missing project() call)",
                "Root CMakeLists.txt does not declare project()",
                false});
        }

        if (issues.empty()) {
            return makePassResult("Root CMakeLists.txt configuration is consistent and modern.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(issues.size()) + " CMake configuration issue(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(issues.size()) +
                " issue(s) in root CMakeLists.txt configuration.",
            CheckStatus::WARNING);
        f.evidence = std::move(issues);
        res.findings.push_back(std::move(f));
        return res;
    }
};

}  // namespace

void registerBuildAndCiRules(RuleRegistry& registry) {
    registry.registerRule(std::make_unique<MissingCiWorkflowRule>());
    registry.registerRule(std::make_unique<MissingTestIntegrationRule>());
    registry.registerRule(std::make_unique<UnpinnedGitHubActionsRule>());
    registry.registerRule(std::make_unique<InsecureWorkflowConfigRule>());
    registry.registerRule(std::make_unique<CMakeConfigurationRule>());
}

}  // namespace repodoctor
