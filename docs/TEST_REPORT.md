# Final test report

## Environment

- Yosys commit: `d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`
- Baseline executable SHA-256:
  `6DFC56EE3F7F2F0C50E85004D4D88783053ED680101EC693B0A2F5FF2F8CFD0B`
- Candidate executable SHA-256:
  `42210DFD5B5F354FA09FBF1251A48698AAFA9301BA4D9B0428B135ACA56A4D4C`
- ABC SHA-256:
  `E7161F996329320AF6DFACF178813DF0EE3C1A6FA82E7CD2216D37A4B7A828A7`
- Baseline and candidate were built with the same compiler and flags.
- Area metric: `cycloneiv_lcell_comb + $not`.

## Area

| Case | Baseline | Candidate | Reduction | DFF |
|---|---:|---:|---:|---:|
| test1 | 288 | 180 | 37.50% | 222 -> 222 |
| test2 | 25 | 16 | 36.00% | 0 -> 0 |
| test3 | 13 | 10 | 23.08% | 17 -> 17 |
| test4 | 38 | 26 | 31.58% | 10 -> 10 |
| balanced | 406 | 248 | 38.92% | 12 -> 12 |
| pipeline | 444 | 376 | 15.32% | 24 -> 24 |
| crossbar | 409 | 310 | 24.21% | 10 -> 10 |
| feedback | 365 | 301 | 17.53% | 16 -> 16 |
| nested | 415 | 344 | 17.11% | 12 -> 12 |

- Official four average: `32.0390%`
- Added five average: `22.6159%`
- All nine average: `26.8039%`
- Area regressions: `0`
- DFF increases: `0`
- Unmapped wide `$lut`: `0`

## Performance

Five baseline-first/candidate-second rounds were run for every case after
warmup, using one work directory, one script, and one ABC executable.

| Worst metric | Ratio |
|---|---:|
| Wall mean | 1.0314 |
| Wall median | 1.0332 |
| Peak memory | 1.0101 |

All ratios are below `1.05`. The slowest candidate run completed in `1.1554`
seconds and the largest peak working set was under 25 MiB.

## Equivalence

The rebuilt binaries were compared after ABC9 and before `map_cells`. The
whole-design Yosys check passes all nine cases. The formal `dffeas` model uses
active-low `clrn`/`prn` and models asynchronous load, clock enable, synchronous
clear, and synchronous load. The current test4 check reports `34 -> 30`
pre-map LUTs and proves all ten outputs equivalent; the final mapped area
result is `38 -> 26`.

Raw evidence:

- `logs/synthesis/stats.csv`
- `logs/performance/raw.csv`
- `logs/formal-yosys/equiv-all.log`
- `logs/formal-yosys/result.json`
