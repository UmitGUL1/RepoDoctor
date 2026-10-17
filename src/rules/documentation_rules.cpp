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

std::optional<IndexedFile> findPrimaryReadme(const ScanContext& context) {
    static const std::array<std::string_view, 6> kCandidates = {
        "README.md", "README.rst", "README.adoc", "README.txt", "README", "readme.md"};
    for (std::string_view name : kCandidates) {
        auto found = context.findFileCaseInsensitive(std::string(name));
        if (found.has_value()) {
            return found;
        }
    }
    return std::nullopt;
}

// RD-DOC-001: Missing README
class MissingReadmeRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-001"; }
    std::string title() const override { return "Missing Repository README"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Verifies that a top-level README file (e.g., README.md) exists at the repository root.";
    }
    std::string remediationGuidance() const override {
        return "Create a top-level README.md explaining the project purpose, build/installation steps, usage examples, and license.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Sub-packages or bare Git repositories without a working tree may intentionally omit a root README.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-DOC-README-TEMPLATE"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto readme = findPrimaryReadme(context);
        if (readme.has_value()) {
            return makePassResult("Found top-level README: " + readme->relativePath);
        }
        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason = "No README file found at repository root.";

        Finding f = makeFinding("Repository is missing a top-level README file.");
        f.evidence.push_back(Evidence{
            "README.md", 0, 0, "(file missing)", "Expected README.md at repository root", false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DOC-002: Empty or Stub README
class EmptyReadmeRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-002"; }
    std::string title() const override { return "Empty or Stub README File"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Checks whether the existing README file is empty or contains only a minimal one-line title stub.";
    }
    std::string remediationGuidance() const override {
        return "Expand README.md with a meaningful project summary, prerequisites, installation steps, and usage examples.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Very small utility repositories or documentation-redirect stubs may intentionally keep README.md brief.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-DOC-README-SECTIONS"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto readme = findPrimaryReadme(context);
        if (!readme.has_value()) {
            return makeNotApplicableResult("No README file present (covered by RD-DOC-001).");
        }
        const auto contentOpt = context.readTextFile(readme->relativePath);
        if (!contentOpt.has_value()) {
            return makeNotVerifiedResult("Unable to read " + readme->relativePath + " as text.");
        }
        const std::string trimmed = trim(*contentOpt);
        if (trimmed.size() >= 80) {
            return makePassResult("README contains substantive content (" +
                                  std::to_string(trimmed.size()) + " bytes).");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = trimmed.empty() ? CheckStatus::FAIL : CheckStatus::WARNING;
        res.statusReason = "README content is empty or below minimum substantive length.";

        Finding f = makeFinding(
            trimmed.empty()
                ? ("'" + readme->relativePath + "' is completely empty (0 non-whitespace bytes).")
                : ("'" + readme->relativePath + "' is a minimal stub (" +
                   std::to_string(trimmed.size()) + " bytes)."),
            res.status);
        f.evidence.push_back(Evidence{
            readme->relativePath,
            1,
            1,
            trimmed.empty() ? "(empty file)" : trimmed.substr(0, 80),
            "Substantive documentation typically exceeds 80 characters.",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DOC-003: Missing Installation Instructions
class MissingInstallationDocsRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-003"; }
    std::string title() const override { return "Missing Installation or Build Instructions"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Checks whether the README or dedicated INSTALL document describes how to build or install the project.";
    }
    std::string remediationGuidance() const override {
        return "Add an '## Installation', '## Building', or '## Getting Started' section to README.md with reproducible commands.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Documentation-only repositories or specifications may not require installation or build steps.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-DOC-README-SECTIONS"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        if (context.findFileCaseInsensitive("INSTALL.md").has_value() ||
            context.findFileCaseInsensitive("INSTALL").has_value()) {
            return makePassResult("Dedicated INSTALL document found.");
        }
        const auto readme = findPrimaryReadme(context);
        if (!readme.has_value()) {
            return makeNotApplicableResult("No README file present.");
        }
        const auto contentOpt = context.readTextFile(readme->relativePath);
        if (!contentOpt.has_value()) {
            return makeNotVerifiedResult("Unable to read README content.");
        }
        const std::string lower = toLower(*contentOpt);
        static const std::array<std::string_view, 9> kInstallKeywords = {
            "install", "building", "build instructions", "getting started", "setup",
            "quick start", "quickstart", "cmake -b", "npm install"};
        for (std::string_view kw : kInstallKeywords) {
            if (lower.find(kw) != std::string::npos) {
                return makePassResult("Installation/setup guidance detected in " +
                                      readme->relativePath);
            }
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason = "No installation or build section found in README.";

        Finding f = makeFinding(
            "No installation, setup, or build instructions detected in '" +
                readme->relativePath + "'.",
            CheckStatus::WARNING,
            Severity::Medium);
        f.evidence.push_back(Evidence{
            readme->relativePath,
            1,
            1,
            "Missing section: ## Installation / ## Building / ## Getting Started",
            "Searched README for installation and build headings/commands.",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DOC-004: Missing Usage Examples
class MissingUsageDocsRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-004"; }
    std::string title() const override { return "Missing Usage Examples in Documentation"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::Low; }
    std::string description() const override {
        return "Checks whether the README provides usage examples, CLI invocations, or API snippets.";
    }
    std::string remediationGuidance() const override {
        return "Add a '## Usage' or '## Examples' section with concrete CLI commands or code snippets.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Projects hosting external documentation sites may keep usage examples outside README.md.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-DOC-README-SECTIONS"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto readme = findPrimaryReadme(context);
        if (!readme.has_value()) {
            return makeNotApplicableResult("No README file present.");
        }
        const auto contentOpt = context.readTextFile(readme->relativePath);
        if (!contentOpt.has_value()) {
            return makeNotVerifiedResult("Unable to read README content.");
        }
        const std::string lower = toLower(*contentOpt);
        static const std::array<std::string_view, 6> kUsageKeywords = {
            "usage", "example", "how to use", "quick start", "command-line", "cli"};
        for (std::string_view kw : kUsageKeywords) {
            if (lower.find(kw) != std::string::npos) {
                return makePassResult("Usage guidance detected in " + readme->relativePath);
            }
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason = "No usage or examples section found in README.";

        Finding f = makeFinding(
            "No usage examples or '## Usage' section detected in '" + readme->relativePath + "'.",
            CheckStatus::WARNING,
            Severity::Low);
        f.evidence.push_back(Evidence{
            readme->relativePath,
            1,
            1,
            "Missing section: ## Usage / ## Examples",
            "Searched README for usage headings and example blocks.",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DOC-005: Missing Contribution Guidance
class MissingContributingRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-005"; }
    std::string title() const override { return "Missing Contribution Guidelines"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::Low; }
    std::string description() const override {
        return "Checks for CONTRIBUTING.md or a contribution section in README.md.";
    }
    std::string remediationGuidance() const override {
        return "Create CONTRIBUTING.md or add a '## Contributing' section to README.md explaining pull request workflow, coding style, and testing expectations.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Single-maintainer archive repositories not accepting external pull requests may intentionally omit contribution guidelines.";
    }
    bool hasSafeFix() const noexcept override { return true; }
    std::string safeFixId() const override { return "FIX-DOC-CONTRIBUTING-TEMPLATE"; }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        if (context.findFileCaseInsensitive("CONTRIBUTING.md").has_value() ||
            context.findFileCaseInsensitive(".github/CONTRIBUTING.md").has_value() ||
            context.findFileCaseInsensitive(" docs/CONTRIBUTING.md").has_value()) {
            return makePassResult("Dedicated CONTRIBUTING.md file found.");
        }
        const auto readme = findPrimaryReadme(context);
        if (readme.has_value()) {
            const auto contentOpt = context.readTextFile(readme->relativePath);
            if (contentOpt.has_value()) {
                const std::string lower = toLower(*contentOpt);
                if (lower.find("contribut") != std::string::npos) {
                    return makePassResult("Contribution guidance found in " + readme->relativePath);
                }
            }
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason = "Neither CONTRIBUTING.md nor a README contribution section was found.";

        Finding f = makeFinding(
            "Repository lacks CONTRIBUTING.md and contribution instructions in README.",
            CheckStatus::WARNING,
            Severity::Low);
        f.evidence.push_back(Evidence{
            "CONTRIBUTING.md",
            0,
            0,
            "(file missing)",
            "Checked root, .github/, and README headings.",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DOC-006: Broken Relative Documentation Links
class BrokenRelativeLinksRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-006"; }
    std::string title() const override { return "Broken Relative Links in Markdown Documentation"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Verifies that relative file links in Markdown files ([label](path/to/file.md)) resolve to existing files inside the repository.";
    }
    std::string remediationGuidance() const override {
        return "Update or remove broken relative links in Markdown files so all referenced documentation and assets exist.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Links pointing to build-generated artifacts or external wiki routes mounted only after site generation may appear missing in a raw source checkout.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        static const std::regex kMdLinkRegex(R"(\[[^\]]*\]\(([^)]+)\))");
        std::vector<Evidence> brokenLinks;
        std::size_t mdFilesScanned = 0;

        for (const auto& file : context.files()) {
            const std::string ext = toLower(
                pathToUtf8(utf8ToPath(file.relativePath).extension()));
            if (ext != ".md" && ext != ".markdown") {
                continue;
            }
            const auto contentOpt = context.readTextFile(file.relativePath);
            if (!contentOpt.has_value()) {
                continue;
            }
            ++mdFilesScanned;

            const auto baseDir = utf8ToPath(file.relativePath).parent_path();
            std::istringstream stream(*contentOpt);
            std::string line;
            std::size_t lineNum = 0;
            bool inCodeBlock = false;

            while (std::getline(stream, line)) {
                ++lineNum;
                const std::string trimmed = trim(line);
                if (trimmed.rfind("```", 0) == 0 || trimmed.rfind("~~~", 0) == 0) {
                    inCodeBlock = !inCodeBlock;
                    continue;
                }
                if (inCodeBlock) {
                    continue;
                }
                if (context.checkAndRecordInlineSuppression(line, id())) {
                    continue;
                }

                for (std::sregex_iterator it(line.begin(), line.end(), kMdLinkRegex), end;
                     it != end;
                     ++it) {
                    std::string target = trim((*it)[1].str());
                    if (target.empty() || target.front() == '#') {
                        continue;
                    }
                    // Strip optional title in link: (path/file.md "Title")
                    const auto spacePos = target.find(' ');
                    if (spacePos != std::string::npos) {
                        target = target.substr(0, spacePos);
                    }
                    // Skip external URLs / schemes
                    if (target.find("://") != std::string::npos ||
                        target.rfind("mailto:", 0) == 0 ||
                        target.rfind("tel:", 0) == 0 ||
                        target.rfind("data:", 0) == 0) {
                        continue;
                    }
                    // Strip anchor fragment (#section) and query (?raw=true)
                    const auto hashPos = target.find('#');
                    if (hashPos != std::string::npos) {
                        target = target.substr(0, hashPos);
                    }
                    const auto queryPos = target.find('?');
                    if (queryPos != std::string::npos) {
                        target = target.substr(0, queryPos);
                    }
                    if (target.empty()) {
                        continue;
                    }

                    std::filesystem::path resolvedRel =
                        (target.front() == '/')
                            ? utf8ToPath(target.substr(1)).lexically_normal()
                            : (baseDir / utf8ToPath(target)).lexically_normal();

                    std::error_code ec;
                    const auto absCandidate = context.repoRoot() / resolvedRel;
                    if (!std::filesystem::exists(absCandidate, ec)) {
                        brokenLinks.push_back(Evidence{
                            file.relativePath,
                            lineNum,
                            static_cast<std::size_t>(it->position(1)) + 1,
                            (*it)[0].str(),
                            "Target '" + pathToGenericUtf8(resolvedRel) + "' does not exist",
                            false});
                    }
                }
            }
        }

        if (mdFilesScanned == 0) {
            return makeNotApplicableResult("No Markdown files found in repository.");
        }
        if (brokenLinks.empty()) {
            return makePassResult("All relative links in " + std::to_string(mdFilesScanned) +
                                  " Markdown file(s) resolved cleanly.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Found " + std::to_string(brokenLinks.size()) + " broken relative link(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(brokenLinks.size()) +
            " broken relative documentation link(s).");
        f.evidence = std::move(brokenLinks);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DOC-007: Missing License File
class MissingLicenseRule final : public IRule {
public:
    std::string id() const override { return "RD-DOC-007"; }
    std::string title() const override { return "Missing License File"; }
    RuleCategory category() const noexcept override { return RuleCategory::Documentation; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Checks for the presence of a top-level LICENSE or COPYING file. Note: RepoDoctor checks only for the presence of a non-empty license file and does not determine legal licensing status or compatibility.";
    }
    std::string remediationGuidance() const override {
        return "Add a standard open-source LICENSE file (e.g., MIT, Apache-2.0, BSD-3-Clause, GPL-3.0) at the repository root after consulting your project's legal and dependency requirements.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Private/internal proprietary repositories may store licensing terms outside the source tree. Filename presence alone does not constitute legal verification.";
    }
    bool isRepositoryWideRule() const noexcept override { return true; }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        static const std::array<std::string_view, 8> kLicenseNames = {
            "LICENSE",      "LICENSE.md",  "LICENSE.txt", "LICENCE",
            "LICENCE.md",   "COPYING",     "COPYING.md",  "COPYING.txt"};

        for (std::string_view candidate : kLicenseNames) {
            const auto found = context.findFileCaseInsensitive(std::string(candidate));
            if (found.has_value() && found->sizeBytes > 16) {
                return makePassResult(
                    "Found license file '" + found->relativePath +
                    "' (filename presence check only; not a legal determination).");
            }
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason = "No top-level LICENSE or COPYING file found.";

        Finding f = makeFinding(
            "No top-level LICENSE or COPYING file found (note: RepoDoctor checks filename presence only, not legal licensing status).");
        f.evidence.push_back(Evidence{
            "LICENSE",
            0,
            0,
            "(file missing)",
            "Checked for LICENSE, LICENSE.md, LICENSE.txt, and COPYING at repository root.",
            false});
        res.findings.push_back(std::move(f));
        return res;
    }
};

}  // namespace

void registerDocumentationRules(RuleRegistry& registry) {
    registry.registerRule(std::make_unique<MissingReadmeRule>());
    registry.registerRule(std::make_unique<EmptyReadmeRule>());
    registry.registerRule(std::make_unique<MissingInstallationDocsRule>());
    registry.registerRule(std::make_unique<MissingUsageDocsRule>());
    registry.registerRule(std::make_unique<MissingContributingRule>());
    registry.registerRule(std::make_unique<BrokenRelativeLinksRule>());
    registry.registerRule(std::make_unique<MissingLicenseRule>());
}

}  // namespace repodoctor
