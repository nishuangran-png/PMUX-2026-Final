# PMUX 项目成果快照（2026-10-20）

## 1. 固化范围

本快照对应 pmux_opt v2 在官网 Yosys v0.69 上的第一轮完整闭环，包含：

- `pmux_opt` 算法源码；
- `synth_intel` 主流程集成补丁与安装脚本；
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
| test4 | 38 | 38 | 0.00% | 10 不变 | PASS |
| **平均** |  |  | **22.23%** |  |  |

本次打包前再次执行单轮面积复测，四题结果与上表一致；EQY 四题全部 PASS。

`test4` 的源结构是 32 路常量 `case`，经 `proc_rom` 转换为只读 `$mem_v2` ROM。
当前规则不对 ROM 做猜测性展开，因此保持中性；这不是算法失败，也不代表 ROM 没有优化空间。

## 4. 效率口径

现有四个公开小用例在 15 次同源交替采样中：

- 最大墙钟时间比：`1.000×`
- 最大峰值 RSS 比：`1.002×`
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
5. `test4` 的 ROM 成本模型属于后续 P2 工作，只有在证明 LCELL 严格下降且效率不回退时才应启用。

## 7. 证据索引

- 算法设计：`docs/算法设计文档-pmux_opt.md`
- 官方复测：`reports/018-最新官方答疑与官网v069复测.md`
- test2 内存修复：`reports/019-test2平衡mux树.md`
- 面积原始数据：`reports/019-官网v069修复后面积.json`
- 效率原始数据：`reports/019-官网v069修复后效率采样.json`
- 核心源码：`src/yosys-069/passes/pmux/pmux_opt.cc`
- 集成补丁：`patches/yosys-069/pmux_opt-integration.patch`
