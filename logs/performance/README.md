# Performance evidence

- `raw.csv`: 10 baseline-first/candidate-second rounds per case.
- `summary.csv`, `summary.json`: local no-polling medians and means.
- `recheck-test2/`: permitted five-run recheck after a scheduling spike.
- `official-summary.json`: final combined performance conclusion.
- `legacy-high-frequency-memory-polling/`: preserved old seven-round data,
  explicitly marked as non-final because a 2 ms memory polling thread
  interfered with wall-clock measurement.

Final reported result:

> In an independent Windows audit, ten rounds produced a maximum wall
> ratio of 1.023 and a maximum memory ratio of 1.031, meeting the relative
> 1.05 threshold. Final scoring remains subject to the official evaluation
> environment.

Local no-polling cross-check: worst ten-round median wall ratio `0.990`,
worst memory ratio `1.033`.
