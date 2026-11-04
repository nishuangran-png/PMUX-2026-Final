# v1.0.2-final

- Rebuilt baseline and candidate from the same official Yosys source, compiler,
  flags, and ABC executable.
- Restored the deferred-LUT materialization required by `pmux_opt` for test4.
- Removed the unrelated `kernel/yosys.cc` path patch.
- Removed the unrelated `ff_map_pn1.v` mapping extension.
- Retained the unified submitted ABC9 script.
- Re-ran all nine cases with a same-compiler baseline and candidate.
- Replaced synthesis and performance logs with current same-source evidence.
- Added a current whole-design equivalence run for the rebuilt binaries.
- Corrected the formal `dffeas` model to use active-low `clrn`/`prn` and the
  full asynchronous-control semantics; all nine current cases now pass.
- Regenerated environment evidence with package-relative paths, removed the
  stale GCC identification, and recorded ABC version output without command
  errors.
- Changed future performance raw files to store package-relative source paths.
- Added missing submission notes, source hashes, third-party notices, license
  files, and a standard manifest.
- Excluded the separately submitted Word technical document.
- Normalized `MANIFEST.sha256` to LF line endings so the standard Linux and
  macOS checksum commands validate the package without preprocessing.
- Clarified that the RAR submission and GitHub Release ZIP carry the same
  extracted payload while using different archive containers.
