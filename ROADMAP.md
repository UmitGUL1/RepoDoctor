# RepoDoctor Engineering Roadmap

**Tagline**: *Scan. Diagnose. Improve.*

RepoDoctor is an open-source, cross-platform repository health analyzer written in C++20 with a headless CLI (`repodoctor-cli`) and a Qt 6 / QML desktop application (`RepoDoctor`). It inspects local Git repositories and public GitHub repositories for documentation completeness, Git hygiene, security and credential exposure, CI/CD & build quality, and dependency hygiene—without requiring accounts, subscriptions, cloud services, or AI API keys.

---

## Phased Delivery Plan

### Phase 0 — Environment, Architecture, CMake & Dependencies
- Inspect compiler toolchains, CMake, Qt 6, `libgit2`, `nlohmann/json`, and `GoogleTest`.
- Establish strict separation between `repodoctor_core` (pure C++20 + `std::filesystem` + optional `libgit2` + `nlohmann::json`) and the Qt 6 UI layer.
- Define `.clang-format`, `.clang-tidy`, `.gitignore`, `ROADMAP.md`, `IMPLEMENTATION_STATUS.md`, and `docs/architecture.md`.

### Phase 1 — Core Scanning Engine & Headless CLI (`repodoctor-cli`)
- Implement foundational value types: `Severity`, `CheckStatus` (`PASS`, `FAIL`, `WARNING`, `NOT_APPLICABLE`, `NOT_VERIFIED`), `RuleCategory`, `Evidence`, `Finding`.
- Implement `IRepositorySource`, `LocalRepositorySource`, `GitRepositoryInspector`, `ConfigurationManager`, `ScanContext`, `IRule`, `RuleRegistry`, and `ScanEngine`.
- Implement bounded-memory file streaming, binary detection, path exclusion globs, file size ceilings, progress reporting, and cancellation support.
- Build `repodoctor-cli` supporting `scan`, `rules`, `explain <RULE_ID>`, `fix --dry-run`, `--format`, `--output`, and `--help` with deterministic CI exit codes.

### Phase 2 — Documentation, Git Hygiene, Build/CI & Dependency Rules
- Implement **Documentation Rules (`RD-DOC-001` .. `RD-DOC-007`)**:
  - Missing README, empty/stub README, missing installation instructions, missing usage examples, missing contribution guidance, broken relative documentation links, missing license file (with explicit non-legal-advice disclaimer).
- Implement **Git Hygiene Rules (`RD-GIT-001` .. `RD-GIT-007`)**:
  - Missing/ineffective `.gitignore`, accidentally tracked build artifacts (`build/`, `node_modules/`, object/binary outputs), large committed files, suspicious binary files, temporary/OS junk files, unresolved merge conflict markers, broken/missing Git metadata.
- Implement **Build & CI Rules (`RD-BLD-001` .. `RD-BLD-005`)**:
  - Missing CI workflow, missing test integration, unpinned third-party GitHub Actions, insecure workflow permissions/triggers (`pull_request_target`, `write-all`), inconsistent/legacy CMake configurations.
- Implement **Dependency Rules (`RD-DEP-001` .. `RD-DEP-003`)** via `DependencyInspector`:
  - CMake/`vcpkg.json`, npm `package.json`, Python `requirements.txt` & `pyproject.toml`.
  - Detect missing lockfiles, unpinned/wildcard dependencies, inconsistent declarations, and optional local advisory matching.

### Phase 3 — Security Hygiene & Context-Aware Secret Detection with Redaction
- Implement `SecretDetectionEngine` and **Security Rules (`RD-SEC-001` .. `RD-SEC-005`)**:
  - Hardcoded cloud/API tokens (AWS, GitHub PAT, Slack, Stripe, Google API, generic high-entropy secrets).
  - Private key material (RSA, OpenSSH, EC, PGP private keys).
  - Suspicious `.env` files and exposed configuration secrets.
  - Hardcoded database/service credentials in configs/code.
  - Unsafe file permissions (world-writable scripts/configs or overly permissive key files where supported).
- Enforce mandatory cryptographic/structural **secret redaction** (`RedactedSecret`) so raw secrets are never stored in `Evidence`, printed to stdout, or exported to reports.
- Include explicit Git history persistence warnings (`git filter-repo` / credential rotation).

### Phase 4 — Transparent Scoring Engine & Multi-Format Report Generation
- Implement `RiskScorer`:
  - 0–100 health score across Documentation, Security Hygiene, Build and CI, Testing, Repository Hygiene, and Dependency Management.
  - Configurable category weights, explainable per-finding deductions, automatic re-normalization for `NOT_APPLICABLE` categories, and strict handling of `NOT_VERIFIED` checks (never counted as `PASS`).
- Implement `ReportGenerator`:
  - Human-readable ANSI/plain console summary.
  - Machine-readable JSON report.
  - OASIS SARIF v2.1.0 report for GitHub Code Scanning / CI integration.
  - Self-contained, accessible dark-theme HTML report.

### Phase 5 — Safe Auto-Fix Planning, Diff Preview & Transactional Execution
- Implement `IFixProvider`, `FixPlanner`, and `FixExecutor`:
  - Generate language-aware `.gitignore` templates.
  - Generate structured `README.md` templates.
  - Generate `CONTRIBUTING.md` templates.
  - Generate starter GitHub Actions CI workflows (`.github/workflows/ci.yml`).
  - Append non-destructive documentation sections (Installation, Usage, Contributing) with backup protection.
- Enforce unified diff generation, dry-run mode, path traversal & symlink escape prevention, conflict detection, and backup creation before any write.

### Phase 6 — Qt 6 / QML Desktop Application (`RepoDoctor`)
- Build the native desktop GUI (`apps/gui`) backed by `repodoctor_core`:
  - **Home**: Local directory picker, public GitHub URL input, recent scan history, quick-start presets.
  - **Scan Dashboard**: Overall 0–100 health score gauge, 6 category score cards, scan duration, files analyzed, severity distribution, and `PASS` / `WARNING` / `FAIL` / `NOT_APPLICABLE` / `NOT_VERIFIED` breakdown.
  - **Findings**: Real-time search, category & severity filters, sorting, redacted evidence inspector, one-click open file location, and remediation guidance panel.
  - **Auto-Fix**: Fixable issue selector, live unified diff preview, explicit confirmation modal, backup toggle, and execution log.
  - **Reports**: One-click export to JSON, SARIF 2.1.0, and HTML with export history.
  - **Settings**: Rule toggle switches, path exclusion patterns, file size limits, privacy controls, and configuration persistence.

### Phase 7 — Safe Public GitHub Repository Integration
- Implement `GitHubRepositorySource`:
  - Strict URL validation (`https://github.com/<owner>/<repo>`), disallowing arbitrary protocols (`ext::`, `file://`, `ssh://` injection) and option injection (`--upload-pack`).
  - Shallow, non-recursive, hook-disabled, LFS-disabled temporary clone (`--depth 1 --no-recurse-submodules -c core.hooksPath=/dev/null`) with timeout and size ceiling enforcement.
  - RAII temporary directory cleanup.

### Phase 8 — GitHub Actions & CI Pipeline Integration
- Create `.github/workflows/ci.yml` and `.github/workflows/release.yml` with pinned actions, minimal `permissions: contents: read`, Linux + Windows build matrix, test execution, SARIF upload support, and configurable severity exit thresholds.

### Phase 9 — Performance Benchmarking, Stress Testing & Hardening
- Build synthetic repository generator and benchmark suite (`benchmarks/repodoctor_benchmarks.cpp`) testing small (50 files), medium (1,000 files), and large (5,000+ files) repositories.
- Verify cancellation responsiveness, memory bounds, symlink loop protection, malformed file resilience, Unicode paths, and AddressSanitizer + UndefinedBehaviorSanitizer cleanliness.

### Phase 10 — Packaging, Documentation & Open-Source Release Preparation
- Build portable Windows x64 CLI release (`repodoctor-cli.exe` + archive + SHA-256 checksums) and Linux binaries (`repodoctor-cli` + `RepoDoctor` desktop app).
- Complete comprehensive documentation (`README.md` with real screenshots, `CONTRIBUTING.md`, `SECURITY.md`, `CODE_OF_CONDUCT.md`, `LICENSE`, and `docs/*.md`).
