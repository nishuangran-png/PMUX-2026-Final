# 官方新增 5 例效率评测（2026-10-26）

## 口径

- 基线：`tools/build/official-v069/build/yosys`
- 候选：`tools/build/official-v069-opt/build-current-20261025/yosys`
- 候选 SHA-256：`15591414460969e5ecdbead9ff9e05e9e8519e0e1bae439cb73a7d308b01e253`
- 策略：`tree-low + factor + skip-deep`，深度阈值 `2`
- 每个用例基线与候选各运行 10 次，报告 wall、CPU 与峰值 RSS 的中位数
- 耗时包含完整 `synth_intel`，不是只测优化 pass

## 结果

| 用例 | 基线逻辑单元 | 候选逻辑单元 | 缩减 | 基线 wall | 候选 wall | 基线 CPU | 候选 CPU | 基线 RSS | 候选 RSS |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| case01 balanced | 406 | 381 | 6.16% | 0.27 s | 0.27 s | 0.26 s | 0.26 s | 26.32 MiB | 27.78 MiB |
| case02 pipeline | 444 | 383 | 13.74% | 0.325 s | 0.29 s | 0.31 s | 0.28 s | 34.72 MiB | 35.35 MiB |
| case03 crossbar | 409 | 319 | 22.00% | 0.30 s | 0.23 s | 0.29 s | 0.22 s | 28.91 MiB | 24.48 MiB |
| case04 feedback | 365 | 354 | 3.01% | 0.26 s | 0.26 s | 0.25 s | 0.25 s | 26.21 MiB | 26.21 MiB |
| case05 nested | 415 | 398 | 4.10% | 0.28 s | 0.28 s | 0.26 s | 0.26 s | 27.18 MiB | 26.92 MiB |

五例 wall 合计从 `1.435 s` 降到 `1.330 s`，约快 `7.3%`；CPU
合计从 `1.37 s` 降到 `1.27 s`，约减少 `7.3%`。最慢单例仍为
`0.29 s`。这些数字只覆盖当前五个小规模用例，不能外推为千级网络结论。

## 算法自身开销

在 `synth_intel` 的同一前置流程之后单独调用 `pmux_opt -v`，测得 pass
主体耗时如下：

| 用例 | `pmux_opt` 耗时 |
|---|---:|
| case01 balanced | 0.121 ms |
| case02 pipeline | 0.357 ms |
| case03 crossbar | 0.246 ms |
| case04 feedback | 0.164 ms |
| case05 nested | 0.164 ms |

算法开销低于 1 ms，完整综合时间主要由 Yosys 和 ABC 的决定性映射阶段
构成。当前实现对 dense 总线位宽设了上限，深度搜索使用共享 memo 和达到
阈值后的提前返回，没有出现指数级扫描。

## 结论

1. 当前策略在五个新例上没有可观测的运行时或内存回归。
2. case02 和 case03 因删除冗余比较器/选择逻辑，完整综合反而更快。
3. case01/04/05 的 wall/CPU 中位数与基线持平；最大 RSS 增量约 1.5 MiB。
4. 后续优化仍应同时记录面积、wall、CPU、峰值 RSS 和 EQY，不能只优化 LUT 数。

原始重复采样见 `repeat10.json`，pass 日志见 `pass-profile/`。
