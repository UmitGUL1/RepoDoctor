# Security Policy

## 1. Security & Privacy Architecture

RepoDoctor is engineered to inspect untrusted repositories safely:

- **No Untrusted Code Execution**: RepoDoctor never executes repository scripts, Makefiles, `CMakeLists.txt` commands, npm scripts, Python setup files, or Git hooks during a scan.
- **Safe Shallow GitHub Clones**: When scanning a public GitHub URL, `GitHubRepositorySource` validates the URL against a strict `https://github.com/<owner>/<repo>` allowlist, blocks option injection (`-`), disables Git hooks (`-c core.hooksPath=/dev/null`), disables `file://` transport, disables LFS smudge filters, and enforces a clone size limit.
- **Mandatory Secret Redaction**: Suspected credentials, API tokens, and private keys are masked inside `SecretDetectionEngine::redactSecretValue()` before any `Evidence` or `Finding` object is constructed. Full secrets are never printed to stdout or written to JSON, SARIF, or HTML reports.
- **Path Traversal & Symlink Protection**: Both `ScanContext` and `FixExecutor` verify canonical paths and reject symbolic link escapes or `..` traversal segments.

---

## 2. Important Notice Regarding Discovered Secrets

RepoDoctor never automatically deletes or modifies suspected secrets in your repository.

> **Deleting a secret from the working tree or committing a removal does NOT erase the secret from Git history.**
> Anyone who clones the repository can still recover historical commits. If RepoDoctor flags a real credential:
> 1. **Revoke and rotate** the exposed key or password immediately at the provider.
> 2. Move the secret to environment variables or a dedicated secret manager.
> 3. Purge the secret from Git commit history using `git filter-repo` or BFG Repo-Cleaner.

---

## 3. Supported Versions

| Version | Supported |
|---|---|
| `1.0.x` | Yes |

## 4. Reporting a Vulnerability

If you discover a security vulnerability in RepoDoctor itself (such as a path traversal bypass, command injection vector, or redaction leak), please report it privately via GitHub Security Advisories rather than opening a public issue.Include a minimal reproduction fixture and affected platform details.
