# Hardening changes — 2026-10-17

- Empty FixExecutor selections now do nothing. The CLI explicitly selects the
  whole plan when --fix-id is absent, preserving its documented behavior.
- GUI scans run on a worker, reject reentry and keep remote checkouts alive.
- Git runs without a shell in a supervised job/process group; timeout, size
  sampling and cancellation terminate it and its helpers.
- Fixes reject removed targets, validate backup paths, retain numbered backups,
  create staging files exclusively and preserve existing POSIX permissions.
- Windows targets reject .GIT, alternate data streams and ambiguous trailing
  dots/spaces. CLI UTF-16 conversion allocates its terminating NUL correctly.
- Sensitive evidence masks the whole detected line. Different provider patterns
  run independently. Example comments no longer hide genuine tokens.
- Hitting the file count limit reports an incomplete scan.
- Clone limits round-trip through .repodoctor.json. GUI loads saved local
  repository settings until the user edits session settings.
- Dashboard text wraps; narrower windows use two category columns.
- Windows CI now installs GoogleTest and executes tests; Qt controller regression
  tests join the Linux suite when dependencies are available.

See [implementation status](../IMPLEMENTATION_STATUS.md) for verification limits.
