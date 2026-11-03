# Final source overlay

`overlay/` contains the complete final files. `UNCHANGED_FILES.sha256`
shows that `kernel/io.cc` and `passes/techmap/abc9_exe.cc` are identical
to the official Yosys base.

Changed/new files:

- `passes/pmux/pmux_opt.cc`
- `passes/pmux/CMakeLists.txt`
- `passes/CMakeLists.txt`
- `techlibs/intel/synth_intel.cc`
- `techlibs/intel/CMakeLists.txt`
- `techlibs/intel/common/ff_map_pn1.v`
- `techlibs/intel/cycloneiv/pmux_abc9.abc`
- `kernel/yosys.cc`

The unified review patch is `../patches/changes.patch`.
