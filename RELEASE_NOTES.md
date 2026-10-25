# PMUX 项目成果快照（2026-10-25 官网 0.69 · 039）

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
| test4 | 38 | 26 | 31.58% | 10 不变 | PASS |
| **平均** |  |  | **32.05%** |  |  |

四个公开用例的单题缩减率均超过 20%。四题 EQY 全部通过；另有 20 个常量 PMUX 矩阵
用例和 4 个官方用例的最终网表验证，合计 **24/24 PASS**。

## 2. 固定版本

| 构件 | SHA-256 | 自报版本 |
|---|---|---|
| `official-v069/build/yosys` | `143bd6015239d40c4bfa156928a48efe2ea1efd5712d5d232eab8929f31eb91d` | `0.69+post (git sha1 9f75ca1f9834…)` |
| `official-v069-opt/build-current-20261025/yosys` | `bef45ff6f6e315ab55cebfdfd42d7c8cfb12878f9df79fcde3c010ce26fedb33` | `0.69+post (git sha1 9f75ca1f9834…)` |
| `src/yosys-069/passes/pmux/pmux_opt.cc` | `beb569869bec888d249f91d991cfdc80b1ea253b8aa0edcc0d9d803d327f6784` | 源码 |
| `tools/oss-cad-suite/bin/yosys-abc` | `91bc02357beb61ace477cf8d8c0a84541ccb44f224c36dab4a8a6f4db689100f` | ABC 1.01 |

两臂使用同一个 ABC，面积命令为
`read_verilog <case>.v; synth_intel -family cycloneiv -top <case>; stat`。

## 3. 本轮修复

- 新增规范优先链结构门槛：同一总线、比较码严格 `N-1..0` 的 `$eq`/全零 `$logic_not`
  链保留给 ABC9；乱序、重复、非互斥和非完整链继续分组。
- 修复上游 RTL 暴露的两条回归：`mux_if_unbal_5_3` 从 `17→20` 恢复为 `17→17`，
  `mux_if_unbal_5_3_width_mismatch` 从 `15→16` 恢复为 `15→15`。
- 同一结构门槛下，`mux_if_unbal_5_3_order` 为 `20→15`，
  `mux_if_unbal_5_3_nonexcl` 为 `16→12`。
- 修正版本口径：安装脚本硬校验 `.gitcommit` 必须为官网 9f75，不再接受 `0.69+117`。
- 修正短索引移位回退：索引宽度小于 8 且解码向量直接驱动顶层输出时跳过改写；
  进入内部布尔逻辑时仍改写。`priority_encoder_128` 由 `307→315` 恢复为 `307→307`。
- 修正一位 LUT 映射：Yosys 0.69 会把所有宽度 1 `$lut` 映射成反相，最终在 RTLIL
  层直接消除一位恒等/反相 LUT。
- 回归覆盖 17 个移位解码边界，17/17 PASS。

## 4. 泛化证据

Yosys `v0.69` 上游 RTL 封存集（桌面 `rtl.zip`，SHA-256
`b15a8540b09822c96d5b50dbab685f762cf43e65694b6295f71b09b86dc3c772`）：

- 12 个源文件、35 个顶层模块；
- 35/35 面积流程完成、35/35 默认 EQY PASS；
- 33 持平、2 改善、0 面积/DFF 回退；
- 合计 `3439→3430`，DFF `512→512`。

这 35 例是泛化证据，不是官方成绩。33 例持平说明当前门槛仍偏保守，不能宣称普遍收益。

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
| priority encoder 512 | 1346 | 1219 | 9.44% | 默认 FAIL / 完整证明 PASS |
| arbiter 64 | 172 | 163 | 5.23% | PASS |
| PicoRV32 | 2108 | 2097 | 0.52% | SKIP |

优先级编码器 512 位的默认逐位分区仍记录为 FAIL。本轮从当前候选重建 LUT 网表后，
`equiv_make` + `equiv_simple -nogroup` 完整证明 PASS（`1346→1219`，46.18 秒），
完整 SAT miter 也返回 `SUCCESS`；机器 JSON 中的默认 FAIL 不改写。PicoRV32 未完成 EQY，
其面积结果只作探索，不作为大规模等价证明。

## 5. 效率

31 次交替采样，最大墙钟比 `1.018×`、最大 CPU 比 `1.000×`、最大峰值 RSS 比 `1.004×`。
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
python3 scripts/eval_holdout.py \
  --manifest experiments/holdout-yosys-v069-20261025/manifest.json \
  --json reports/039-holdout-yosys-v069.json
python3 scripts/verify_shift_decode_cases.py
```

## 7. 待办与边界

1. `test4` 已达到 `31.58%`，但 ROM 路径仍需在更多真实表结构上验证。
2. PicoRV32 尚未完成 EQY，真实千级混合结构证据仍不完整。
3. 上游 35 例中仍有 33 例持平，仍需补充公开 RTL 和更多拓扑。
4. 隐藏效率用例、Windows/eLinx 流程不在本轮核心算法结论内。

完整证据索引见 `reports/039-上游封存集与规范优先链门槛-20261025.md` 与
`reports/039-manifest.sha256`。
