# RepoDoctor verification status

Verified on 2026-10-17 using GitHub Actions.

Source revision: `30e81f00738f40559426106eef9f7ad081ed4234`.

[Build, test logs and downloadable applications](https://github.com/UmitGUL1/RepoDoctor/actions/runs/39042050719)

## Results

| Check | Result |
|---|---|
| Linux C++20 CLI and Qt 6 desktop build | Passed |
| Linux core tests | 39 cases passed |
| Linux Qt controller tests | 4 cases passed |
| Linux real QML application and six-page capture | Passed |
| Linux ASan and UBSan core tests | Passed |
| Windows MSVC CLI and Qt 6 desktop build | Passed |
| Windows core tests | 39 cases passed |
| Windows Qt controller tests | 4 cases passed |
| Windows deployed application with isolated runtime PATH | Passed |
| Windows six-page screenshot capture | Passed |
| CLI self-scan and report generation | Passed on both systems |

The CI run includes the portable **repodoctor-windows-x64** application artifact,
the **repodoctor-linux-x64** artifact with screenshots, and a SARIF report.

## Advanced scanning integration

The Desktop additions were merged onto the previously hardened source. Each
platform now runs 39 core cases and 4 Qt controller cases. Sanitizers exercise
the same 39 core cases. The packaged Windows GUI also passes the isolated-PATH
startup and six-page capture checks.

- Staged scans read index blobs, including partial staging and missing working files.
- Invalid revisions and incompatible scan modes fail explicitly.
- Git reads use bounded, cancellable child processes with separate arguments.
- History preserves snapshot line numbers and binary sizes, rejects shallow clones,
  and withholds potentially sensitive author/message metadata.
- Version 2 SHA-256 baselines distinguish changed secrets and new occurrences.
- Baseline output never overwrites existing files; malformed input fails the scan.
- Accepted baseline checks remain NOT_VERIFIED.
- Quoted ignore markers cannot suppress secrets; cross-rule evidence is redacted.
- New scan settings round-trip, while file-writing paths stay explicit CLI/API inputs.

See [advanced scanning](docs/advanced-scanning.md) for commands, migration and limits.

## Fixes included

- Empty selections never apply unselected fixes.
- GUI scans run on a worker and publish their result before completion signals.
- The controller retains remote checkouts while their reports remain active.
- Git clone supervision applies cancellation, timeout and sampled size limits.
- Backup paths reject symlinks; earlier backups are retained with numbered names.
- Fix staging uses exclusive creation and guarded commits; removed targets conflict.
- Sensitive evidence masks the whole detected line and does not expose a second token.
- File-count truncation produces an explicit incomplete-scan error.
- Windows file-operation names avoid SDK macros; Unicode argument buffers are correctly sized.
- Symlinks are inspected before target resolution. Unicode filename tests verify actual file creation.
- Windows packages include Qt and application-local MSVC runtime files.

## Reproduce

~~~sh
cmake -S . -B build/review -DCMAKE_BUILD_TYPE=Debug
cmake --build build/review --parallel
ctest --test-dir build/review --output-on-failure --no-tests=error
~~~

With Visual Studio, use --config Debug for the build and -C Debug for CTest.
Qt and GoogleTest must be installed for the complete desktop test suite.

## Known limits

- Rules and the score are heuristic static checks, not a security certification.
- Clone size is sampled; brief overshoot between checks is possible.
- Atomic creation requires hard-link support and otherwise fails safely.
- Fixes commit per file rather than rolling back a whole plan transactionally.
- Concurrent hostile replacement of ancestor directories is not fully excluded.
- Secret signatures skip example paths and lines longer than 1024 bytes.
- Linux binaries need distribution Qt/libgit2 runtime packages; they are not AppImages.
- Automated coverage includes controllers, scanning, reports and application startup;
  it does not establish that every possible interactive workflow is defect-free.
