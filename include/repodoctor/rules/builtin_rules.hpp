#pragma once

#include "repodoctor/rules/irule.hpp"
#include "repodoctor/rules/rule_registry.hpp"

namespace repodoctor {

void registerDocumentationRules(RuleRegistry& registry);
void registerGitHygieneRules(RuleRegistry& registry);
void registerSecurityRules(RuleRegistry& registry);
void registerBuildAndCiRules(RuleRegistry& registry);
void registerDependencyRules(RuleRegistry& registry);

}  // namespace repodoctor
