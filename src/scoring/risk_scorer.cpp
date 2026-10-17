#include "repodoctor/scoring/risk_scorer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace repodoctor {

double RiskScorer::baseDeductionForSeverity(
    Severity severity,
    CheckStatus status) noexcept {
    if (status != CheckStatus::FAIL && status != CheckStatus::WARNING) {
        return 0.0;
    }

    double base = 0.0;
    switch (severity) {
        case Severity::Info:
            base = 2.0;
            break;
        case Severity::Low:
            base = 8.0;
            break;
        case Severity::Medium:
            base = 18.0;
            break;
        case Severity::High:
            base = 30.0;
            break;
        case Severity::Critical:
            base = 45.0;
            break;
    }

    if (status == CheckStatus::WARNING) {
        base *= 0.6;
    }
    return base;
}

std::string RiskScorer::computeGradeLabel(double score) {
    if (score >= 90.0) {
        return "A";
    }
    if (score >= 80.0) {
        return "B";
    }
    if (score >= 70.0) {
        return "C";
    }
    if (score >= 55.0) {
        return "D";
    }
    return "F";
}

HealthScoreReport RiskScorer::computeScore(
    const std::vector<RuleEvaluationResult>& evaluations,
    const ScanConfiguration& config) const {
    HealthScoreReport report{};
    report.methodologyNote =
        "RepoDoctor Health Score (0-100) is a weighted engineering heuristic across six categories. "
        "NOT_APPLICABLE categories are excluded and weights re-normalized. "
        "NOT_VERIFIED checks are tracked explicitly and never counted as PASS. "
        "This score is a diagnostic aid, not a legal or security certification.";

    static const std::array<RuleCategory, 6> kAllCategories = {
        RuleCategory::Documentation,
        RuleCategory::SecurityHygiene,
        RuleCategory::BuildAndCI,
        RuleCategory::Testing,
        RuleCategory::RepositoryHygiene,
        RuleCategory::DependencyManagement
    };

    for (RuleCategory cat : kAllCategories) {
        CategoryScore cs{};
        cs.category = cat;
        const auto wIt = config.categoryWeights.find(cat);
        cs.configuredWeight = (wIt != config.categoryWeights.end()) ? wIt->second : 15.0;
        cs.score = 100.0;
        cs.status = CheckStatus::NOT_APPLICABLE;
        report.categoryScores[cat] = cs;
    }

    for (const auto& eval : evaluations) {
        auto& cs = report.categoryScores[eval.category];
        switch (eval.status) {
            case CheckStatus::PASS:
                cs.passedChecks++;
                report.totalPassedChecks++;
                break;
            case CheckStatus::FAIL:
                cs.failedChecks++;
                report.totalFailedChecks++;
                break;
            case CheckStatus::WARNING:
                cs.warningChecks++;
                report.totalWarningChecks++;
                break;
            case CheckStatus::NOT_APPLICABLE:
                cs.notApplicableChecks++;
                report.totalNotApplicableChecks++;
                break;
            case CheckStatus::NOT_VERIFIED:
                cs.notVerifiedChecks++;
                report.totalNotVerifiedChecks++;
                break;
        }

        if (eval.status == CheckStatus::FAIL || eval.status == CheckStatus::WARNING) {
            for (const auto& finding : eval.findings) {
                const double points = baseDeductionForSeverity(finding.severity, finding.status);
                if (points > 0.0) {
                    ScoreDeduction ded{};
                    ded.ruleId = finding.ruleId;
                    ded.ruleTitle = finding.title;
                    ded.category = finding.category;
                    ded.severity = finding.severity;
                    ded.pointsDeducted = points;
                    ded.explanation = finding.summary;
                    cs.deductions.push_back(ded);
                    report.allDeductions.push_back(std::move(ded));
                }
            }
        }
    }

    double totalApplicableWeight = 0.0;
    for (auto& [cat, cs] : report.categoryScores) {
        const std::size_t activeChecks =
            cs.passedChecks + cs.failedChecks + cs.warningChecks;
        if (activeChecks == 0) {
            if (cs.notVerifiedChecks > 0) {
                cs.status = CheckStatus::NOT_VERIFIED;
            } else {
                cs.status = CheckStatus::NOT_APPLICABLE;
            }
            cs.score = 100.0;
            continue;
        }

        double totalDeducted = 0.0;
        for (const auto& ded : cs.deductions) {
            totalDeducted += ded.pointsDeducted;
        }
        cs.score = std::clamp(100.0 - totalDeducted, 0.0, 100.0);

        if (cs.failedChecks > 0) {
            cs.status = CheckStatus::FAIL;
        } else if (cs.warningChecks > 0) {
            cs.status = CheckStatus::WARNING;
        } else {
            cs.status = CheckStatus::PASS;
        }

        totalApplicableWeight += cs.configuredWeight;
    }

    if (totalApplicableWeight > 0.0) {
        double weightedSum = 0.0;
        for (auto& [cat, cs] : report.categoryScores) {
            const std::size_t activeChecks =
                cs.passedChecks + cs.failedChecks + cs.warningChecks;
            if (activeChecks > 0) {
                cs.effectiveNormalizedWeight =
                    (cs.configuredWeight / totalApplicableWeight) * 100.0;
                weightedSum += cs.score * (cs.configuredWeight / totalApplicableWeight);
            } else {
                cs.effectiveNormalizedWeight = 0.0;
            }
        }
        report.overallScore = std::round(weightedSum * 10.0) / 10.0;
    } else {
        report.overallScore = 100.0;
    }

    report.gradeLabel = computeGradeLabel(report.overallScore);
    return report;
}

}  // namespace repodoctor
