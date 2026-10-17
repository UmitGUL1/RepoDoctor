# RepoDoctor implementation and verification status

Updated: 2026-10-17.

## Current source revision

This revision fixes empty auto-fix selections, background GUI scans, remote
checkout lifetime, clone supervision, backup paths and retention, exclusive
staging, Unicode argument conversion, mixed-secret evidence, scan file limits,
saved clone settings, dashboard layout and Windows CI tests.

This is **not a newly verified binary release**. The original desktop directory
and its packaged binaries were not modified. Rebuild this source before use.

## Verification for this revision

- Reviewed changes against the supplied source and checked patch whitespace.
- Attempted CMake configuration with the installed MinGW compiler. Its compiler
  sanity check failed before project compilation.
- WSL execution was denied in this session. Network name resolution prevented
  obtaining another toolchain.
- Updated C++ tests, Qt tests, sanitizers and benchmarks have **not been run**.
  Earlier release claims do not verify these changes.

## Regression coverage added

Core tests cover empty selections, removed targets, backup retention, backup
symlinks, mixed credentials, scan truncation, clone settings, cancellation,
timeout and clone size limits. Clone tests use a compiled local fake Git client
and do not contact GitHub.

Qt controller tests cover asynchronous return and reentry, cancellation during
a clone, controller destruction, and remote checkout lifetime. They build when
both Qt and GoogleTest are available.

## Reproduce verification

~~~sh
cmake -S . -B build/review -DCMAKE_BUILD_TYPE=Debug
cmake --build build/review --parallel
ctest --test-dir build/review --output-on-failure --no-tests=error
~~~

With Visual Studio, add --config Debug to the build and -C Debug to CTest.
Headless builds may set -DREPODOCTOR_BUILD_GUI=OFF. Install GoogleTest to run tests.

## Remaining limits

- Rules remain heuristic static checks, not a security certification.
- Clone size is sampled during download; brief overshoot is possible.
- Atomic creation requires hard-link support; unsupported filesystems fail safely.
- Fixes commit per file, not as a rollback transaction for an entire plan.
- Concurrent hostile replacement of ancestor directories is not fully excluded
  by path checks; use a trusted local workspace.
- Secret signatures still skip example paths and lines longer than 1024 bytes.
  Redaction covers detected secret evidence, not arbitrary text every other rule
  might quote.
