#include "repodoctor/config/configuration_manager.hpp"
#include "repodoctor/core/types.hpp"
#include "repodoctor/engine/scan_engine.hpp"
#include "repodoctor/fixes/fix_subsystem.hpp"
#include "repodoctor/reports/report_generator.hpp"
#include "repodoctor/rules/rule_registry.hpp"
#include "repodoctor/sources/irepository_source.hpp"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr int kExitOk = 0;
constexpr int kExitFindingsThresholdExceeded = 1;
constexpr int kExitUsageError = 2;
constexpr int kExitSourceError = 3;
constexpr int kExitFixError = 4;

void printHelp(std::ostream& out) {
    out << R"(RepoDoctor CLI v1.0.0 — Scan. Diagnose. Improve.
Professional Open-Source Repository Health Analyzer

USAGE:
  repodoctor scan <path|github-url> [options]
  repodoctor rules [--format console|json]
  repodoctor explain <RULE_ID>
  repodoctor fix <path> [--dry-run | --apply --yes] [--fix-id <FIX_ID>]
  repodoctor --help | --version

COMMANDS:
  scan <target>        Analyze a local Git repository or public GitHub URL
  rules                List all registered diagnostic rules and metadata
  explain <RULE_ID>    Show detailed documentation, remediation, and FP notes for a rule
  fix <path>           Plan, preview unified diffs, and optionally apply safe auto-fixes

SCAN OPTIONS:
  --format <fmt>       Output format: console (default), json, sarif, html
  --output, -o <file>  Write report output to the specified file path
  --config <file>      Load scan configuration from a specific JSON file
  --fail-on <sev>      Exit with code 1 if any finding is >= severity
                       (INFO, LOW, MEDIUM, HIGH, CRITICAL, NONE; default: HIGH)
  --disable-rule <id>  Disable a specific rule ID for this scan run

FIX OPTIONS:
  --dry-run            Preview planned changes and unified diffs without modifying files (default)
  --apply              Apply selected fixes after previewing unified diffs
  --yes, -y            Confirm applying non-conflicting fixes in non-interactive mode
  --fix-id <id>        Select a specific fix ID to preview/apply (may be repeated)

EXIT CODES:
  0  Success (no findings above threshold)
  1  Findings detected at or above the configured --fail-on severity threshold
  2  Command-line usage or argument validation error
  3  Repository source or I/O error
  4  Auto-fix conflict or execution error

EXAMPLES:
  repodoctor scan .
  repodoctor scan ./my-project
  repodoctor scan . --format json
  repodoctor scan . --format sarif
  repodoctor scan . --format html --output report.html
  repodoctor rules
  repodoctor explain RD-SEC-001
  repodoctor fix . --dry-run
)";
}

bool isGitHubUrlTarget(const std::string& target) {
    return target.rfind("https://github.com/", 0) == 0 ||
           target.rfind("http://github.com/", 0) == 0;
}

int handleRulesCommand(const std::vector<std::string>& args) {
    std::string format = "console";
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--format" && i + 1 < args.size()) {
            format = args[++i];
        } else if (args[i] == "--help" || args[i] == "-h") {
            printHelp(std::cout);
            return kExitOk;
        } else {
            std::cerr << "Unknown option for 'rules': " << args[i] << "\n";
            return kExitUsageError;
        }
    }

    const auto registry = repodoctor::RuleRegistry::createDefaultRegistry();
    if (format == "json") {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& rule : registry.rules()) {
            arr.push_back({
                {"id", rule->id()},
                {"title", rule->title()},
                {"category", repodoctor::toString(rule->category())},
                {"default_severity", repodoctor::toString(rule->defaultSeverity())},
                {"has_safe_fix", rule->hasSafeFix()},
                {"safe_fix_id", rule->safeFixId()},
                {"description", rule->description()}
            });
        }
        std::cout << arr.dump(2) << "\n";
        return kExitOk;
    }

    std::cout << "Registered RepoDoctor Rules (" << registry.rules().size() << "):\n";
    std::cout << "----------------------------------------------------------------------------------------\n";
    std::cout << std::left << std::setw(14) << "RULE ID"
              << std::setw(12) << "SEVERITY"
              << std::setw(24) << "CATEGORY"
              << "TITLE\n";
    std::cout << "----------------------------------------------------------------------------------------\n";
    for (const auto& rule : registry.rules()) {
        std::cout << std::left << std::setw(14) << rule->id()
                  << std::setw(12) << repodoctor::toString(rule->defaultSeverity())
                  << std::setw(24) << repodoctor::toDisplayLabel(rule->category())
                  << rule->title()
                  << (rule->hasSafeFix() ? " [Fixable]" : "") << "\n";
    }
    return kExitOk;
}

int handleExplainCommand(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: repodoctor explain <RULE_ID>\n";
        return kExitUsageError;
    }
    const std::string ruleId = args[1];
    const auto registry = repodoctor::RuleRegistry::createDefaultRegistry();
    const repodoctor::IRule* rule = registry.findById(ruleId);
    if (rule == nullptr) {
        std::cerr << "Unknown rule ID: '" << ruleId << "'. Run 'repodoctor rules' to list valid IDs.\n";
        return kExitUsageError;
    }

    std::cout << "============================================================================\n";
    std::cout << " Rule ID      : " << rule->id() << "\n";
    std::cout << " Title        : " << rule->title() << "\n";
    std::cout << " Category     : " << repodoctor::toDisplayLabel(rule->category()) << "\n";
    std::cout << " Severity     : " << repodoctor::toString(rule->defaultSeverity()) << "\n";
    std::cout << " Safe AutoFix : "
              << (rule->hasSafeFix() ? ("Yes (" + rule->safeFixId() + ")") : "No") << "\n";
    std::cout << "----------------------------------------------------------------------------\n";
    std::cout << " Description:\n   " << rule->description() << "\n\n";
    std::cout << " Remediation Guidance:\n   " << rule->remediationGuidance() << "\n\n";
    std::cout << " False-Positive Considerations:\n   " << rule->falsePositiveConsiderations() << "\n";
    std::cout << "============================================================================\n";
    return kExitOk;
}

int handleScanCommand(const std::vector<std::string>& args) {
    std::string target = ".";
    bool targetProvided = false;
    repodoctor::ReportFormat format = repodoctor::ReportFormat::Console;
    std::string outputPath;
    std::string configPath;
    std::string failOnStr;
    std::vector<std::string> cliDisabledRules;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            printHelp(std::cout);
            return kExitOk;
        } else if (arg == "--format") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --format\n";
                return kExitUsageError;
            }
            const auto parsedFmt = repodoctor::parseReportFormat(args[++i]);
            if (!parsedFmt.has_value()) {
                std::cerr << "Unsupported report format: '" << args[i]
                          << "'. Expected console, json, sarif, or html.\n";
                return kExitUsageError;
            }
            format = *parsedFmt;
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for " << arg << "\n";
                return kExitUsageError;
            }
            outputPath = args[++i];
        } else if (arg == "--config") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --config\n";
                return kExitUsageError;
            }
            configPath = args[++i];
        } else if (arg == "--fail-on") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --fail-on\n";
                return kExitUsageError;
            }
            failOnStr = args[++i];
        } else if (arg == "--disable-rule") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --disable-rule\n";
                return kExitUsageError;
            }
            cliDisabledRules.push_back(args[++i]);
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "Unknown option for 'scan': " << arg << "\n";
            return kExitUsageError;
        } else if (!targetProvided) {
            target = arg;
            targetProvided = true;
        } else {
            std::cerr << "Unexpected positional argument: " << arg << "\n";
            return kExitUsageError;
        }
    }

    repodoctor::ConfigurationManager configMgr;
    std::vector<std::string> configWarnings;
    std::optional<repodoctor::ScanConfiguration> configOverride;

    if (!configPath.empty()) {
        configOverride = configMgr.loadFromFile(repodoctor::utf8ToPath(configPath), &configWarnings);
    } else if (!isGitHubUrlTarget(target)) {
        const std::filesystem::path repoConfig =
            repodoctor::utf8ToPath(target) / ".repodoctor.json";
        std::error_code ec;
        if (std::filesystem::exists(repoConfig, ec)) {
            configOverride = configMgr.loadFromFile(repoConfig, &configWarnings);
        }
    }

    if (!failOnStr.empty() || !cliDisabledRules.empty()) {
        if (!configOverride.has_value()) {
            configOverride = repodoctor::ScanConfiguration{};
        }
        for (const auto& rId : cliDisabledRules) {
            configOverride->disabledRules[rId] =
                repodoctor::DisabledRuleConfig{rId, "Disabled via --disable-rule CLI flag"};
        }
    }

    bool disableThresholdExit = false;
    if (!failOnStr.empty()) {
        if (failOnStr == "NONE" || failOnStr == "none" || failOnStr == "off") {
            disableThresholdExit = true;
        } else {
            const auto parsedSev = repodoctor::parseSeverity(failOnStr);
            if (!parsedSev.has_value()) {
                std::cerr << "Invalid --fail-on severity: '" << failOnStr << "'\n";
                return kExitUsageError;
            }
            configOverride->failOnSeverity = *parsedSev;
        }
    }

    std::unique_ptr<repodoctor::IRepositorySource> source;
    if (isGitHubUrlTarget(target)) {
        source = std::make_unique<repodoctor::GitHubRepositorySource>(target);
    } else {
        source = std::make_unique<repodoctor::LocalRepositorySource>(repodoctor::utf8ToPath(target));
    }

    repodoctor::ScanEngine engine;
    repodoctor::ScanResult scanResult = engine.scan(*source, configOverride);
    for (const auto& w : configWarnings) {
        scanResult.configurationWarnings.push_back(w);
    }

    if (!scanResult.scanCompleted) {
        std::cerr << "[ERROR] " << scanResult.errorMessage << "\n";
        return kExitSourceError;
    }

    repodoctor::ReportGenerator reportGen;
    if (!outputPath.empty()) {
        std::string writeError;
        if (!reportGen.writeToFile(
                scanResult, format, repodoctor::utf8ToPath(outputPath), &writeError)) {
            std::cerr << "[ERROR] " << writeError << "\n";
            return kExitSourceError;
        }
        std::cout << "Report written to " << outputPath << " ("
                  << repodoctor::toString(format) << " format).\n";
    } else {
        std::cout << reportGen.generate(scanResult, format);
        if (format != repodoctor::ReportFormat::Console) {
            std::cout << "\n";
        }
    }

    if (!disableThresholdExit) {
        const repodoctor::Severity threshold =
            configOverride.has_value() ? configOverride->failOnSeverity
                                       : repodoctor::Severity::High;
        for (const auto& finding : scanResult.findings) {
            if (static_cast<int>(finding.severity) >= static_cast<int>(threshold)) {
                return kExitFindingsThresholdExceeded;
            }
        }
    }

    return kExitOk;
}

int handleFixCommand(const std::vector<std::string>& args) {
    std::string target = ".";
    bool targetProvided = false;
    bool dryRun = true;
    bool confirmedYes = false;
    std::set<std::string> selectedFixIds;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            printHelp(std::cout);
            return kExitOk;
        } else if (arg == "--dry-run") {
            dryRun = true;
        } else if (arg == "--apply") {
            dryRun = false;
        } else if (arg == "--yes" || arg == "-y") {
            confirmedYes = true;
        } else if (arg == "--fix-id") {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for --fix-id\n";
                return kExitUsageError;
            }
            selectedFixIds.insert(args[++i]);
        } else if (!arg.empty() && arg.front() == '-') {
            std::cerr << "Unknown option for 'fix': " << arg << "\n";
            return kExitUsageError;
        } else if (!targetProvided) {
            target = arg;
            targetProvided = true;
        } else {
            std::cerr << "Unexpected positional argument: " << arg << "\n";
            return kExitUsageError;
        }
    }

    repodoctor::LocalRepositorySource source(repodoctor::utf8ToPath(target));
    repodoctor::ScanEngine engine;
    const repodoctor::ScanResult scanResult = engine.scan(source);
    if (!scanResult.scanCompleted) {
        std::cerr << "[ERROR] " << scanResult.errorMessage << "\n";
        return kExitSourceError;
    }

    repodoctor::FixPlanner planner;
    const repodoctor::FixPlan plan =
        planner.createPlan(scanResult, repodoctor::utf8ToPath(scanResult.repositoryPath));

    if (plan.fixes.empty()) {
        std::cout << "No safe auto-fixes are needed for " << scanResult.repositoryPath << ".\n";
        return kExitOk;
    }

    std::cout << "============================================================================\n";
    std::cout << " RepoDoctor Safe Auto-Fix Planner ("
              << (dryRun ? "DRY-RUN PREVIEW" : "APPLY MODE") << ")\n";
    std::cout << "============================================================================\n\n";

    for (const auto& fix : plan.fixes) {
        if (!selectedFixIds.empty() && selectedFixIds.count(fix.fixId) == 0) {
            continue;
        }
        std::cout << "[" << fix.fixId << "] (" << fix.ruleId << ") " << fix.title << "\n";
        std::cout << "Target    : " << fix.targetRelativePath << "\n";
        std::cout << "Rationale : " << fix.rationale << "\n";
        if (fix.hasConflict) {
            std::cout << "CONFLICT  : " << fix.conflictReason << "\n";
        }
        std::cout << "Unified Diff Preview:\n";
        std::cout << "----------------------------------------------------------------------------\n";
        std::cout << fix.unifiedDiff;
        std::cout << "----------------------------------------------------------------------------\n\n";
    }

    if (!dryRun && !confirmedYes) {
        std::cout << "Confirm applying the planned fixes above? Type 'yes' to proceed: ";
        std::string answer;
        std::getline(std::cin, answer);
        if (answer != "yes" && answer != "y" && answer != "Y") {
            std::cout << "Aborted by user. No files were modified.\n";
            return kExitOk;
        }
    }

    if (selectedFixIds.empty()) {
        for (const auto& fix : plan.fixes) selectedFixIds.insert(fix.fixId);
    }
    repodoctor::FixExecutor executor;
    const auto execReport = executor.execute(plan, selectedFixIds, dryRun, true);
    for (const auto& item : execReport.results) {
        if (item.skipped) {
            continue;
        }
        std::cout << " -> [" << item.fixId << "] " << item.message;
        if (!item.backupRelativePath.empty()) {
            std::cout << " (Backup: " << item.backupRelativePath << ")";
        }
        std::cout << "\n";
    }

    return execReport.allSucceeded ? kExitOk : kExitFixError;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv != nullptr) {
        args.reserve(static_cast<std::size_t>(wargc));
        for (int i = 1; i < wargc; ++i) {
            const int len = WideCharToMultiByte(
                CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                std::string utf8Arg(static_cast<std::size_t>(len), '\0');
                WideCharToMultiByte(
                    CP_UTF8, 0, wargv[i], -1, utf8Arg.data(), len, nullptr, nullptr);
                utf8Arg.pop_back();
                args.push_back(std::move(utf8Arg));
            } else {
                args.emplace_back();
            }
        }
        LocalFree(wargv);
    } else {
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
        }
    }
#else
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
#endif

    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        printHelp(std::cout);
        return kExitOk;
    }

    if (args[0] == "--version" || args[0] == "-v" || args[0] == "version") {
        std::cout << "repodoctor-cli version 1.0.0\n";
        return kExitOk;
    }

    const std::string& subcommand = args[0];
    if (subcommand == "scan") {
        return handleScanCommand(args);
    }
    if (subcommand == "rules") {
        return handleRulesCommand(args);
    }
    if (subcommand == "explain") {
        return handleExplainCommand(args);
    }
    if (subcommand == "fix") {
        return handleFixCommand(args);
    }

    std::cerr << "Unknown command: '" << subcommand
              << "'. Run 'repodoctor --help' for usage instructions.\n";
    return kExitUsageError;
}
