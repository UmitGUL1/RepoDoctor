#pragma once

#include "repodoctor/config/configuration_manager.hpp"
#include "repodoctor/core/types.hpp"

#include <map>
#include <string>
#include <vector>

namespace repodoctor {

struct ScoreDeduction {
    std::string ruleId;
    std::string ruleTitle;
    RuleCategory category{RuleCategory::Documentation};
    Severity severity{Severity::Medium};
    double pointsDeducted{0.0};
    std::string explanation;
};

struct CategoryScore {
    RuleCategory category{RuleCategory::Documentation};
    double configuredWeight{0.0};
    double effectiveNormalizedWeight{0.0};
    double score{100.0};
    CheckStatus status{CheckStatus::PASS};
    std::size_t passedChecks{0};
    std::size_t failedChecks{0};
    std::size_t warningChecks{0};
    std::size_t notApplicableChecks{0};
    std::size_t notVerifiedChecks{0};
    std::vector<ScoreDeduction> deductions;
};

struct HealthScoreReport {
    double overallScore{100.0};
    std::string gradeLabel{"A"};
    std::map<RuleCategory, CategoryScore> categoryScores;
    std::vector<ScoreDeduction> allDeductions;
    std::size_t totalPassedChecks{0};
    std::size_t totalFailedChecks{0};
    std::size_t totalWarningChecks{0};
    std::size_t totalNotApplicableChecks{0};
    std::size_t totalNotVerifiedChecks{0};
    std::string methodologyNote;
};

class RiskScorer {
public:
    RiskScorer() = default;

    [[nodiscard]] HealthScoreReport computeScore(
        const std::vector<RuleEvaluationResult>& evaluations,
        const ScanConfiguration& config) const;

    [[nodiscard]] static double baseDeductionForSeverity(
        Severity severity,
        CheckStatus status) noexcept;

    [[nodiscard]] static std::string computeGradeLabel(double score);
};

}  // namespace repodoctor
