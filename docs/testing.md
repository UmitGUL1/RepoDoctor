# Testing & Verification Guide

RepoDoctor uses **GoogleTest** (`tests/repodoctor_tests.cpp`) and a synthetic benchmark suite (`benchmarks/repodoctor_benchmarks.cpp`) to verify correctness, security redaction, and performance.

---

## 1. Running the Automated Test Suite

```bash
cmake -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel
ctest --test-dir build/linux --output-on-failure
```

## 2. Test Coverage Matrix

| Test Suite | Verified Scenarios |
|---|---|
| `RuleRegistryTest` | Verifies all 27 built-in rules have unique IDs, non-empty descriptions, remediation guidance, and false-positive notes. |
| `HealthyRepositoryTest` | Verifies a complete, well-documented repository achieves 100.0/100 (`Grade A`) with zero false positives. |
| `DocumentationRulesTest` | Verifies `RD-DOC-001`..`007` (missing README, stub README, missing install/usage/contributing docs, broken relative Markdown links, missing LICENSE). |
| `GitHygieneRulesTest` | Verifies `RD-GIT-001`..`007` (missing `.gitignore`, tracked build artifacts, oversized files, `.exe` binaries, `.bak` temp files, merge conflict markers, corrupted `.git/HEAD`, and Markdown horizontal-rule false-positive avoidance). |
| `SecurityRulesAndRedactionTest` | Verifies `RD-SEC-001`..`004` true positives, `.env.example` / `AKIAIOSFODNN7EXAMPLE` false-positive suppression, and strict redaction across Console, JSON, SARIF, and HTML reports. |
| `BuildCiAndDependencyRulesTest` | Verifies `RD-BLD-001`..`005` and `RD-DEP-001`..`003` (missing tests, unpinned GitHub Actions, `pull_request_target` + `write-all`, legacy CMake, missing lockfile, wildcard/duplicate dependencies, local OSV advisory cache match). |
| `EdgeCasesTest` | Verifies empty repositories, Unicode filenames (`ümit_gül_doküman_日本語.md`), deeply nested paths, malformed `package.json` & `.repodoctor.json`, symlinks, and mid-scan `std::atomic<bool>` cancellation. |
| `AutoFixSubsystemTest` | Verifies unified diff generation, dry-run non-modification, backup creation, conflict detection on pre-existing files, and rejection of `../` path traversal and `.git/` writes. |
| `GitHubUrlValidationTest` | Verifies public HTTPS GitHub URL normalization and rejection of `--upload-pack` option injection, shell metacharacters, `file://` URLs, and `..` segments. |
| `ReportFormatsTest` | Parses and validates JSON, OASIS SARIF v2.1.0, and HTML report output. |

---

## 3. Running AddressSanitizer & UndefinedBehaviorSanitizer

```bash
cmake -B build/sanitizers -G Ninja -DCMAKE_BUILD_TYPE=Debug -DREPODOCTOR_ENABLE_SANITIZERS=ON -DREPODOCTOR_BUILD_GUI=OFF
cmake --build build/sanitizers --parallel
ctest --test-dir build/sanitizers --output-on-failure
```
