# Compliance statement

- Yosys base version: `d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`.
- Baseline and candidate use the same ABC executable.
- The candidate adds one optimization pass and one unified ABC9 script.
- The ABC script is used for all nine cases without case-name switching.
- No `PMUX_*` tuning environment variable is set by an evaluator-facing script;
  the submitted binary uses its compiled defaults.
- `abc9.verify` remains enabled.
- No `$lut` rewriting is performed after ABC9 and before `map_cells`.
- `kernel/io.cc`, `kernel/yosys.cc`, and `passes/techmap/abc9_exe.cc` match
  the official source.
- No legacy ABC path is used.
- No source change targets `test1` through `test4`, case names, file names, or
  node-count thresholds.

The deferred-LUT branch in `synth_intel.cc` only materializes temporary LUT
cells created by `pmux_opt`. It does not change or reorder any standard pass
implementation.

The source audit is reproducible from `source/CHANGED_FILES.sha256`,
`source/final/UNCHANGED_FILES.sha256`, and `source/patches/changes.patch`.
