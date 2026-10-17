# RepoDoctor Scoring Methodology

RepoDoctor computes an explainable **Repository Health Score** from **0.0 to 100.0** via [`RiskScorer`](../include/repodoctor/scoring/risk_scorer.hpp).

> **Important Disclaimer**: The RepoDoctor Health Score is a transparent engineering heuristic designed to highlight actionable repository hygiene improvements. It is **not** a formal security certification, legal compliance audit, or guarantee of code correctness.

---

## 1. Default Category Weights

| Category | Enum Value | Default Weight |
|---|---|---|
| **Security Hygiene** | `RuleCategory::SecurityHygiene` | **25.0%** |
| **Documentation** | `RuleCategory::Documentation` | **20.0%** |
| **Build and CI** | `RuleCategory::BuildAndCI` | **15.0%** |
| **Testing** | `RuleCategory::Testing` | **15.0%** |
| **Repository Hygiene** | `RuleCategory::RepositoryHygiene` | **15.0%** |
| **Dependency Management** | `RuleCategory::DependencyManagement` | **10.0%** |

Weights can be customized per repository in `.repodoctor.json` under `"category_weights"`.

---

## 2. Check Statuses

Every rule evaluation produces one of five explicit statuses:

- `PASS`: The check was applicable, verified, and passed with zero findings.
- `WARNING`: A non-blocking engineering or hygiene issue was found (60% of base severity deduction).
- `FAIL`: A substantive issue was detected (100% of base severity deduction).
- `NOT_APPLICABLE`: The check does not apply to this repository (e.g., CMake checks on a pure Python repo, or dependency checks on a repo with no manifests).
- `NOT_VERIFIED`: The check could not be verified locally (e.g., POSIX permission bit checks on a Windows mount, or vulnerability advisory checks when no local OSV advisory cache is provided). **RepoDoctor never treats `NOT_VERIFIED` as `PASS`.**

---

## 3. Severity Deductions Within a Category

Each category starts at **100.0 points**. Findings deduct points according to severity and status:

| Severity | `FAIL` Deduction | `WARNING` Deduction (0.6x) |
|---|---|---|
| `CRITICAL` | 45.0 pts | 27.0 pts |
| `HIGH` | 30.0 pts | 18.0 pts |
| `MEDIUM` | 18.0 pts | 10.8 pts |
| `LOW` | 8.0 pts | 4.8 pts |
| `INFO` | 2.0 pts | 1.2 pts |

Category scores are clamped to `[0.0, 100.0]`.

---

## 4. Weight Re-Normalization for `NOT_APPLICABLE` Categories

If all checks within a category return `NOT_APPLICABLE` (or `NOT_VERIFIED` with zero active checks), that category's effective weight is set to `0.0%` and the remaining applicable categories have their weights proportionally normalized to sum to `100.0%`. This ensures repositories are never penalized for language-specific or ecosystem-specific checks that do not apply to them.
