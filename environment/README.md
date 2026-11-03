# Build environment

- Windows: `10.0.26200`, AMD64.
- Toolchain family: MSYS2 MinGW64 with MSVCRT.
- UCRT64 was not used. `yosys.exe` imports `msvcrt.dll`.
- GCC/G++: `16.2.0`, Rev3, Built by MSYS2 project.
- Target triplet: `x86_64-w64-mingw32`.
- CMake: `4.4.4`.
- Python used by test scripts: `3.11.6`.
- GNU Make: `4.4.1`.
- Build type: `Release`, `-O3 -DNDEBUG`.
- CMake option: `YOSYS_WITHOUT_SLANG=ON`.
- Both builds use the identical ABC executable in this package.

`environment.json` is the machine-readable record. `env-hashes.sha256`
pins the compiler, CMake, Python, Make, Yosys, and ABC executables.

The shipped `yosys-abc.exe` reports ABC 1.01 and is pinned by SHA-256. The
prebuilt binary does not expose its git commit; the Yosys source tree's
expected ABC submodule commit is `1e3d9abd7934ee0376a9fd474655a85780069776`.
