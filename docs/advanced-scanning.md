# Incremental scans, history and baselines

These options are available in `repodoctor-cli scan`. Git must be installed for
incremental and history scans, including builds that use libgit2 for metadata.

```sh
repodoctor-cli scan . --staged
repodoctor-cli scan . --since HEAD~1
repodoctor-cli scan . --scan-history --max-commits 50
repodoctor-cli scan . --write-baseline baseline-v2.json
repodoctor-cli scan . --baseline baseline-v2.json
```

## Incremental scans

`--staged` scans the actual index blobs. Partial staging, new repositories and
staged files missing from the working directory are supported. Unstaged edits do
not affect the scanned content. Filenames are decoded from NUL-delimited Git
output, preserving Unicode, whitespace and quotes. Deleted entries have no
remaining content to scan.

`--since REF` scans current working files whose tracked paths changed compared
with the specified revision. It does not include untracked files; use a full scan
to cover those. An invalid revision, corrupt index or unavailable Git is a scan
error, not a clean report. An empty valid change set is allowed.

Repository-wide and dependency checks are skipped during incremental scans.
Incremental scores describe only that subset. Use one of `--staged`, `--since`
or `--scan-history` at a time.

## History

History scanning inspects complete versions of changed files in up to 50 commits
by default (allowed range: 1–1000). This preserves original line numbers, catches
credentials removed by later commits, and measures binary files by blob size.
The same credential may appear in multiple historical snapshots.

A full local clone is required. Shallow clones, including the temporary clone
created for GitHub URL scans, report an error when history is requested. This
prevents an incomplete clone being presented as checked history.

Commit hashes are retained. Authors and messages are withheld from reports
because these attacker-controlled fields can contain credentials themselves.
Content over the configured per-file limit is counted as skipped; binary content
is not scanned as text. Historical scanning also has a 32 MB text budget, 50,000
file snapshot limit and a 60-second inspection budget. Each Git operation has a
30-second timeout, bounded output and supports cancellation. Hitting a hard
inspection limit returns an incomplete scan error. Git arguments never use a
shell; hooks, external diffs, text conversion, filesystem monitors and network
protocols are disabled for these reads.

## Baselines

Version 2 baselines use SHA-256 identities including the rule, path, line, column,
commit and original sensitive-line digest. Neither raw credentials nor their
internal digests are included in reports. A changed credential with the same
masked preview is a new finding; a new occurrence on another line is also new.

Accepted findings are counted separately and their checks become `NOT_VERIFIED`,
never `PASS`. Baselines acknowledge existing debt; they do not repair it. Review
baseline changes like source changes. Evidence without a reliable sensitive
content identity is conservatively retained.

Old baseline schemas must be regenerated. Invalid or missing baseline files fail
the scan. Reading and writing baselines are separate operations. Writing uses
exclusive creation and refuses existing output paths, including symlinks. Choose
a new filename when regenerating. Baseline paths are explicit CLI/API inputs;
repository configuration cannot request file writes.

## Inline exceptions

An explicit comment on the same or previous line can accept a known false
positive, for example `// repodoctor-ignore: RD-SEC-001 (test fixture)` or
`# repodoctor-ignore: RD-SEC-004`. Quoted marker text is not an instruction.
Suppressed findings are counted in the report; keep exceptions narrow and
review their reasons.
