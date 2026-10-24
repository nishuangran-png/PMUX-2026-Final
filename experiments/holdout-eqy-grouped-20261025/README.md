# 封存集 EQY 全输出同分区复验（2026-10-25）

> **版本边界（2026-10-25 补记）**：本目录保存的是旧 `ad7ff16f3 / 0.69+117`
> 构建上的分区方法实验，表中 `323→316`、`1315→1208` 等数字**不是**官网 9f75
> 的正式成绩。它只证明“默认逐位分区可能产生假反例、全输出同分区可消除该类假反例”。
> 官网 9f75 的最终数字是 `priority_encoder_128: 307→307`、
> `priority_encoder_512: 1346→1219`、`arbiter_64: 172→163`，见
> `reports/036-官网0.69纠错与最终验证-20261025.md`。

## 结论

默认 EQY 会把 `priority_encoder` 的 128 个未编码输出和 7 个编码输出逐位切成独立分区。
其中 128/512 位优先级编码器的若干分区出现了**部分定义**：分区把某个未纳入独立输入的
编码输出位绑成 `1'z`，再在另一分区里自由选择该位，因而产生假反例。该问题已在
`experiments/holdout-eqy-default-failure-20261025/` 固化。

把顶层全部 `output_*` 放进同一 `[collect] group output_*` 后，三个用例均通过：

| 用例 | 基线 LCELL+NOT | 候选 LCELL+NOT | 缩减 | DFF | EQY |
|---|---:|---:|---:|---:|:---:|
| `verilog_axi_priority_encoder_128` | 323 | 316 | 2.17% | 0 | PASS |
| `verilog_axi_priority_encoder_512` | 1315 | 1208 | 8.14% | 0 | PASS |
| `verilog_axi_arbiter_64` | 173 | 163 | 5.78% | 71 不变 | PASS |

`reports/036-holdout-lut-equivalence.*` 另用直接 SAT miter 证明了同一组 abc9 `$lut`
网表等价，支持“默认分区的 128/512 FAIL 是分区假阴性”的判断。

**表述边界：** 这里是“默认分区的假阴性已定位，改为全输出同分区后通过”，不是
“默认 EQY 分区原本全部通过”。

## 复现

```bash
python3 scripts/eval_holdout.py \
  --manifest experiments/holdout-20261024/manifest.json \
  --case verilog_axi_priority_encoder_128 \
  --case verilog_axi_priority_encoder_512 \
  --case verilog_axi_arbiter_64 \
  --workdir experiments/holdout-eqy-grouped-rerun \
  --eqy-collect-group 'output_*' \
  --json reports/036-holdout-eqy-grouped.json
```

`--workdir` 必须为空目录。提交的 `*.eqy` 是当次实际配置；EQY 自动生成的 IL、分区和
策略目录体积很大，只保留在本机并由 `.gitignore` 忽略。
