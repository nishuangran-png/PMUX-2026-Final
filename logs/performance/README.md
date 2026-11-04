# Performance evidence

The current `raw.csv`, `summary.csv`, and `summary.json` are a same-source,
same-compiler, five-round baseline-first/candidate-second measurement of the
final v1.0.2 binaries.

Final result:

- worst wall-clock mean ratio: `1.0314`
- worst wall-clock median ratio: `1.0332`
- worst peak-memory ratio: `1.0101`
- every ratio is below `1.05`

The source column in `raw.csv` uses package-relative paths.
