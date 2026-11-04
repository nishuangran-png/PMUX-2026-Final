# Independent validation of the rebuilt final pair

## Purpose

The previous package compared a candidate with a separately built baseline.
This revision rebuilds both binaries from the same official source and
toolchain. The only source difference is the submitted PMUX pass, its
integration, and the submitted ABC script.

## Commands

Both arms used:

```text
read_verilog <case>.v
synth_intel -family cycloneiv -top <top>
stat
```

## Results

| Case | Baseline | Candidate | Reduction |
|---|---:|---:|---:|
| test1 | 288 | 180 | 37.50% |
| test2 | 25 | 16 | 36.00% |
| test3 | 13 | 10 | 23.08% |
| test4 | 38 | 26 | 31.58% |
| balanced | 406 | 248 | 38.92% |
| pipeline | 444 | 376 | 15.32% |
| crossbar | 409 | 310 | 24.21% |
| feedback | 365 | 301 | 17.53% |
| nested | 415 | 344 | 17.11% |

- Official four average: `32.0390%`
- Added five average: `22.6159%`
- Overall average: `26.8039%`
- No case increased; no DFF increased.

## Performance

The five-round same-source run gives:

- test2 wall mean ratio: `1.0314`;
- worst wall median ratio: `1.0332`;
- worst memory ratio: `1.0101`.

All are below `1.05`. Normal process-scheduling variation remains visible in
individual samples, but the five-round test2 aggregate stays below the limit.

## Formal check

Whole-design Yosys equivalence passes all nine current cases. The comparison is
performed on the rebuilt binaries after ABC9 and before `map_cells`. The formal
`dffeas` model uses active-low `clrn`/`prn` and models asynchronous load, clock
enable, synchronous clear, and synchronous load. Test4 is included in this
current run: its pre-map LUT count changes from 34 to 30, its final mapped area
metric changes from 38 to 26, and all ten outputs are proved equivalent.
