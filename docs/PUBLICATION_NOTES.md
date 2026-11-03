# Public copy notes

This directory is the publication copy of the frozen `PMUX_final_repro`
package.

## What changed

- Windows local-home path strings in environment files, synthesis logs,
  EQY netlists, performance CSV files and generated `.ys` files were replaced
  with `%LOCAL_HOME%`.
- The review patch uses LF line endings so it applies cleanly to the LF
  official source tree; its content is otherwise unchanged.
- The large Windows executables and runtime libraries are distributed in the
  GitHub Release rather than committed to Git.
- A repository README, third-party notice and repository-level ignore rules
  were added.
- An evidence-backed development timeline and SVG overview were added for
  official review. The timeline is a curated record of the real development
  stages; it does not create retrospective Git commits.
- A public development log indexes all 31 original non-merge commits from
  2026-09-22 to 2026-10-30. The original commit objects are connected to the
  publication repository's `main` graph; their dates, SHAs and messages are
  not rewritten. The final tree remains the frozen delivery tree.

## What did not change

- Case RTL files.
- Final source overlay and patch.
- Synthesis statistics.
- Performance measurements.
- EQY/equivalence conclusions.
- Frozen binary SHA-256 values.

The later documentation-only revisions change `README.md`,
`DEVELOPMENT_LOG.md`, `docs/DEVELOPMENT_TIMELINE.md`,
`docs/PUBLICATION_NOTES.md`, and the timeline asset. They do not change RTL,
source overlay, patches, binaries, synthesis logs, performance samples, EQY
results, or `FINAL_VERSION.json`. `SHA256SUMS` is regenerated after the
publication documentation update.

The original package remains the authoritative evidence source. This public
copy is intended to make review and reproduction straightforward without
tracking binary build products in Git history.
