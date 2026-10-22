# PMUX 项目成果快照（2026-10-23 实验性检查点）

## 1. 固化范围

本快照对应 pmux_opt v2 在官网 Yosys v0.69 上验证 `test4` 常量 ROM 路径的阶段性闭环，
包含：

- `pmux_opt` 算法源码；
- `synth_intel` 主流程集成补丁与安装脚本；
- 小型常量 ROM 转 LUT 规则与 ABC9 输入规范化补丁；
- 官方四题、规模化用例、随机用例与参考用例；
- 面积、效率、EQY 评测脚本和结果数据；
- 算法设计文档、环境文档和阶段实验记录。

不包含 `tools/`、`build/`、编译产物、临时综合目录和会话中生成的非正式压测文件。

## 2. 固定基线

- 综合器：Yosys 官网 tag `v0.69`，commit `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`
- 目标流程：`synth_intel -family cycloneiv`
- 映射：ABC9
- 面积口径：`cycloneiv_lcell_comb + $not`
- 缩减率：每个用例先计算，再对四个用例等权平均
- 等价性：原版和优化版都运行到 ABC9 后的 `$lut` 网表，再用 EQY 验证

## 3. 当前结果

| 用例 | 原版 LCELL | 优化版 | 缩减率 | DFF | EQY |
|---|---:|---:|---:|---:|:---:|
| test1 | 288 | 202 | 29.86% | 222 不变 | PASS |
| test2 | 25 | 16 | 36.00% | 0 不变 | PASS |
| test3 | 13 | 10 | 23.08% | 17 不变 | PASS |
| test4 | 38 | 35 | 7.89% | 10 不变 | PASS |
| **平均** |  |  | **24.21%** |  |  |

本次冻结前再次执行单轮面积复测，四题结果与上表一致；EQY 四题全部 PASS。

`test4` 的源结构是 32 路常量 `case`，Yosys 默认 `proc` 先由 `proc_rom` 将其转换为
只读 `$mem_v2` ROM，再由 `proc_mux` 处理剩余选择，因此原规则在 `pmux_opt` 处看到
`0 $pmux cell(s)`。当前分支增加了“小型、无写口、全初始化、组合读 ROM”转 LUT 的通用规则，
并把紧随其后的常量使能 mux 一并吸收。实测面积由 38 降到 35。

这个结果不能全部宣传为 PMUX 规则收益：控制实验表明 ABC9 输入顺序同样会改变映射结果。
因此本检查点只确认路径可行，稳定归因和泛化结论仍待后续验证。完整记录见
`reports/020-test4零收益定位.md`。

## 4. 效率口径

现有四个公开小用例在 7 次同源交替采样中：

- 最大墙钟时间比：`1.000×`
- 最大峰值 RSS 比：`1.018×`
- 官方满分条件：每例时间和峰值内存均不超过原版 `1.05×`

这些数字只说明公开四题没有明显回退，不构成大规模效率或隐藏用例通过的证明。四题耗时仅约
`0.08—0.24 s`，采样分辨率约 `10 ms`，不能把 `1.000×` 解释为精确相等或性能优势。

会话中曾生成 `/tmp/scale_real1k.v` 做临时压力检查，但它是高度重复的 4 级 MUX 链，不是官方
用例，也没有完成 EQY，未纳入本快照，不能作为泛化证据。

## 5. 快速复现

```bash
# 1. 准备官网 Yosys 0.69 源码树和 OSS CAD Suite
bash scripts/install_pmux_069.sh tools/build/official-v069-opt

# 2. 构建优化版 Yosys
cmake -S tools/build/official-v069-opt -B tools/build/official-v069-opt/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build tools/build/official-v069-opt/build --target yosys -j8

# 3. 面积与形式等价
python3 scripts/eval_official.py --repeat 3
python3 scripts/eqy_official.py

# 4. 效率采样
python3 scripts/eval_efficiency.py --repeat 15
```

完整的安装与依赖说明见 `README.md` 和 `docs/Mac环境搭建.md`。

## 6. 已知限制

1. 当前只对恰好 4 级的优先级 MUX 链使用平衡树；更长链仍可能生成较大 `$pmux`，需要继续检查时间和内存。
2. 官方新增效率用例尚未公布，现有四题结果不能外推为最终满分。
3. Windows 原生 `yosys.exe` 和 PowerShell `Measure-Command` 正式口径仍需复测。
4. 真实、混合结构的 1000+ cell 留出集尚未固化，当前不能宣称一般电路泛化已经完成。
5. `test4` 的 ROM 规则仍是实验性路径；ABC9 输入顺序会显著影响结果，正式交付前必须确认流程
   修改许可，并补齐 X 常量、多 ROM、多端口、更大地址空间和随机结构回归。

## 7. 证据索引

- 算法设计：`docs/算法设计文档-pmux_opt.md`
- 官方复测：`reports/018-最新官方答疑与官网v069复测.md`
- test2 内存修复：`reports/019-test2平衡mux树.md`
- test4 定位与阶段修复：`reports/020-test4零收益定位.md`
- test4 阶段面积原始数据：`reports/020-test4阶段修复-面积.json`
- test4 阶段效率原始数据：`reports/020-test4阶段修复-效率采样.json`
- 面积原始数据：`reports/019-官网v069修复后面积.json`
- 效率原始数据：`reports/019-官网v069修复后效率采样.json`
- 核心源码：`src/yosys-069/passes/pmux/pmux_opt.cc`
- 集成补丁：`patches/yosys-069/pmux_opt-integration.patch`
- ABC9 输入规范化补丁：`patches/yosys-069/abc9-input-canonicalization.patch`
