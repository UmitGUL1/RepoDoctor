# Rule Authoring Guide

RepoDoctor is designed so external contributors can add new diagnostic rules with minimal boilerplate.

---

## 1. Anatomy of an `IRule`

Every rule in RepoDoctor inherits from [`repodoctor::IRule`](../include/repodoctor/rules/irule.hpp) and provides:

1. **Stable Rule ID** (`id()`): Follow the convention `RD-<CAT>-<NNN>`:
   - `RD-DOC-*`: Documentation
   - `RD-GIT-*`: Repository / Git Hygiene
   - `RD-SEC-*`: Security Hygiene
   - `RD-BLD-*`: Build, CI, and Testing
   - `RD-DEP-*`: Dependency Management
2. **Title** (`title()`): Concise, human-readable title.
3. **Category** (`category()`): One of the `RuleCategory` enum values.
4. **Default Severity** (`defaultSeverity()`): `Info`, `Low`, `Medium`, `High`, or `Critical`.
5. **Description** (`description()`): Explains what the rule checks and why it matters.
6. **Remediation Guidance** (`remediationGuidance()`): Concrete, actionable steps to fix the issue.
7. **False-Positive Considerations** (`falsePositiveConsiderations()`): Honest notes on when the check may not apply or how to configure around edge cases.
8. **Evaluation Logic** (`evaluate(const ScanContext& context)`): Pure, read-only inspection returning a `RuleEvaluationResult`.

---

## 2. Example Rule Implementation

```cpp
class CustomHeaderGuardRule final : public IRule {
public:
    std::string id() const override { return "RD-BLD-006"; }
    std::string title() const override { return "Missing Header Guard or #pragma once"; }
    RuleCategory category() const noexcept override { return RuleCategory::BuildAndCI; }
    Severity defaultSeverity() const noexcept override { return Severity::Low; }
    std::string description() const override {
        return "Checks C/C++ header files (.hpp, .h) for #pragma once or #ifndef header guards.";
    }
    std::string remediationGuidance() const override {
        return "Add '#pragma once' at the top of each C/C++ header file.";
    }
    std::string falsePositiveConsiderations() const override {
        return "X-Macro headers designed to be included multiple times intentionally omit header guards.";
    }

    RuleEvaluationResult evaluate(const ScanContext& context) const override {
        std::vector<Evidence> missing;
        std::size_t headersChecked = 0;

        for (const auto& file : context.files()) {
            if (!file.relativePath.ends_with(".hpp")) continue;
            const auto content = context.readTextFile(file.relativePath);
            if (!content.has_value()) continue;
            ++headersChecked;
            if (content->find("#pragma once") == std::string::npos &&
                content->find("#ifndef") == std::string::npos) {
                missing.push_back(Evidence{
                    file.relativePath, 1, 1, file.relativePath, "Missing #pragma once", false});
            }
        }

        if (headersChecked == 0) {
            return makeNotApplicableResult("No .hpp files found.");
        }
        if (missing.empty()) {
            return makePassResult("All headers contain include guards.");
        }

        RuleEvaluationResult res{};
        res.ruleId = id();
        res.ruleTitle = title();
        res.category = category();
        res.defaultSeverity = defaultSeverity();
        res.status = CheckStatus::WARNING;
        Finding f = makeFinding("Detected headers without include guards.", CheckStatus::WARNING);
        f.evidence = std::move(missing);
        res.findings.push_back(std::move(f));
        return res;
    }
};
```

---

## 3. Registering and Testing Your Rule

1. Register the rule inside the appropriate `register*Rules(RuleRegistry&)` function in `src/rules/`.
2. Add both a **true-positive** test case and a **false-positive / `NOT_APPLICABLE`** test case in `tests/repodoctor_tests.cpp`.
3. Verify via `./build/linux/repodoctor-cli explain <YOUR_RULE_ID>`.
