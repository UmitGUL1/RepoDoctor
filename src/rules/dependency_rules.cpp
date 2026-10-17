#include "repodoctor/rules/builtin_rules.hpp"

namespace repodoctor {

namespace {

// RD-DEP-001: Missing Dependency Lockfile or Baseline
class MissingLockfileRule final : public IRule {
public:
    std::string id() const override { return "RD-DEP-001"; }
    std::string title() const override { return "Missing Dependency Lockfile or Reproducible Baseline"; }
    RuleCategory category() const noexcept override { return RuleCategory::DependencyManagement; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Checks that dependency manifests (package.json, pyproject.toml, requirements.txt, vcpkg.json) are paired with a lockfile or pinned baseline for reproducible builds.";
    }
    std::string remediationGuidance() const override {
        return "Commit your ecosystem lockfile (package-lock.json / yarn.lock / pnpm-lock.yaml for npm, poetry.lock / uv.lock or exact '==' pins for Python, 'builtin-baseline' or vcpkg-configuration.json for vcpkg).";
    }
    std::string falsePositiveConsiderations() const override {
        return "Reusable libraries intended solely for downstream consumption may intentionally omit lockfiles in some ecosystems, though CI reproducibility benefits from locked versions.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto& depReport = context.dependencyReport();
        if (depReport.manifests.empty()) {
            return makeNotApplicableResult(
                "No dependency manifests (vcpkg.json, package.json, requirements.txt, pyproject.toml) found.");
        }

        std::vector<Evidence> missingLocks;
        for (const auto& manifest : depReport.manifests) {
            if (!manifest.dependencies.empty() && !manifest.hasLockfile) {
                missingLocks.push_back(Evidence{
                    manifest.manifestRelativePath,
                    1,
                    1,
                    manifest.manifestRelativePath,
                    toString(manifest.ecosystem) +
                        " manifest declares dependencies without a corresponding lockfile or baseline",
                    false});
            }
        }

        if (missingLocks.empty()) {
            return makePassResult("All dependency manifests have lockfiles or pinned baselines.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(missingLocks.size()) + " manifest(s) without a lockfile.";

        Finding f = makeFinding(
            "Detected " + std::to_string(missingLocks.size()) +
                " dependency manifest(s) lacking a lockfile or baseline.",
            CheckStatus::WARNING);
        f.evidence = std::move(missingLocks);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DEP-002: Unpinned, Wildcard, or Inconsistent Dependency Declarations
class UnpinnedOrInconsistentDependenciesRule final : public IRule {
public:
    std::string id() const override { return "RD-DEP-002"; }
    std::string title() const override { return "Unpinned, Wildcard, or Inconsistent Dependency Declarations"; }
    RuleCategory category() const noexcept override { return RuleCategory::DependencyManagement; }
    Severity defaultSeverity() const noexcept override { return Severity::Medium; }
    std::string description() const override {
        return "Detects wildcard ('*', 'latest') or completely unconstrained dependencies, duplicate declarations across dependencies/devDependencies, or malformed manifest syntax.";
    }
    std::string remediationGuidance() const override {
        return "Replace wildcard ('*' or 'latest') dependency constraints with explicit semantic version ranges or exact versions, and remove duplicate declarations.";
    }
    std::string falsePositiveConsiderations() const override {
        return "Monorepo workspace-internal dependencies using workspace:* protocols should be scoped appropriately.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto& depReport = context.dependencyReport();
        if (depReport.manifests.empty()) {
            return makeNotApplicableResult("No dependency manifests found.");
        }

        std::vector<Evidence> issues;
        bool hasFormatFailure = false;

        for (const auto& manifest : depReport.manifests) {
            if (manifest.isDeprecatedFormat && !manifest.formatWarning.empty()) {
                hasFormatFailure = true;
                issues.push_back(Evidence{
                    manifest.manifestRelativePath,
                    1,
                    1,
                    manifest.manifestRelativePath,
                    manifest.formatWarning,
                    false});
            }
            for (const auto& dep : manifest.dependencies) {
                if (dep.isWildcardOrUnpinned) {
                    issues.push_back(Evidence{
                        dep.manifestPath,
                        dep.lineNumber,
                        1,
                        dep.name + (dep.versionConstraint.empty()
                                        ? " (unconstrained)"
                                        : (": " + dep.versionConstraint)),
                        "Unpinned or wildcard dependency constraint in " +
                            toString(dep.ecosystem),
                        false});
                }
            }
        }

        for (const auto& note : depReport.inconsistencyNotes) {
            issues.push_back(Evidence{
                depReport.manifests.front().manifestRelativePath,
                1,
                1,
                note,
                "Inconsistent dependency declaration",
                false});
        }

        if (issues.empty()) {
            return makePassResult("All declared dependencies use constrained, consistent specifications.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = hasFormatFailure ? CheckStatus::FAIL : CheckStatus::WARNING;
        res.statusReason =
            "Found " + std::to_string(issues.size()) + " dependency constraint/consistency issue(s).";

        Finding f = makeFinding(
            "Detected " + std::to_string(issues.size()) +
                " unpinned, wildcard, or inconsistent dependency declaration(s).",
            res.status);
        f.evidence = std::move(issues);
        res.findings.push_back(std::move(f));
        return res;
    }
};

// RD-DEP-003: Optional Advisory Cache Verification
class DependencyAdvisoryRule final : public IRule {
public:
    std::string id() const override { return "RD-DEP-003"; }
    std::string title() const override { return "Dependency Vulnerability Advisory Verification"; }
    RuleCategory category() const noexcept override { return RuleCategory::DependencyManagement; }
    Severity defaultSeverity() const noexcept override { return Severity::High; }
    std::string description() const override {
        return "Matches declared dependencies against a locally cached, attributable advisory database (.repodoctor-advisories.json). Never claims vulnerabilities without verified advisory data.";
    }
    std::string remediationGuidance() const override {
        return "Upgrade affected packages to the patched version listed in the advisory attribution report.";
    }
    std::string falsePositiveConsiderations() const override {
        return "When no local advisory database is configured or enabled, this check honestly reports NOT_VERIFIED rather than assuming dependencies are vulnerability-free.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        const auto& depReport = context.dependencyReport();
        if (depReport.manifests.empty()) {
            return makeNotApplicableResult("No dependency manifests found.");
        }

        if (!depReport.advisoryDatabaseChecked) {
            return makeNotVerifiedResult(
                "No local vulnerability advisory cache (.repodoctor-advisories.json) was provided or enabled; RepoDoctor does not claim dependencies are vulnerability-free without verified advisory data.");
        }

        if (depReport.verifiedAdvisories.empty()) {
            return makePassResult(
                "Zero known vulnerabilities matched against " +
                depReport.advisorySourceAttribution + ".");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::FAIL;
        res.statusReason =
            "Matched " + std::to_string(depReport.verifiedAdvisories.size()) +
            " advisory record(s) from " + depReport.advisorySourceAttribution + ".";

        Finding f = makeFinding(
            "Matched " + std::to_string(depReport.verifiedAdvisories.size()) +
            " known vulnerability advisory record(s) [Source: " +
            depReport.advisorySourceAttribution + "].");
        for (const auto& adv : depReport.verifiedAdvisories) {
            f.evidence.push_back(Evidence{
                depReport.manifests.front().manifestRelativePath,
                1,
                1,
                adv.packageName + " (" + adv.installedOrDeclaredConstraint + ") -> " + adv.advisoryId,
                adv.summary + (adv.fixedVersion.empty() ? "" : (" | Fixed in: " + adv.fixedVersion)) +
                    " | Source: " + adv.advisorySource,
                false});
        }
        res.findings.push_back(std::move(f));
        return res;
    }
};

}  // namespace

void registerDependencyRules(RuleRegistry& registry) {
    registry.registerRule(std::make_unique<MissingLockfileRule>());
    registry.registerRule(std::make_unique<UnpinnedOrInconsistentDependenciesRule>());
    registry.registerRule(std::make_unique<DependencyAdvisoryRule>());
}

}  // namespace repodoctor
