# PMUX 项目成果快照（2026-10-25 官网 0.69 最终验证）

## 1. 结论

本快照固定 Yosys 官网 `v0.69` commit `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`
上的 PMUX 优化实现。旧 `ad7ff16f3` 树是 `0.69+117`，其 `27.38%`、`34→27`、
`13→10` 等结果已全部退出正式口径。

官网四题面积（`cycloneiv_lcell_comb + $not`）：

| 用例 | 基线 | 候选 | 缩减率 | DFF | EQY |
|---|---:|---:|---:|---:|:---:|
| test1 | 288 | 202 | 29.86% | 222 不变 | PASS |
| test2 | 25 | 16 | 36.00% | 0 不变 | PASS |
| test3 | 13 | 9 | 30.77% | 17 不变 | PASS |
| test4 | 38 | 32 | 15.79% | 10 不变 | PASS |
| **平均** |  |  | **28.10%** |  |  |

`test4` 仍未达到单题 20%，不能表述为四题全部过线。四题 EQY 全部通过；另有
20 个常量 PMUX 矩阵用例和 4 个官方用例的最终网表验证，合计 **24/24 PASS**。

## 2. 固定版本

| 构件 | SHA-256 | 自报版本 |
|---|---|---|
| `official-v069/build/yosys` | `143bd6015239d40c4bfa156928a48efe2ea1efd5712d5d232eab8929f31eb91d` | `0.69+post (git sha1 9f75ca1f9834…)` |
| `official-v069-opt/build-current-20261025/yosys` | `2fa898af248f9709b9995ee9c2511ffe410084a7b5a890905f7b53adae050037` | `0.69+post (git sha1 9f75ca1f9834…)` |
| `tools/oss-cad-suite/bin/yosys-abc` | `91bc02357beb61ace477cf8d8c0a84541ccb44f224c36dab4a8a6f4db689100f` | ABC 1.01 |

两臂使用同一个 ABC，面积命令为
`read_verilog <case>.v; synth_intel -family cycloneiv -top <case>; stat`。

## 3. 本轮修复

- 修正版本口径：安装脚本硬校验 `.gitcommit` 必须为官网 9f75，不再接受 `0.69+117`。
- 修正短索引移位回退：索引宽度小于 8 且解码向量直接驱动顶层输出时跳过改写；
  进入内部布尔逻辑时仍改写。`priority_encoder_128` 由 `307→315` 恢复为 `307→307`。
- 修正一位 LUT 映射：Yosys 0.69 会把所有宽度 1 `$lut` 映射成反相，最终在 RTLIL
  层直接消除一位恒等/反相 LUT。
- 回归覆盖 17 个移位解码边界，17/17 PASS。

## 4. 泛化证据

20 个常量 PMUX 尺寸矩阵用例：改善 3、持平 17、回退 0，合计 `277→231`（16.61%），
全部通过 SAT miter 或门级仿真。

9 个封存用例全部保留：

| 用例 | 基线 | 候选 | 缩减率 | EQY |
|---|---:|---:|---:|:---:|
| priority chain 5 | 96 | 66 | 31.25% | PASS |
| priority chain 6 | 112 | 83 | 25.89% | PASS |
| priority chain 8 | 160 | 114 | 28.75% | PASS |
| priority chain 16 | 336 | 255 | 24.11% | PASS |
| priority encoder 16 | 34 | 34 | 0.00% | PASS |
| priority encoder 128 | 307 | 307 | 0.00% | PASS |
| priority encoder 512 | 1346 | 1219 | 9.44% | PASS |
| arbiter 64 | 172 | 163 | 5.23% | PASS |
| PicoRV32 | 2108 | 2097 | 0.52% | SKIP |

优先级编码器 128/512 位的 EQY 使用全部输出同分区；默认逐位分区存在部分定义假反例，
边界说明见 `experiments/holdout-eqy-grouped-20261025/README.md`。PicoRV32 未完成 EQY，
其面积结果只作探索，不作为大规模等价证明。

## 5. 效率

31 次交替采样，最大墙钟比 `1.008×`、最大 CPU 比 `1.000×`、最大峰值 RSS 比 `0.988×`。
四个公开用例均低于 `1.05×` 满分线，且没有用例超过 `1.15×` 归零线。该结论仅适用于
当前四个公开用例，不能外推隐藏效率用例。

## 6. 快速复现

```bash
bash scripts/install_pmux_069.sh tools/build/official-v069-opt
cmake -S tools/build/official-v069-opt \
  -B tools/build/official-v069-opt/build-current-20261025 \
  -DCMAKE_BUILD_TYPE=Release \
  -DGIT_EXECUTABLE=/usr/bin/false \
  -DYOSYS_ORIGIN_INFO=
cmake --build tools/build/official-v069-opt/build-current-20261025 --target yosys -j8

python3 scripts/eval_official.py --repeat 3
python3 scripts/eqy_official.py
python3 scripts/eval_efficiency.py --repeat 31
python3 scripts/verify_shift_decode_cases.py
```

## 7. 待办与边界

1. `test4` 仍为 `15.79%`，需要继续分析 ROM/LUT 路径。
2. PicoRV32 尚未完成 EQY，真实千级混合结构证据仍不完整。
3. 封存集只有 9 例，仍需补充公开 RTL 和更多拓扑。
4. 隐藏效率用例、Windows/eLinx 流程不在本轮核心算法结论内。

完整证据索引见 `reports/036-官网0.69纠错与最终验证-20261025.md`。
