#include "repodoctor/config/configuration_manager.hpp"
#include "repodoctor/core/types.hpp"
#include "repodoctor/engine/scan_context.hpp"
#include "repodoctor/engine/scan_engine.hpp"
#include "repodoctor/fixes/fix_subsystem.hpp"
#include "repodoctor/inspectors/dependency_inspector.hpp"
#include "repodoctor/inspectors/git_repository_inspector.hpp"
#include "repodoctor/reports/report_generator.hpp"
#include "repodoctor/rules/rule_registry.hpp"
#include "repodoctor/scoring/risk_scorer.hpp"
#include "repodoctor/security/secret_detection_engine.hpp"
#include "repodoctor/sources/irepository_source.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <thread>
#include "fake_git_environment.hpp"

namespace repodoctor::testing {

namespace {

class ScopedTempRepo {
public:
    ScopedTempRepo() {
        const auto nowTicks =
            std::chrono::steady_clock::now().time_since_epoch().count();
        std::mt19937_64 rng(static_cast<std::uint64_t>(nowTicks) ^
                            reinterpret_cast<std::uintptr_t>(this));
        root_ = std::filesystem::temp_directory_path() /
                ("repodoctor_test_" + std::to_string(rng() & 0xFFFFFFFFULL));
        std::filesystem::create_directories(root_);
    }

    ~ScopedTempRepo() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    const std::filesystem::path& path() const noexcept { return root_; }

    void writeFile(const std::string& relPath, const std::string& content) const {
        const auto fullPath = root_ / utf8ToPath(relPath);
        std::error_code ec;
        std::filesystem::create_directories(fullPath.parent_path(), ec);
        std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    void initMinimalGitMetadata(const std::string& branch = "main") const {
        std::filesystem::create_directories(root_ / ".git" / "objects");
        std::filesystem::create_directories(root_ / ".git" / "refs" / "heads");
        writeFile(".git/HEAD", "ref: refs/heads/" + branch + "\n");
        writeFile(
            ".git/refs/heads/" + branch,
            "0123456789abcdef0123456789abcdef01234567\n");
    }

    void populateHealthyBaseline() const {
        initMinimalGitMetadata();
        writeFile(
            ".gitignore",
            "build/\n*.o\n*.obj\n*.exe\n.env\n.DS_Store\nThumbs.db\n");
        writeFile(
            "README.md",
            "# Healthy Project\n\n"
            "This is a complete, well-documented C++20 library and CLI tool.\n\n"
            "## Installation\n\n"
            "Run `cmake -B build && cmake --build build` to compile.\n\n"
            "## Usage\n\n"
            "Run `./build/app --help` for command-line options.\n\n"
            "## Contributing\n\n"
            "See [CONTRIBUTING.md](CONTRIBUTING.md) for pull request guidelines.\n");
        writeFile(
            "CONTRIBUTING.md",
            "# Contributing\n\nPlease run unit tests before submitting pull requests.\n");
        writeFile(
            "LICENSE",
            "MIT License\n\nCopyright (c) 2026 RepoDoctor Contributors\n"
            "Permission is hereby granted, free of charge, to any person...\n");
        writeFile(
            "CMakeLists.txt",
            "cmake_minimum_required(VERSION 3.20)\nproject(HealthyProject LANGUAGES CXX)\n");
        writeFile("src/main.cpp", "int main() { return 0; }\n");
        writeFile("tests/main_test.cpp", "int main() { return 0; }\n");
        writeFile(
            ".github/workflows/ci.yml",
            "name: CI\non: [push, pull_request]\npermissions:\n  contents: read\n"
            "jobs:\n  build:\n    runs-on: ubuntu-latest\n    steps:\n"
            "      - uses: actions/checkout@b4ffde65f46336ab88eb53be808477a3936bae11\n");
    }

private:
    std::filesystem::path root_;
};

const RuleEvaluationResult* findEval(
    const ScanResult& result,
    std::string_view ruleId) {
    for (const auto& ev : result.ruleEvaluations) {
        if (ev.ruleId == ruleId) {
            return &ev;
        }
    }
    return nullptr;
}

// Construct synthetic dummy tokens at runtime so static scanners never flag the test source file itself
std::string makeSyntheticAwsKey() {
    return std::string("AKIA") + "Z9X8C7V6B5N4M3L2";
}

std::string makeSyntheticGitHubPat() {
    return std::string("ghp_") + "Q7mN2vR9pL4xT8cW5zH1sK6uB3dF0yJ9aE2g";
}

std::string makeSyntheticStripeKey() {
    return std::string("sk_live_") + "Q7mN2vR9pL4xT8cW5zH1sK6uB3dF";
}

std::string makeSyntheticPemHeader() {
    return std::string("-----BEGIN ") + "RSA PRIVATE KEY-----";
}

}  // namespace

TEST(RuleRegistryTest, RegistersAll27BuiltinRulesWithUniqueIds) {
    const auto registry = RuleRegistry::createDefaultRegistry();
    EXPECT_EQ(registry.rules().size(), 27U);

    std::set<std::string> ids;
    for (const auto& rule : registry.rules()) {
        EXPECT_FALSE(rule->id().empty());
        EXPECT_FALSE(rule->title().empty());
        EXPECT_FALSE(rule->description().empty());
        EXPECT_FALSE(rule->remediationGuidance().empty());
        EXPECT_FALSE(rule->falsePositiveConsiderations().empty());
        EXPECT_TRUE(ids.insert(rule->id()).second) << "Duplicate rule ID: " << rule->id();
    }
}

TEST(HealthyRepositoryTest, HealthyBaselinePassesApplicableChecksWithHighScore) {
    ScopedTempRepo repo;
    repo.populateHealthyBaseline();

    LocalRepositorySource source(repo.path());
    ScanEngine engine;
    const ScanResult result = engine.scan(source);

    ASSERT_TRUE(result.scanCompleted);
    EXPECT_TRUE(result.findings.empty());
    EXPECT_DOUBLE_EQ(result.healthScore.overallScore, 100.0);
    EXPECT_EQ(result.healthScore.gradeLabel, "A");
}

TEST(DocumentationRulesTest, DetectsMissingAndStubDocumentationAndBrokenLinks) {
    ScopedTempRepo repo;
    repo.initMinimalGitMetadata();

    // Case 1: Completely missing README, CONTRIBUTING, and LICENSE
    {
        LocalRepositorySource source(repo.path());
        ScanEngine engine;
        const ScanResult res = engine.scan(source);
        ASSERT_TRUE(res.scanCompleted);

        ASSERT_NE(findEval(res, "RD-DOC-001"), nullptr);
        EXPECT_EQ(findEval(res, "RD-DOC-001")->status, CheckStatus::FAIL);

        ASSERT_NE(findEval(res, "RD-DOC-005"), nullptr);
        EXPECT_EQ(findEval(res, "RD-DOC-005")->status, CheckStatus::WARNING);

        ASSERT_NE(findEval(res, "RD-DOC-007"), nullptr);
        EXPECT_EQ(findEval(res, "RD-DOC-007")->status, CheckStatus::FAIL);
    }

    // Case 2: Stub README with broken relative link and no install/usage sections
    repo.writeFile("README.md", "# Stub\nSee [Broken](docs/does-not-exist.md).\n");
    {
        LocalRepositorySource source(repo.path());
        ScanEngine engine;
        const ScanResult res = engine.scan(source);
        ASSERT_TRUE(res.scanCompleted);

        EXPECT_EQ(findEval(res, "RD-DOC-001")->status, CheckStatus::PASS);
        EXPECT_EQ(findEval(res, "RD-DOC-002")->status, CheckStatus::WARNING);
        EXPECT_EQ(findEval(res, "RD-DOC-003")->status, CheckStatus::WARNING);
        EXPECT_EQ(findEval(res, "RD-DOC-004")->status, CheckStatus::WARNING);
        EXPECT_EQ(findEval(res, "RD-DOC-006")->status, CheckStatus::FAIL);
    }
}

TEST(GitHygieneRulesTest, DetectsGitHygieneIssuesAndAvoidsMarkdownHorizontalRuleFalsePositive) {
    ScopedTempRepo repo;
    // Corrupted .git/HEAD
    std::filesystem::create_directories(repo.path() / ".git" / "objects");
    std::filesystem::create_directories(repo.path() / ".git" / "refs");
    repo.writeFile(".git/HEAD", "corrupted_non_ref_content\n");

    // Build artifact, binary, temporary file, and merge conflict
    repo.writeFile("CMakeFiles/Target.dir/foo.o", "binary_obj");
    repo.writeFile("bin/tool.exe", std::string("\x7F\x45\x4C\x46\0\0", 6));
    repo.writeFile("notes.bak", "backup");
    repo.writeFile(
        "src/conflict.cpp",
        "int a = 1;\n<<<<<<< HEAD\nint b = 2;\n=======\nint b = 3;\n>>>>>>> feature-branch\n");
    // Markdown horizontal rule with ======= should NOT trigger RD-GIT-006 on its own
    repo.writeFile("docs/heading.md", "Title\n=======\nParagraph text.\n");

    // Large file > custom threshold
    ScanConfiguration cfg{};
    cfg.largeFileThresholdBytes = 1024;  // 1 KB for test speed
    repo.writeFile("assets/huge.bin", std::string(2048, 'X'));

    LocalRepositorySource source(repo.path());
    ScanEngine engine;
    const ScanResult res = engine.scan(source, cfg);
    ASSERT_TRUE(res.scanCompleted);

    EXPECT_EQ(findEval(res, "RD-GIT-001")->status, CheckStatus::FAIL);     // Missing .gitignore
    EXPECT_EQ(findEval(res, "RD-GIT-002")->status, CheckStatus::FAIL);     // Build artifacts
    EXPECT_EQ(findEval(res, "RD-GIT-003")->status, CheckStatus::WARNING);  // Large file
    EXPECT_EQ(findEval(res, "RD-GIT-004")->status, CheckStatus::WARNING);  // .exe binary
    EXPECT_EQ(findEval(res, "RD-GIT-005")->status, CheckStatus::WARNING);  // .bak temp file
    EXPECT_EQ(findEval(res, "RD-GIT-006")->status, CheckStatus::FAIL);     // Merge conflict
    EXPECT_EQ(findEval(res, "RD-GIT-007")->status, CheckStatus::FAIL);     // Corrupted .git/HEAD

    // Verify only src/conflict.cpp was flagged by RD-GIT-006, not docs/heading.md
    const auto* conflictEval = findEval(res, "RD-GIT-006");
    ASSERT_FALSE(conflictEval->findings.empty());
    ASSERT_EQ(conflictEval->findings[0].evidence.size(), 1U);
    EXPECT_EQ(conflictEval->findings[0].evidence[0].relativeFilePath, "src/conflict.cpp");
}

TEST(SecurityRulesAndRedactionTest, DetectsSecretsRedactsAllOutputsAndSuppressesPlaceholders) {
    ScopedTempRepo repo;
    repo.populateHealthyBaseline();

    const std::string rawAws = makeSyntheticAwsKey();
    const std::string rawGh = makeSyntheticGitHubPat();
    const std::string rawStripe = makeSyntheticStripeKey();
    const std::string rawPem = makeSyntheticPemHeader();
    const std::string rawDbPass = std::string("Sup3rS3cr3t") + "Pr0dDbP@ss99!";
    const std::string rawJwtVal = std::string("K9x!mP2$vL8@") + "qW4#zR7&nB5*tY1^uJ3";

    // True positives
    repo.writeFile(
        "src/config.cpp",
        "const char* aws = \"" + rawAws + "\";\n"
        "const char* gh = \"" + rawGh + "\";\n"
        "const char* stripe = \"" + rawStripe + "\";\n"
        "const char* db = \"postgres://admin:" + rawDbPass + "@db.internal:5432/prod\";\n");
    repo.writeFile("keys/server.pem", rawPem + "\nMIIEpAIBAAKCAQEA...\n");
    repo.writeFile(".env", std::string("JWT_") + "SECRET=" + rawJwtVal + "\n");

    // False positive candidates (must NOT be flagged)
    repo.writeFile(
        ".env.example",
        "AWS_ACCESS_KEY_ID=AKIAIOSFODNN7EXAMPLE\n"
        "API_KEY=your_api_key_placeholder\n");
    repo.writeFile(
        "src/safe_env.cpp",
        "auto key = std::getenv(\"API_KEY\");\n"
        "const char* doc = \"AKIAIOSFODNN7EXAMPLE\";\n");

    LocalRepositorySource source(repo.path());
    ScanEngine engine;
    const ScanResult res = engine.scan(source);
    ASSERT_TRUE(res.scanCompleted);

    EXPECT_EQ(findEval(res, "RD-SEC-001")->status, CheckStatus::FAIL);
    EXPECT_EQ(findEval(res, "RD-SEC-002")->status, CheckStatus::FAIL);
    EXPECT_EQ(findEval(res, "RD-SEC-003")->status, CheckStatus::FAIL);
    EXPECT_EQ(findEval(res, "RD-SEC-004")->status, CheckStatus::FAIL);

    // Verify that NONE of the raw secrets appear in any report format!
    ReportGenerator gen;
    const std::string consoleRep = gen.generateConsoleReport(res);
    const std::string jsonRep = gen.generateJsonReport(res);
    const std::string sarifRep = gen.generateSarifReport(res);
    const std::string htmlRep = gen.generateHtmlReport(res);

    for (const std::string& reportText : {consoleRep, jsonRep, sarifRep, htmlRep}) {
        EXPECT_EQ(reportText.find(rawAws), std::string::npos);
        EXPECT_EQ(reportText.find(rawGh), std::string::npos);
        EXPECT_EQ(reportText.find(rawStripe), std::string::npos);
        EXPECT_EQ(reportText.find(rawDbPass), std::string::npos);
        EXPECT_EQ(reportText.find(rawJwtVal), std::string::npos);
    }

    // Verify remediation includes Git history warning
    EXPECT_NE(consoleRep.find("Git history"), std::string::npos);
}

TEST(BuildCiAndDependencyRulesTest, DetectsCiAndDependencyHygieneAndAdvisoryCache) {
    ScopedTempRepo repo;
    repo.initMinimalGitMetadata();
    repo.writeFile("CMakeLists.txt", "cmake_minimum_required(VERSION 2.8)\n");
    repo.writeFile("src/app.cpp", "int main() { return 0; }\n");
    repo.writeFile(
        ".github/workflows/unsafe.yml",
        "name: Unsafe\non: pull_request_target\npermissions: write-all\n"
        "jobs:\n  pr:\n    runs-on: ubuntu-latest\n    steps:\n"
        "      - uses: thirdparty/custom-action@main\n"
        "      - run: git checkout ${{ github.event.pull_request.head.sha }}\n");
    repo.writeFile(
        "package.json",
        R"({
  "name": "demo",
  "dependencies": {
    "lodash": "4.17.15",
    "left-pad": "*"
  },
  "devDependencies": {
    "lodash": "^4.17.21"
  }
})");
    repo.writeFile(
        ".repodoctor-advisories.json",
        R"({
  "source": "Local OSV Snapshot 2026-10",
  "advisories": [
    {
      "id": "GHSA-jf85-cpcp-j695",
      "package": "lodash",
      "affected_version": "4.17.15",
      "severity": "HIGH",
      "summary": "Prototype Pollution in lodash",
      "fixed_version": "4.17.21"
    }
  ]
})");

    ScanConfiguration cfg{};
    cfg.enableAdvisoryLookup = true;

    LocalRepositorySource source(repo.path());
    ScanEngine engine;
    const ScanResult res = engine.scan(source, cfg);
    ASSERT_TRUE(res.scanCompleted);

    EXPECT_EQ(findEval(res, "RD-BLD-001")->status, CheckStatus::PASS);
    EXPECT_EQ(findEval(res, "RD-BLD-002")->status, CheckStatus::FAIL);     // Missing tests
    EXPECT_EQ(findEval(res, "RD-BLD-003")->status, CheckStatus::WARNING);  // Unpinned action @main
    EXPECT_EQ(findEval(res, "RD-BLD-004")->status, CheckStatus::FAIL);     // pull_request_target + write-all
    EXPECT_EQ(findEval(res, "RD-BLD-005")->status, CheckStatus::WARNING);  // CMake 2.8 & missing project()
    EXPECT_EQ(findEval(res, "RD-DEP-001")->status, CheckStatus::WARNING);  // Missing package-lock.json
    EXPECT_EQ(findEval(res, "RD-DEP-002")->status, CheckStatus::WARNING);  // Wildcard '*' + duplicate lodash
    EXPECT_EQ(findEval(res, "RD-DEP-003")->status, CheckStatus::FAIL);     // Matched local OSV advisory
}

TEST(EdgeCasesTest, EmptyRepoUnicodeFilenamesLongPathsMalformedFilesSymlinksAndCancellation) {
    ScopedTempRepo repo;

    // 1. Empty repository scan
    {
        LocalRepositorySource source(repo.path());
        ScanEngine engine;
        const ScanResult emptyRes = engine.scan(source);
        ASSERT_TRUE(emptyRes.scanCompleted);
        EXPECT_EQ(emptyRes.statistics.totalFilesDiscovered, 0U);
        // Dependency & Testing rules should be NOT_APPLICABLE on an empty repo
        EXPECT_EQ(findEval(emptyRes, "RD-DEP-001")->status, CheckStatus::NOT_APPLICABLE);
        EXPECT_EQ(findEval(emptyRes, "RD-BLD-002")->status, CheckStatus::NOT_APPLICABLE);
    }

    // 2. Unicode filenames, long nested paths, and malformed JSON manifests
    const auto unicodeName = pathToGenericUtf8(std::filesystem::path(u8"docs/\u00fcmit_g\u00fcl_dok\u00fcman_\u4e2d\u6587.md"));
    repo.writeFile(unicodeName, "# Unicode Markdown\nValid content.\n");
    ASSERT_TRUE(std::filesystem::exists(repo.path() / utf8ToPath(unicodeName)));
    repo.writeFile(
        "deep/level1/level2/level3/level4/level5/level6/level7/nested_module.py",
        "def hello():\n    return 42\n");
    repo.writeFile("package.json", "{ malformed json without closing brace ");
    repo.writeFile(".repodoctor.json", "{ invalid config json ");

    // 3. Symlink inside repository (where supported by OS)
    std::error_code symEc;
    std::filesystem::create_symlink(
        repo.path() / "docs", repo.path() / "linked_docs", symEc);

    {
        LocalRepositorySource source(repo.path());
        ScanEngine engine;
        const ScanResult res = engine.scan(source);
        ASSERT_TRUE(res.scanCompleted);
        EXPECT_FALSE(res.configurationWarnings.empty());
        EXPECT_EQ(findEval(res, "RD-DEP-002")->status, CheckStatus::FAIL);
        if (!symEc) {
            EXPECT_GE(res.statistics.symlinksSkipped, 1U);
        }
    }

    // 4. Cancellation support
    {
        std::atomic<bool> cancelFlag{true};
        LocalRepositorySource source(repo.path());
        ScanEngine engine;
        const ScanResult cancelledRes = engine.scan(source, std::nullopt, &cancelFlag);
        EXPECT_FALSE(cancelledRes.scanCompleted);
        EXPECT_TRUE(cancelledRes.statistics.wasCancelled);
    }
}

TEST(AutoFixSubsystemTest, PreviewsUnifiedDiffsAppliesSafelyAndBlocksPathTraversalAndConflicts) {
    ScopedTempRepo repo;
    repo.initMinimalGitMetadata();

    LocalRepositorySource source(repo.path());
    ScanEngine engine;
    const ScanResult scanRes = engine.scan(source);
    ASSERT_TRUE(scanRes.scanCompleted);

    FixPlanner planner;
    const FixPlan plan = planner.createPlan(scanRes, repo.path());
    ASSERT_GE(plan.fixes.size(), 4U);

    for (const auto& fix : plan.fixes) {
        EXPECT_FALSE(fix.unifiedDiff.empty());
        EXPECT_NE(fix.unifiedDiff.find("+++ b/"), std::string::npos);
    }

    std::set<std::string> allFixIds;
    for (const auto& fix : plan.fixes) allFixIds.insert(fix.fixId);
    // Dry-run execution must not create files
    FixExecutor executor;
    const auto dryReport = executor.execute(plan, allFixIds, true, true);
    EXPECT_TRUE(dryReport.dryRun);
    EXPECT_TRUE(dryReport.allSucceeded);
    EXPECT_FALSE(std::filesystem::exists(repo.path() / "README.md"));

    // Apply execution creates files safely
    const auto applyReport = executor.execute(plan, allFixIds, false, true);
    EXPECT_FALSE(applyReport.dryRun);
    EXPECT_TRUE(applyReport.allSucceeded);
    EXPECT_GT(applyReport.appliedCount, 0U);
    EXPECT_TRUE(std::filesystem::exists(repo.path() / "README.md"));
    EXPECT_TRUE(std::filesystem::exists(repo.path() / ".gitignore"));
    EXPECT_TRUE(std::filesystem::exists(repo.path() / "CONTRIBUTING.md"));
    EXPECT_TRUE(std::filesystem::exists(repo.path() / ".github/workflows/ci.yml"));

    // Re-executing the old CreateFile plan when files already exist must detect conflict and refuse overwrite
    const auto conflictReport = executor.execute(plan, {"FIX-DOC-README-TEMPLATE"}, false, true);
    EXPECT_FALSE(conflictReport.allSucceeded);
    EXPECT_EQ(conflictReport.failedCount, 1U);

    // Path traversal & .git modification must be strictly rejected
    std::string err;
    EXPECT_FALSE(FixExecutor::isSafeRelativeTargetPath(repo.path(), "../escape.txt", nullptr, &err));
    EXPECT_FALSE(FixExecutor::isSafeRelativeTargetPath(repo.path(), "/etc/passwd", nullptr, &err));
    EXPECT_FALSE(FixExecutor::isSafeRelativeTargetPath(repo.path(), ".git/config", nullptr, &err));
#ifdef _WIN32
    EXPECT_FALSE(FixExecutor::isSafeRelativeTargetPath(repo.path(), ".GIT/config", nullptr, &err));
    EXPECT_FALSE(FixExecutor::isSafeRelativeTargetPath(repo.path(), "file.txt:stream", nullptr, &err));
#endif
}

TEST(GitHubUrlValidationTest, ValidatesPublicHttpsUrlsAndBlocksOptionOrCommandInjection) {
    std::string norm;
    std::string slug;
    std::string err;

    EXPECT_TRUE(GitHubRepositorySource::isValidGitHubRepositoryUrl(
        "https://github.com/nlohmann/json", &norm, &slug, &err));
    EXPECT_EQ(norm, "https://github.com/nlohmann/json.git");
    EXPECT_EQ(slug, "nlohmann/json");

    EXPECT_FALSE(GitHubRepositorySource::isValidGitHubRepositoryUrl(
        "--upload-pack=touch /tmp/pwn", &norm, &slug, &err));
    EXPECT_FALSE(GitHubRepositorySource::isValidGitHubRepositoryUrl(
        "https://github.com/owner/repo;rm -rf /", &norm, &slug, &err));
    EXPECT_FALSE(GitHubRepositorySource::isValidGitHubRepositoryUrl(
        "file:///etc/passwd", &norm, &slug, &err));
    EXPECT_FALSE(GitHubRepositorySource::isValidGitHubRepositoryUrl(
        "https://github.com/../etc", &norm, &slug, &err));
}

TEST(ReportFormatsTest, GeneratesValidJsonSarifAndHtmlReports) {
    ScopedTempRepo repo;
    repo.initMinimalGitMetadata();

    LocalRepositorySource source(repo.path());
    ScanEngine engine;
    const ScanResult res = engine.scan(source);
    ASSERT_TRUE(res.scanCompleted);

    ReportGenerator gen;

    // JSON report validation
    const auto jsonDoc = nlohmann::json::parse(gen.generateJsonReport(res));
    EXPECT_EQ(jsonDoc["tool"]["name"], "RepoDoctor");
    EXPECT_TRUE(jsonDoc["scan_completed"].get<bool>());
    EXPECT_TRUE(jsonDoc.contains("health_score"));
    EXPECT_TRUE(jsonDoc.contains("findings"));

    // SARIF 2.1.0 report validation
    const auto sarifDoc = nlohmann::json::parse(gen.generateSarifReport(res));
    EXPECT_EQ(sarifDoc["version"], "2.1.0");
    ASSERT_TRUE(sarifDoc["runs"].is_array());
    ASSERT_EQ(sarifDoc["runs"].size(), 1U);
    EXPECT_EQ(sarifDoc["runs"][0]["tool"]["driver"]["name"], "RepoDoctor");
    EXPECT_FALSE(sarifDoc["runs"][0]["results"].empty());

    // HTML report validation
    const std::string html = gen.generateHtmlReport(res);
    EXPECT_NE(html.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(html.find("RepoDoctor Health Report"), std::string::npos);
}

}  // namespace repodoctor::testing

namespace repodoctor::testing {

TEST(AutoFixRegressionTest, EmptySelectionNeverAppliesChanges) {
    ScopedTempRepo repo;
    repo.initMinimalGitMetadata();
    LocalRepositorySource source(repo.path());
    const auto scan = ScanEngine{}.scan(source);
    ASSERT_TRUE(scan.scanCompleted);
    const auto plan = FixPlanner{}.createPlan(scan, repo.path());
    ASSERT_FALSE(plan.fixes.empty());
    const auto result = FixExecutor{}.execute(plan, {}, false, true);
    EXPECT_EQ(result.appliedCount, 0U);
    EXPECT_EQ(result.skippedCount, plan.fixes.size());
    EXPECT_FALSE(std::filesystem::exists(repo.path() / "README.md"));
}

TEST(AutoFixRegressionTest, RemovedModificationTargetIsNotRecreated) {
    ScopedTempRepo repo;
    FixPlan plan;
    plan.repositoryRoot = repo.path();
    PlannedFix fix;
    fix.fixId = "regression";
    fix.targetRelativePath = "removed.txt";
    fix.operation = FixOperationType::AppendOrModifyFile;
    fix.originalContent = "before";
    fix.proposedContent = "after";
    plan.fixes.push_back(fix);
    const auto result = FixExecutor{}.execute(plan, {fix.fixId}, false, true);
    EXPECT_EQ(result.failedCount, 1U);
    EXPECT_FALSE(std::filesystem::exists(repo.path() / "removed.txt"));
}

} // namespace repodoctor::testing

namespace repodoctor::testing {
TEST(SecurityRegressionTest, MixedSecretsNeverLeakThroughSharedLineEvidence) {
    ScopedTempRepo repo;
    repo.populateHealthyBaseline();
    const auto aws = makeSyntheticAwsKey();
    const auto github = makeSyntheticGitHubPat();
    repo.writeFile("credentials.conf", "cloud=" + aws + "; github=" + github + " # example comment\n");
    LocalRepositorySource source(repo.path());
    const auto scan = ScanEngine{}.scan(source);
    ASSERT_TRUE(scan.scanCompleted);
    ASSERT_EQ(findEval(scan, "RD-SEC-001")->status, CheckStatus::FAIL);
    ReportGenerator generator;
    for (const auto& text : {generator.generateConsoleReport(scan), generator.generateJsonReport(scan),
            generator.generateSarifReport(scan), generator.generateHtmlReport(scan)}) {
        EXPECT_EQ(text.find(aws), std::string::npos);
        EXPECT_EQ(text.find(github), std::string::npos);
    }
    const auto matches = SecretDetectionEngine{}.scanFileContent("credentials.conf", "cloud=" + aws + "; github=" + github);
    EXPECT_GE(matches.size(), 2U);
}

TEST(ScanLimitRegressionTest, FileCapDoesNotProduceACompletedHealthReport) {
    ScopedTempRepo repo;
    repo.writeFile("one.txt", "one");
    repo.writeFile("two.txt", "two");
    ScanConfiguration config;
    config.maxRepositoryScanFiles = 1;
    LocalRepositorySource source(repo.path());
    const auto scan = ScanEngine{}.scan(source, config);
    EXPECT_FALSE(scan.scanCompleted);
    EXPECT_TRUE(scan.statistics.fileLimitReached);
}

TEST(CloneLimitsRegressionTest, SettingsRoundTripAndCancelledCloneDoesNotStart) {
    ScopedTempRepo repo;
    ScanConfiguration config;
    config.cloneTimeoutSeconds = 7;
    config.maxCloneSizeBytes = 12345;
    ConfigurationManager manager;
    ASSERT_TRUE(manager.saveToFile(config, repo.path() / ".repodoctor.json"));
    const auto loaded = manager.loadFromRepository(repo.path());
    EXPECT_EQ(loaded.cloneTimeoutSeconds, 7);
    EXPECT_EQ(loaded.maxCloneSizeBytes, 12345U);
    std::atomic<bool> cancelled{true};
    GitHubRepositorySource source("https://github.com/owner/repository");
    source.setCancellationToken(&cancelled);
    const auto prepared = source.prepare(config);
    EXPECT_FALSE(prepared.success);
    EXPECT_NE(prepared.errorMessage.find("cancelled"), std::string::npos);
}

TEST(AutoFixRegressionTest, ExistingBackupsArePreservedAcrossRuns) {
    ScopedTempRepo repo;
    repo.writeFile("config.txt", "before");
    repo.writeFile(".repodoctor-backup/config.txt.bak", "old backup");
    FixPlan plan;
    plan.repositoryRoot = repo.path();
    PlannedFix fix;
    fix.fixId = "backup-test";
    fix.targetRelativePath = "config.txt";
    fix.operation = FixOperationType::AppendOrModifyFile;
    fix.originalContent = "before";
    fix.proposedContent = "after";
    plan.fixes.push_back(fix);
    const auto result = FixExecutor{}.execute(plan, {fix.fixId}, false, true);
    EXPECT_EQ(result.appliedCount, 1U);
    ASSERT_EQ(result.results.size(), 1U);
    EXPECT_EQ(result.results[0].backupRelativePath, ".repodoctor-backup/config.txt.bak.1");
    std::ifstream old(repo.path() / ".repodoctor-backup/config.txt.bak");
    std::string content;
    std::getline(old, content);
    EXPECT_EQ(content, "old backup");
}

TEST(AutoFixRegressionTest, BackupDirectorySymlinkIsRejected) {
    ScopedTempRepo repo;
    ScopedTempRepo outside;
    repo.writeFile("config.txt", "before");
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside.path(), repo.path() / ".repodoctor-backup", ec);
    if (ec) GTEST_SKIP() << "Symlink creation unavailable: " << ec.message();
    FixPlan plan;
    plan.repositoryRoot = repo.path();
    PlannedFix fix;
    fix.fixId = "backup-symlink";
    fix.targetRelativePath = "config.txt";
    fix.operation = FixOperationType::AppendOrModifyFile;
    fix.originalContent = "before";
    fix.proposedContent = "after";
    plan.fixes.push_back(fix);
    const auto result = FixExecutor{}.execute(plan, {fix.fixId}, false, true);
    EXPECT_EQ(result.failedCount, 1U);
    EXPECT_FALSE(std::filesystem::exists(outside.path() / "config.txt.bak"));
}
} // namespace repodoctor::testing

namespace repodoctor::testing {
TEST(CloneProcessRegressionTest, TimeoutAndSizeLimitStopOfflineChildProcess) {
    FakeGitEnvironment fixture;
    ScanConfiguration config;
    config.cloneTimeoutSeconds = 1;
    const auto started = std::chrono::steady_clock::now();
    GitHubRepositorySource slow("https://github.com/offline/timeout");
    const auto timeout = slow.prepare(config);
    EXPECT_FALSE(timeout.success);
    EXPECT_NE(timeout.errorMessage.find("timed out"), std::string::npos);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(5));

    config.cloneTimeoutSeconds = 5;
    config.maxCloneSizeBytes = 1024;
    GitHubRepositorySource large("https://github.com/offline/oversize");
    const auto oversize = large.prepare(config);
    EXPECT_FALSE(oversize.success);
    EXPECT_NE(oversize.errorMessage.find("size limit"), std::string::npos);
}

TEST(CloneProcessRegressionTest, EngineCancellationStopsAnActiveOfflineClone) {
    FakeGitEnvironment fixture;
    std::atomic<bool> cancelled{false};
    GitHubRepositorySource source("https://github.com/offline/timeout");
    std::thread trigger([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cancelled.store(true);
    });
    ScanResult scan;
    try {
        scan = ScanEngine{}.scan(source, std::nullopt, &cancelled);
    } catch (...) {
        trigger.join();
        throw;
    }
    trigger.join();
    EXPECT_FALSE(scan.scanCompleted);
    EXPECT_TRUE(scan.statistics.wasCancelled);
    EXPECT_NE(scan.errorMessage.find("cancelled"), std::string::npos);
}
} // namespace repodoctor::testing
