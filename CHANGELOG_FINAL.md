# Final change note

- Source code and candidate binary were frozen; no rebuild was performed.
- Replaced high-frequency memory polling with no-polling wall-clock measurement.
- Preserved the old seven-round data as non-final historical evidence.
- Added 10-round no-polling raw data and the permitted five-run test2 recheck.
- Updated the formal report to the independently audited 1.023 wall-clock and
  1.031 memory ratios.
- Corrected the added-five-case EQY diagnosis to a generated `.sv` relative
  path error, followed by successful whole-design equivalence.
- Corrected all delivery paths to `source/final/`, `candidate/`,
  `logs/eqy/netlists/`, `source/final/dffeas_formal.v`,
  `logs/performance/`, and `logs/eqy/`.
- Added `scripts/smoke_runtime.ps1`.
- Documented that the candidate resolves ABC from its own directory while the
  baseline must be run through the package scripts that set `ABC`.
- Regenerated the Word report and the original frozen package's
  `MANIFEST.sha256`.
