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

## What did not change

- Case RTL files.
- Final source overlay and patch.
- Synthesis statistics.
- Performance measurements.
- EQY/equivalence conclusions.
- Frozen binary SHA-256 values.

The original package remains the authoritative evidence source. This public
copy is intended to make review and reproduction straightforward without
tracking binary build products in Git history.
