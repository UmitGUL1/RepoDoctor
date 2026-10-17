#include "repodoctor/reports/report_generator.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace repodoctor {

namespace {

std::string htmlEscape(std::string_view sv) {
    std::string out;
    out.reserve(sv.size());
    for (char c : sv) {
        switch (c) {
            case '&':
                out.append("&amp;");
                break;
            case '<':
                out.append("&lt;");
                break;
            case '>':
                out.append("&gt;");
                break;
            case '"':
                out.append("&quot;");
                break;
            case '\'':
                out.append("&#39;");
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string sarifLevelForSeverity(Severity sev) {
    switch (sev) {
        case Severity::Critical:
        case Severity::High:
            return "error";
        case Severity::Medium:
        case Severity::Low:
            return "warning";
        case Severity::Info:
            return "note";
    }
    return "warning";
}

}  // namespace

std::optional<ReportFormat> parseReportFormat(std::string_view formatStr) {
    std::string lower(formatStr);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lower == "console" || lower == "text" || lower == "table") {
        return ReportFormat::Console;
    }
    if (lower == "json") {
        return ReportFormat::Json;
    }
    if (lower == "sarif") {
        return ReportFormat::Sarif;
    }
    if (lower == "html") {
        return ReportFormat::Html;
    }
    return std::nullopt;
}

std::string toString(ReportFormat format) {
    switch (format) {
        case ReportFormat::Console:
            return "console";
        case ReportFormat::Json:
            return "json";
        case ReportFormat::Sarif:
            return "sarif";
        case ReportFormat::Html:
            return "html";
    }
    return "console";
}

std::string ReportGenerator::generate(
    const ScanResult& result,
    ReportFormat format) const {
    switch (format) {
        case ReportFormat::Console:
            return generateConsoleReport(result);
        case ReportFormat::Json:
            return generateJsonReport(result);
        case ReportFormat::Sarif:
            return generateSarifReport(result);
        case ReportFormat::Html:
            return generateHtmlReport(result);
    }
    return generateConsoleReport(result);
}

std::string ReportGenerator::generateConsoleReport(const ScanResult& result) const {
    std::ostringstream out;
    out << "============================================================================\n";
    out << " RepoDoctor — Open-Source Repository Health Analyzer\n";
    out << " Tagline: Scan. Diagnose. Improve.\n";
    out << "============================================================================\n";

    if (!result.scanCompleted) {
        out << " [ERROR] Scan did not complete: " << result.errorMessage << "\n";
        out << "============================================================================\n";
        return out.str();
    }

    out << " Repository : " << result.repositoryName << " (" << result.repositoryPath << ")\n";
    out << " Timestamp  : " << result.scanTimestampIso8601 << "\n";
    out << std::fixed << std::setprecision(1);
    out << " Duration   : " << result.statistics.durationMilliseconds << " ms | "
        << "Files Discovered: " << result.statistics.totalFilesDiscovered
        << " (Text: " << result.statistics.textFilesAnalyzed
        << ", Binary: " << result.statistics.binaryFilesDetected << ")\n";
    out << "----------------------------------------------------------------------------\n";
    out << " OVERALL REPOSITORY HEALTH SCORE : " << result.healthScore.overallScore
        << " / 100  [Grade: " << result.healthScore.gradeLabel << "]\n";
    out << " Checks Summary : "
        << "PASS=" << result.healthScore.totalPassedChecks << " | "
        << "WARN=" << result.healthScore.totalWarningChecks << " | "
        << "FAIL=" << result.healthScore.totalFailedChecks << " | "
        << "N/A=" << result.healthScore.totalNotApplicableChecks << " | "
        << "UNVERIFIED=" << result.healthScore.totalNotVerifiedChecks << "\n";
    out << "----------------------------------------------------------------------------\n";
    out << " CATEGORY BREAKDOWN:\n";

    for (const auto& [cat, cs] : result.healthScore.categoryScores) {
        out << "  - " << std::left << std::setw(24) << toDisplayLabel(cat) << ": ";
        if (cs.status == CheckStatus::NOT_APPLICABLE || cs.status == CheckStatus::NOT_VERIFIED) {
            out << std::setw(14) << toString(cs.status)
                << " (Effective Weight: 0.0%)\n";
        } else {
            out << std::right << std::setw(5) << cs.score << " / 100  ["
                << std::left << std::setw(7) << toString(cs.status)
                << "] (Weight: " << std::right << std::setw(4) << cs.effectiveNormalizedWeight << "%)\n";
        }
    }

    if (!result.configurationWarnings.empty()) {
        out << "----------------------------------------------------------------------------\n";
        out << " CONFIGURATION WARNINGS:\n";
        for (const auto& w : result.configurationWarnings) {
            out << "  [!] " << w << "\n";
        }
    }

    out << "----------------------------------------------------------------------------\n";
    if (result.findings.empty()) {
        out << " FINDINGS: None! All applicable checks passed.\n";
    } else {
        out << " FINDINGS (" << result.findings.size() << "):\n\n";
        for (const auto& f : result.findings) {
            out << "  [" << toString(f.severity) << "] [" << f.ruleId << "] " << f.title << "\n";
            out << "    Category    : " << toDisplayLabel(f.category)
                << " | Status: " << toString(f.status)
                << (f.fixable ? " | [Auto-Fix Available]" : "") << "\n";
            out << "    Summary     : " << f.summary << "\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(f.evidence.size(), 5); ++i) {
                const auto& ev = f.evidence[i];
                out << "    Evidence    : " << ev.relativeFilePath;
                if (ev.lineNumber > 0) {
                    out << ":" << ev.lineNumber;
                }
                out << " -> " << ev.snippet;
                if (!ev.contextNote.empty()) {
                    out << " (" << ev.contextNote << ")";
                }
                out << "\n";
            }
            if (f.evidence.size() > 5) {
                out << "    Evidence    : ... and " << (f.evidence.size() - 5) << " more item(s)\n";
            }
            out << "    Remediation : " << f.remediationGuidance << "\n\n";
        }
    }

    out << "----------------------------------------------------------------------------\n";
    out << " Note: " << result.healthScore.methodologyNote << "\n";
    out << "============================================================================\n";
    return out.str();
}

std::string ReportGenerator::generateJsonReport(const ScanResult& result) const {
    nlohmann::json root = nlohmann::json::object();
    root["tool"] = {
        {"name", "RepoDoctor"},
        {"version", "1.0.0"},
        {"tagline", "Scan. Diagnose. Improve."}
    };
    root["scan_completed"] = result.scanCompleted;
    root["error_message"] = result.errorMessage;
    root["repository"] = {
        {"name", result.repositoryName},
        {"path", result.repositoryPath},
        {"source", result.sourceDescription},
        {"timestamp_utc", result.scanTimestampIso8601}
    };
    root["statistics"] = {
        {"total_files_discovered", result.statistics.totalFilesDiscovered},
        {"text_files_analyzed", result.statistics.textFilesAnalyzed},
        {"binary_files_detected", result.statistics.binaryFilesDetected},
        {"skipped_by_exclusion", result.statistics.skippedByExclusion},
        {"skipped_by_size_limit", result.statistics.skippedBySizeLimit},
        {"symlinks_skipped", result.statistics.symlinksSkipped},
        {"total_bytes_read", result.statistics.totalBytesRead},
        {"duration_ms", result.statistics.durationMilliseconds},
        {"was_cancelled", result.statistics.wasCancelled},
          {"file_limit_reached", result.statistics.fileLimitReached}
    };

    nlohmann::json categoriesJson = nlohmann::json::array();
    for (const auto& [cat, cs] : result.healthScore.categoryScores) {
        nlohmann::json dedArr = nlohmann::json::array();
        for (const auto& d : cs.deductions) {
            dedArr.push_back({
                {"rule_id", d.ruleId},
                {"rule_title", d.ruleTitle},
                {"severity", toString(d.severity)},
                {"points_deducted", d.pointsDeducted},
                {"explanation", d.explanation}
            });
        }
        categoriesJson.push_back({
            {"category", toString(cat)},
            {"display_label", toDisplayLabel(cat)},
            {"score", cs.score},
            {"status", toString(cs.status)},
            {"configured_weight", cs.configuredWeight},
            {"effective_normalized_weight", cs.effectiveNormalizedWeight},
            {"passed_checks", cs.passedChecks},
            {"failed_checks", cs.failedChecks},
            {"warning_checks", cs.warningChecks},
            {"not_applicable_checks", cs.notApplicableChecks},
            {"not_verified_checks", cs.notVerifiedChecks},
            {"deductions", dedArr}
        });
    }

    root["health_score"] = {
        {"overall_score", result.healthScore.overallScore},
        {"grade", result.healthScore.gradeLabel},
        {"passed_checks", result.healthScore.totalPassedChecks},
        {"failed_checks", result.healthScore.totalFailedChecks},
        {"warning_checks", result.healthScore.totalWarningChecks},
        {"not_applicable_checks", result.healthScore.totalNotApplicableChecks},
        {"not_verified_checks", result.healthScore.totalNotVerifiedChecks},
        {"methodology_note", result.healthScore.methodologyNote},
        {"categories", categoriesJson}
    };

    nlohmann::json evalsJson = nlohmann::json::array();
    for (const auto& ev : result.ruleEvaluations) {
        evalsJson.push_back({
            {"rule_id", ev.ruleId},
            {"title", ev.ruleTitle},
            {"category", toString(ev.category)},
            {"status", toString(ev.status)},
            {"reason", ev.statusReason}
        });
    }
    root["rule_evaluations"] = evalsJson;

    nlohmann::json findingsJson = nlohmann::json::array();
    for (const auto& f : result.findings) {
        nlohmann::json evJson = nlohmann::json::array();
        for (const auto& e : f.evidence) {
            evJson.push_back({
                {"file", e.relativeFilePath},
                {"line", e.lineNumber},
                {"column", e.columnNumber},
                {"snippet", e.snippet},
                {"context", e.contextNote},
                {"is_redacted", e.isRedacted}
            });
        }
        findingsJson.push_back({
            {"rule_id", f.ruleId},
            {"title", f.title},
            {"category", toString(f.category)},
            {"severity", toString(f.severity)},
            {"status", toString(f.status)},
            {"summary", f.summary},
            {"description", f.description},
            {"remediation", f.remediationGuidance},
            {"false_positive_notes", f.falsePositiveNotes},
            {"fixable", f.fixable},
            {"fix_id", f.fixId},
            {"evidence", evJson}
        });
    }
    root["findings"] = findingsJson;
    root["configuration_warnings"] = result.configurationWarnings;

    return root.dump(2);
}

std::string ReportGenerator::generateSarifReport(const ScanResult& result) const {
    nlohmann::json sarif = nlohmann::json::object();
    sarif["$schema"] = "https://json.schemastore.org/sarif-2.1.0.json";
    sarif["version"] = "2.1.0";

    nlohmann::json rulesArray = nlohmann::json::array();
    for (const auto& ev : result.ruleEvaluations) {
        rulesArray.push_back({
            {"id", ev.ruleId},
            {"name", ev.ruleTitle},
            {"shortDescription", {{"text", ev.ruleTitle}}},
            {"defaultConfiguration", {{"level", sarifLevelForSeverity(ev.defaultSeverity)}}},
            {"properties", {{"category", toString(ev.category)}}}
        });
    }

    nlohmann::json resultsArray = nlohmann::json::array();
    for (const auto& f : result.findings) {
        nlohmann::json locations = nlohmann::json::array();
        for (const auto& e : f.evidence) {
            nlohmann::json physLoc = {
                {"artifactLocation",
                 {{"uri", e.relativeFilePath.empty() ? "." : e.relativeFilePath},
                  {"uriBaseId", "%SRCROOT%"}}}
            };
            if (e.lineNumber > 0) {
                physLoc["region"] = {
                    {"startLine", e.lineNumber},
                    {"startColumn", e.columnNumber > 0 ? e.columnNumber : 1},
                    {"snippet", {{"text", e.snippet}}}
                };
            }
            locations.push_back({{"physicalLocation", physLoc}});
        }

        if (locations.empty()) {
            locations.push_back({
                {"physicalLocation",
                 {{"artifactLocation", {{"uri", "."}, {"uriBaseId", "%SRCROOT%"}}}}}
            });
        }

        resultsArray.push_back({
            {"ruleId", f.ruleId},
            {"level", sarifLevelForSeverity(f.severity)},
            {"message", {{"text", f.summary + " Remediation: " + f.remediationGuidance}}},
            {"locations", locations},
            {"properties",
             {{"category", toString(f.category)},
              {"severity", toString(f.severity)},
              {"fixable", f.fixable}}}
        });
    }

    nlohmann::json runObj = {
        {"tool",
         {{"driver",
           {{"name", "RepoDoctor"},
            {"version", "1.0.0"},
            {"informationUri", "https://github.com/repodoctor/repodoctor"},
            {"rules", rulesArray}}}}},
        {"results", resultsArray}
    };

    sarif["runs"] = nlohmann::json::array({runObj});
    return sarif.dump(2);
}

std::string ReportGenerator::generateHtmlReport(const ScanResult& result) const {
    std::ostringstream html;
    html << std::fixed << std::setprecision(1);
    html << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RepoDoctor Health Report — )" << htmlEscape(result.repositoryName) << R"(</title>
<style>
  :root {
    --bg: #0b111e;
    --panel: #131c2e;
    --border: #1e293b;
    --text: #e2e8f0;
    --muted: #94a3b8;
    --accent: #06b6d4;
    --pass: #10b981;
    --warn: #f59e0b;
    --fail: #ef4444;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    padding: 32px;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg);
    color: var(--text);
    line-height: 1.5;
  }
  .container { max-width: 1120px; margin: 0 auto; }
  .header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 24px;
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    margin-bottom: 24px;
  }
  .score-box {
    text-align: right;
  }
  .score-num {
    font-size: 42px;
    font-weight: 800;
    color: var(--accent);
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 16px;
    margin-bottom: 24px;
  }
  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 18px;
  }
  .badge {
    display: inline-block;
    padding: 2px 10px;
    border-radius: 999px;
    font-size: 12px;
    font-weight: 700;
  }
  .badge-CRITICAL, .badge-HIGH, .badge-FAIL { background: rgba(239,68,68,0.2); color: #fca5a5; }
  .badge-MEDIUM, .badge-WARNING { background: rgba(245,158,11,0.2); color: #fcd34d; }
  .badge-LOW, .badge-INFO { background: rgba(6,182,212,0.2); color: #67e8f9; }
  .badge-PASS { background: rgba(16,185,129,0.2); color: #6ee7b7; }
  .finding {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 20px;
    margin-bottom: 16px;
  }
  code, pre {
    font-family: "JetBrains Mono", Consolas, monospace;
    background: #070b14;
    padding: 4px 8px;
    border-radius: 6px;
    font-size: 13px;
  }
</style>
</head>
<body>
<div class="container">
  <div class="header">
    <div>
      <h1 style="margin:0 0 6px 0;">RepoDoctor Health Report</h1>
      <div style="color:var(--muted);">Repository: <strong>)"
         << htmlEscape(result.repositoryName) << R"(</strong> ()"
         << htmlEscape(result.repositoryPath) << R"()</div>
      <div style="color:var(--muted);font-size:13px;">Scanned at )"
         << htmlEscape(result.scanTimestampIso8601) << " in "
         << result.statistics.durationMilliseconds << " ms | Files analyzed: "
         << result.statistics.textFilesAnalyzed << R"(</div>
    </div>
    <div class="score-box">
      <div class="score-num">)" << result.healthScore.overallScore << R"( <span style="font-size:20px;color:var(--muted);">/ 100</span></div>
      <div>Grade: <strong>)" << htmlEscape(result.healthScore.gradeLabel) << R"(</strong></div>
    </div>
  </div>
  <h2>Category Breakdown</h2>
  <div class="grid">
)";

    for (const auto& [cat, cs] : result.healthScore.categoryScores) {
        html << "    <div class=\"card\">\n"
             << "      <div style=\"display:flex;justify-content:space-between;align-items:center;\">\n"
             << "        <strong>" << htmlEscape(toDisplayLabel(cat)) << "</strong>\n"
             << "        <span class=\"badge badge-" << htmlEscape(toString(cs.status)) << "\">"
             << htmlEscape(toString(cs.status)) << "</span>\n"
             << "      </div>\n"
             << "      <div style=\"font-size:26px;font-weight:700;margin:10px 0;\">"
             << cs.score << " / 100</div>\n"
             << "      <div style=\"color:var(--muted);font-size:13px;\">Pass: "
             << cs.passedChecks << " | Warn: " << cs.warningChecks
             << " | Fail: " << cs.failedChecks
             << " | Unverified: " << cs.notVerifiedChecks << "</div>\n"
             << "    </div>\n";
    }

    html << "  </div>\n  <h2>Actionable Findings (" << result.findings.size() << ")</h2>\n";
    if (result.findings.empty()) {
        html << "  <div class=\"card\">All applicable checks passed with zero findings.</div>\n";
    } else {
        for (const auto& f : result.findings) {
            html << "  <div class=\"finding\">\n"
                 << "    <div style=\"display:flex;gap:10px;align-items:center;margin-bottom:8px;\">\n"
                 << "      <span class=\"badge badge-" << htmlEscape(toString(f.severity)) << "\">"
                 << htmlEscape(toString(f.severity)) << "</span>\n"
                 << "      <code>" << htmlEscape(f.ruleId) << "</code>\n"
                 << "      <strong>" << htmlEscape(f.title) << "</strong>\n"
                 << "    </div>\n"
                 << "    <p style=\"margin:6px 0;\">" << htmlEscape(f.summary) << "</p>\n";
            if (!f.evidence.empty()) {
                html << "    <div style=\"margin:10px 0;\">\n";
                for (std::size_t i = 0; i < std::min<std::size_t>(f.evidence.size(), 5); ++i) {
                    const auto& e = f.evidence[i];
                    html << "      <div><code>" << htmlEscape(e.relativeFilePath);
                    if (e.lineNumber > 0) {
                        html << ":" << e.lineNumber;
                    }
                    html << "</code> &rarr; <code>" << htmlEscape(e.snippet) << "</code></div>\n";
                }
                html << "    </div>\n";
            }
            html << "    <div style=\"color:var(--muted);font-size:14px;\"><strong>Remediation:</strong> "
                 << htmlEscape(f.remediationGuidance) << "</div>\n"
                 << "  </div>\n";
        }
    }

    html << "  <p style=\"color:var(--muted);font-size:12px;margin-top:32px;\">"
         << htmlEscape(result.healthScore.methodologyNote) << "</p>\n"
         << "</div>\n</body>\n</html>\n";
    return html.str();
}

bool ReportGenerator::writeToFile(
    const ScanResult& result,
    ReportFormat format,
    const std::filesystem::path& outputPath,
    std::string* errorOut) const {
    std::error_code ec;
    const auto parent = outputPath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        if (errorOut != nullptr) {
            *errorOut = "Failed to open output report path for writing: " + pathToUtf8(outputPath);
        }
        return false;
    }

    const std::string content = generate(result, format);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return out.good();
}

}  // namespace repodoctor
