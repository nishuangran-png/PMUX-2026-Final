# Build environment

- Windows `10.0.26200`, AMD64.
- Portable Zig `0.16.0` / Clang `21.1.0`.
- CMake `4.4.4`, Ninja `1.13.2`, Python `3.14.2`.
- Release build with `-O3 -DNDEBUG`.
- `YOSYS_WITHOUT_SLANG=ON`.
- External ABC: `yosys-abc.exe` supplied beside each runtime binary.
- Baseline and candidate use the same compiler, flags, source commit, and ABC.

The build wrapper only removes warning-as-error flags that differ between
compilers. It does not alter generated code or optimization behavior.

`compiler-verbose.txt` records the submitted compiler wrapper identity.
`abc-version.txt` records the validated ABC version. Hashes in
`env-hashes.sha256` use package-relative paths and can be checked after moving
the extracted package to another drive.
