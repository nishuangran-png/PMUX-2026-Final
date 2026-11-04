# Submission notes

## Main artifact

Use `PMUX-2026-Final-v1.0.2-final-repro.rar` for the "all code" upload. The
GitHub Release publishes an equivalent ZIP built from the same payload.

## Runtime

- `baseline/` contains the unmodified official Yosys runtime.
- `candidate/` contains the optimized runtime.
- Both directories contain the same `yosys-abc.exe`.
- Scripts set `ABC` and use relative package paths.

## Formal verification

- The current rebuilt binaries have whole-design Yosys equivalence evidence for
  all nine cases in `logs/formal-yosys/`.
- The provided `scripts/run_eqy.ps1` can reproduce native EQY when an EQY
  executable is available, but native EQY is not required to read the frozen
  current evidence.

## Source review

Apply `source/patches/changes.patch` to the official Yosys commit, or use
`source/candidate_source_tree.tar.gz`.

The only modified/new source files are listed in
`source/CHANGED_FILES.sha256`.
