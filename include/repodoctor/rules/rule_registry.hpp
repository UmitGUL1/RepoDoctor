#pragma once

#include "repodoctor/rules/irule.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace repodoctor {

class RuleRegistry {
public:
    RuleRegistry();

    static RuleRegistry createDefaultRegistry();

    void registerRule(std::unique_ptr<IRule> rule);

    [[nodiscard]] const std::vector<std::unique_ptr<IRule>>& rules() const noexcept {
        return rules_;
    }

    [[nodiscard]] const IRule* findById(std::string_view ruleId) const;

    [[nodiscard]] std::vector<const IRule*> findByCategory(RuleCategory category) const;

private:
    std::vector<std::unique_ptr<IRule>> rules_;
};

}  // namespace repodoctor
