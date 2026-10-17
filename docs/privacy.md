# Privacy & Data Handling Architecture

RepoDoctor is built from the ground up with strict developer privacy and offline operation guarantees.

---

## 1. Zero Cloud, Account, or AI Dependency

- Local repository scans operate **100% offline** using the C++20 Standard Library, `std::filesystem`, and `libgit2`.
- No user account, API key, telemetry endpoint, or external AI service is ever contacted or required.

## 2. Public GitHub URL Scanning

- Only when you explicitly invoke `repodoctor scan https://github.com/<owner>/<repo>` (or click **Scan GitHub URL** in the desktop GUI) does RepoDoctor perform a network request—specifically a shallow `git clone --depth 1` directly from `github.com` into a temporary directory.
- Temporary checkouts are deleted automatically when the scan completes (`GitHubRepositorySource::~GitHubRepositorySource()`).

## 3. Mandatory Secret Redaction

- Whenever `SecretDetectionEngine` encounters a suspected API key, cloud token, password, or private key, it immediately masks the value via `SecretDetectionEngine::redactSecretValue()` (e.g., `ghp_****************ABCD` or `[REDACTED:PRIVATE_KEY]`).
- Raw secret strings are never stored in `Finding` or `Evidence` structs, never rendered in the GUI, never printed to the console, and never written to JSON, SARIF, or HTML reports.
