# RepoDoctor System Architecture

## 1. Architectural Principles

1. **Strict UI Independence**: The core analysis library (`repodoctor_core`) depends only on the C++20 Standard Library (`std::filesystem`, `std::regex`, `std::chrono`, `std::atomic`), `nlohmann/json`, and optionally `libgit2`. It has zero dependency on Qt.
2. **Passive, Safe Analysis**: Scanning a repository never executes untrusted repository scripts, Makefiles, CMake hooks, npm lifecycle scripts, Python `setup.py`, or Git hooks.
3. **Zero-Leak Secret Handling**: Suspected secrets are redacted inside `SecretDetectionEngine` before creating `Evidence` objects. Full credentials never enter findings, logs, console output, or exported reports.
4. **Transparent, Explainable Scoring**: Every score deduction traces directly to a specific rule finding with explicit category weights and support for `PASS`, `FAIL`, `WARNING`, `NOT_APPLICABLE`, and `NOT_VERIFIED`.
5. **Non-Destructive Auto-Fix**: Fix planning (`FixPlanner`) is strictly separated from execution (`FixExecutor`). Fixes produce unified diffs, reject symlink/path traversal escapes, detect conflicts, create backups, and require explicit confirmation.

---

## 2. Component Diagram

```
+-----------------------------------------------------------------------+
|                    Presentation & Entry Points                        |
|   +----------------------------+   +------------------------------+   |
|   |   repodoctor-cli (C++20)   |   |  RepoDoctor Desktop (Qt6/QML)|   |
|   +-------------+--------------+   +---------------+--------------+   |
+-----------------|----------------------------------|------------------+
                  |                                  |
                  v                                  v
+-----------------------------------------------------------------------+
|                       repodoctor_core (Static Lib)                    |
|                                                                       |
|  +-----------------------+    +------------------------------------+  |
|  |   IRepositorySource   |    |        ConfigurationManager        |  |
|  | - LocalRepoSource     |    | - .repodoctor.json parsing         |  |
|  | - GitHubRepoSource    |    | - Rule overrides & justifications  |  |
|  +-----------+-----------+    +-----------------+------------------+  |
|              |                                  |                     |
|              v                                  v                     |
|  +-----------------------------------------------------------------+  |
|  |                           ScanEngine                            |  |
|  |  Builds ScanContext -> Runs RuleRegistry -> Invokes RiskScorer  |  |
|  +--------+------------------+-------------------+-----------------+  |
|           |                  |                   |                    |
|           v                  v                   v                    |
|  +-----------------+ +----------------+ +--------------------------+  |
|  | GitRepoInspector| |  RuleRegistry  | |      RiskScorer          |  |
|  | (libgit2 + fs)  | |  25+ IRules    | | 0-100 Category Breakdown |  |
|  +-----------------+ +-------+--------+ +--------------------------+  |
|                              |                                        |
|           +------------------+------------------+                     |
|           v                                     v                     |
|  +-------------------------+         +-----------------------------+  |
|  |  SecretDetectionEngine  |         |     DependencyInspector     |  |
|  | Context-aware + Redact  |         | CMake/vcpkg, npm, Python    |  |
|  +-------------------------+         +-----------------------------+  |
|                                                                       |
|  +---------------------------------+ +-----------------------------+  |
|  |  FixPlanner & FixExecutor       | |       ReportGenerator       |  |
|  |  IFixProvider, Unified Diff,    | | Console, JSON, SARIF 2.1.0, |  |
|  |  Symlink Guard, Transactional   | | Self-Contained HTML         |  |
|  +---------------------------------+ +-----------------------------+  |
+-----------------------------------------------------------------------+
```

---

## 3. Core Interfaces & Responsibilities

### 3.1 Repository Acquisition (`IRepositorySource`)
- `LocalRepositorySource`: Validates local directory existence, resolves canonical path, verifies readability, and ensures symlink boundaries are respected.
- `GitHubRepositorySource`: Validates `https://github.com/<owner>/<repo>` URLs against strict allowlists, prevents argument injection, clones into a temporary sandboxed directory with `--depth 1 --no-recurse-submodules -c core.hooksPath=/dev/null`, enforces size and timeout bounds, and cleans up via RAII on destruction.

### 3.2 Repository Inspection (`ScanContext`, `GitRepositoryInspector`, `DependencyInspector`, `SecretDetectionEngine`)
- `ScanContext`: Performs a single bounded-memory pass over the repository directory tree, indexing file metadata (`RelativePath`, byte size, binary flag, permissions, tracked status) while honoring `.repodoctor.json` exclusions, max file size limits, and an atomic cancellation token (`std::atomic<bool>`).
- `GitRepositoryInspector`: Uses `libgit2` (when compiled with `REPODOCTOR_USE_LIBGIT2`) and direct `.git/` metadata inspection to determine repository validity, HEAD reference health, tracked file index, `.gitignore` rules, submodules, and committed large/binary blobs.
- `DependencyInspector`: Parses `CMakeLists.txt`, `vcpkg.json`, `package.json` (+ `package-lock.json`, `yarn.lock`, `pnpm-lock.yaml`), `requirements.txt`, and `pyproject.toml` (+ `poetry.lock`, `uv.lock`, `Pipfile.lock`) into normalized `DependencyDeclaration` records.
- `SecretDetectionEngine`: Scans text files line-by-line with bounded line length, combining signature patterns (AWS, GitHub, Slack, Stripe, Google, PEM private keys, connection strings, `.env` assignments) with Shannon entropy and false-positive heuristics (ignoring `EXAMPLE`, `dummy`, `placeholder`, `${ENV_VAR}`, test fixtures). All matches pass through `SecretDetectionEngine::redact()` before leaving the engine.

### 3.3 Rules Engine (`IRule`, `RuleRegistry`)
- Each rule implements `IRule` and returns a `RuleEvaluationResult` containing a `CheckStatus` (`PASS`, `FAIL`, `WARNING`, `NOT_APPLICABLE`, `NOT_VERIFIED`) and zero or more `Finding` records with `Evidence` items.
- `RuleRegistry` maintains the canonical catalog of rules, supports filtering by category or ID, and enforces configuration overrides (including recording when a rule is disabled and whether a justification was documented).

### 3.4 Scoring (`RiskScorer`)
- Computes category scores (0–100) for:
  1. `Documentation` (default weight: 20%)
  2. `SecurityHygiene` (default weight: 25%)
  3. `BuildAndCI` (default weight: 15%)
  4. `Testing` (default weight: 15%)
  5. `RepositoryHygiene` (default weight: 15%)
  6. `DependencyManagement` (default weight: 10%)
- Each finding produces an explicit, documented deduction (`ScoreDeduction`) capped per rule so a single noisy rule cannot distort the entire category.
- Categories where all rules return `NOT_APPLICABLE` are marked `NOT_APPLICABLE` and their weights are proportionally redistributed across applicable categories.
- Checks returning `NOT_VERIFIED` are explicitly surfaced and never counted as `PASS`.

### 3.5 Auto-Fix Subsystem (`IFixProvider`, `FixPlanner`, `FixExecutor`)
- `FixPlanner` evaluates findings and generates a `FixPlan` containing discrete `PlannedFix` items.
- Each `PlannedFix` includes the target relative path, operation type (`CreateFile`, `ModifyFile`), original content, proposed content, unified diff, and conflict status.
- `FixExecutor` validates that every target path resolves strictly inside the repository root without traversing symlinks, creates `.repodoctor-backup/` snapshots when modifying existing files, and writes changes atomically.
