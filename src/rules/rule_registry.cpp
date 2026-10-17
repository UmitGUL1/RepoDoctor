#include "repodoctor/rules/rule_registry.hpp"
#include "repodoctor/rules/builtin_rules.hpp"

namespace repodoctor {

RuleRegistry::RuleRegistry() = default;

RuleRegistry RuleRegistry::createDefaultRegistry() {
    RuleRegistry reg;
    registerDocumentationRules(reg);
    registerGitHygieneRules(reg);
    registerSecurityRules(reg);
    registerBuildAndCiRules(reg);
    registerDependencyRules(reg);
    return reg;
}

void RuleRegistry::registerRule(std::unique_ptr<IRule> rule) {
    if (rule != nullptr) {
        rules_.push_back(std::move(rule));
    }
}

const IRule* RuleRegistry::findById(std::string_view ruleId) const {
    for (const auto& rule : rules_) {
        if (rule->id() == ruleId) {
            return rule.get();
        }
    }
    return nullptr;
}

std::vector<const IRule*> RuleRegistry::findByCategory(RuleCategory category) const {
    std::vector<const IRule*> out;
    for (const auto& rule : rules_) {
        if (rule->category() == category) {
            out.push_back(rule.get());
        }
    }
    return out;
}

}  // namespace repodoctor
