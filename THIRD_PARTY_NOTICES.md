# Third-party notices

This repository contains source derived from Yosys and documents a build of
Yosys together with the ABC synthesis tool.

## Yosys

- Upstream project: <https://github.com/YosysHQ/yosys>
- Base commit: `d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`
- License: ISC
- License copy: `LICENSES/Yosys-ISC.txt`

Yosys-derived files retain their upstream copyright and license headers.

## ABC

The Windows release asset contains a build of ABC. The ABC source is part of
the Yosys source distribution at the pinned Yosys commit and is distributed
under its own upstream license. The release asset is identified by the
SHA-256 values in `hashes/windows_binaries.sha256`.

## Bundled source archives

`source/baseline_source_tree.tar.gz` is an archival copy of the official
Yosys source tree at the pinned commit. License files for Yosys and bundled
third-party libraries remain inside that archive.

No third-party source is relicensed by this repository. Original project
documentation and modifications are provided for the competition submission;
no separate license is asserted for them.
