#pragma once

#include "repodoctor/core/types.hpp"
#include "repodoctor/engine/scan_context.hpp"

#include <string>

namespace repodoctor {

class IRule {
public:
    virtual ~IRule() = default;

    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::string title() const = 0;
    [[nodiscard]] virtual RuleCategory category() const noexcept = 0;
    [[nodiscard]] virtual Severity defaultSeverity() const noexcept = 0;
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual std::string remediationGuidance() const = 0;
    [[nodiscard]] virtual std::string falsePositiveConsiderations() const = 0;
    [[nodiscard]] virtual bool hasSafeFix() const noexcept { return false; }
    [[nodiscard]] virtual std::string safeFixId() const { return ""; }

    [[nodiscard]] virtual RuleEvaluationResult evaluate(const ScanContext& context) const = 0;

protected:
    [[nodiscard]] RuleEvaluationResult makePassResult(std::string reason = "Check passed.") const;
    [[nodiscard]] RuleEvaluationResult makeNotApplicableResult(std::string reason) const;
    [[nodiscard]] RuleEvaluationResult makeNotVerifiedResult(std::string reason) const;
    [[nodiscard]] Finding makeFinding(
        std::string summary,
        CheckStatus status = CheckStatus::FAIL,
        std::optional<Severity> overrideSeverity = std::nullopt) const;
};

}  // namespace repodoctor
