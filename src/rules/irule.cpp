#include "repodoctor/rules/irule.hpp"

namespace repodoctor {

RuleEvaluationResult IRule::makePassResult(std::string reason) const {
    RuleEvaluationResult res{};
    res.ruleId = id();
    res.ruleTitle = title();
    res.category = category();
    res.defaultSeverity = defaultSeverity();
    res.status = CheckStatus::PASS;
    res.statusReason = std::move(reason);
    return res;
}

RuleEvaluationResult IRule::makeNotApplicableResult(std::string reason) const {
    RuleEvaluationResult res{};
    res.ruleId = id();
    res.ruleTitle = title();
    res.category = category();
    res.defaultSeverity = defaultSeverity();
    res.status = CheckStatus::NOT_APPLICABLE;
    res.statusReason = std::move(reason);
    return res;
}

RuleEvaluationResult IRule::makeNotVerifiedResult(std::string reason) const {
    RuleEvaluationResult res{};
    res.ruleId = id();
    res.ruleTitle = title();
    res.category = category();
    res.defaultSeverity = defaultSeverity();
    res.status = CheckStatus::NOT_VERIFIED;
    res.statusReason = std::move(reason);
    return res;
}

Finding IRule::makeFinding(
    std::string summary,
    CheckStatus status,
    std::optional<Severity> overrideSeverity) const {
    Finding f{};
    f.ruleId = id();
    f.title = title();
    f.category = category();
    f.severity = overrideSeverity.value_or(defaultSeverity());
    f.status = status;
    f.summary = std::move(summary);
    f.description = description();
    f.remediationGuidance = remediationGuidance();
    f.falsePositiveNotes = falsePositiveConsiderations();
    f.fixable = hasSafeFix();
    f.fixId = safeFixId();
    return f;
}

}  // namespace repodoctor
