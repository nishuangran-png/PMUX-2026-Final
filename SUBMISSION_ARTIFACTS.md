# Submission artifacts

The evaluator-facing archive is:

```text
PMUX-2026-Final-v1.0.2-final-repro.rar
```

The GitHub Release mirror is:

```text
PMUX-2026-Final-v1.0.2-final-repro.zip
```

Both archives contain the same extracted payload. RAR and ZIP are different
container formats, so their outer file hashes are not expected to match. Such
equivalence is established by comparing the extracted file list and each
file's SHA-256.

The SHA-256 sidecar is generated after the archive is frozen.

`MANIFEST.sha256` covers every file in the archive except itself. It uses LF
line endings, allowing verification with either:

```text
sha256sum -c MANIFEST.sha256
shasum -a 256 -c MANIFEST.sha256
```

The runtime pair is:

- `baseline/yosys.exe`
- `candidate/yosys.exe`
- identical `yosys-abc.exe` in both directories

The source pair is:

- `source/baseline_source_tree.tar.gz`
- `source/candidate_source_tree.tar.gz`
- `source/patches/changes.patch`

This package is self-contained for synthesis and includes all scripts and
relative runtime data needed by the "all code" upload. The separately submitted
Word technical document is intentionally absent from both archive formats.
