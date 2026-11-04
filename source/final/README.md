# Final source overlay

`overlay/` contains the final files.

Only the following source components differ from the unmodified official
Yosys source:

- `passes/pmux/pmux_opt.cc`: new optimization pass;
- `passes/pmux/CMakeLists.txt`: build registration for the new pass;
- `passes/CMakeLists.txt`: registration of the new pass directory;
- `techlibs/intel/synth_intel.cc`: insertion of the new pass and the submitted
  ABC9 script in `synth_intel`;
- `techlibs/intel/CMakeLists.txt`: installation of the submitted ABC script;
- `techlibs/intel/cycloneiv/pmux_abc9.abc`: unified submitted ABC9 script.

`kernel/io.cc`, `kernel/yosys.cc`, and `passes/techmap/abc9_exe.cc` are
identical to the official source. No legacy ABC path or unrelated pass code is
modified.

The deferred-LUT branch in `synth_intel.cc` materializes temporary LUT cells
created by `pmux_opt` before the remaining logic is mapped. This is part of the
new pass integration and does not reorder the existing standard passes.

The unified review patch is `../patches/changes.patch`.
