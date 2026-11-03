# Formal result summary

Raw records: `eqy-all.log`

Machine-readable result: `result.json`

Derived conclusion:

**Successfully proved designs equivalent** for all nine supplied cases.

- `test1`, `test2`, `test3`, `test4`: native EQY partition PASS.
- `pmux_case01_balanced` through `pmux_case05_nested`: the EQY strategy
  failed because generated partition `.sv` files were not found at the
  expected relative path. The raw ERROR records are preserved. The
  designs were then proved with the permitted whole-design
  `equiv_simple/equiv_induct/equiv_status -assert` mode and are marked
  `PASS*`.

The raw log supports only the missing generated `.sv` path diagnosis above.
