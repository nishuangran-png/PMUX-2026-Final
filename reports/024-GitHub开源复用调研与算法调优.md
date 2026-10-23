# 024 · GitHub 开源复用调研与 PMUX 算法调优

日期：2026-10-23  
分支：`codex/test4-rom-lowering-20261023`  
基线：Yosys 0.69，`synth_intel -family cycloneiv`，ABC9  
当前生产候选：双映射 ABC9（未在本报告中更换）

## 一、结论先行

本轮搜索了 GitHub 上与 Yosys、ABC、LUT 映射、PMUX/MUX 重构和 FPGA 综合相关的
仍在维护的项目。结论是：

1. **没有发现可以直接嵌入 Yosys 0.69 `synth_intel` 的成熟开源实现。**
2. **Wildebeest 最相关，但它是 Yosys 插件和自研 FPGA 架构流程，不是一个可搬进
   `synth_intel` 的 PMUX pass，也没有为 Cyclone IV 的面积口径提供现成实现。**
3. **eqmap、mockturtle、also、phyLS、JARL 等要么绑定其他 Yosys 版本、要么只提供
   研究框架/近似映射，不能直接替代当前双映射。**
4. **继续叠加 ABC 脚本的收益不足以抵消泛化和效率风险。** 已经测过 `flow3_k4` 和
   “串行跑两套流程取较优”，后者虽然能少 338 个 LUT，但 P95 时间约 1.59 倍，不能接受。
5. 当前生产候选仍是：官方四题 `288→180`、`25→16`、`13→10`、`38→26`，平均
   **32.04%**。这组数字不因本次调研而变化。

本轮唯一值得留作后续研究的现象是：只对“最多 3 个选择分支”的小 PMUX 调用
`pmux2shiftx`，在 103 个泛化样本上得到 **5 个更好、98 个持平、0 个更差，合计少
42 个 LUT**。但官方四题没有额外收益，而且这个门槛是在同一批 103 个样本上观察到的，
还不能当作已证明泛化的生产规则。

## 二、搜索范围与筛选标准

搜索方向包括：

- `yosys pmux`
- `priority mux logic synthesis`
- `LUT mapping yosys`
- `AIG optimization abc`
- `e-graph LUT mapping`
- `Boolean matching FPGA synthesis`
- `technology mapping FPGA synthesis`
- `yosys plugin`、`RTLIL`、`Yosys optimizer`

筛选时只接受同时满足以下条件的实现：

- 许可证允许在本项目中使用；
- 维护状态可确认，不是多年未更新的实验快照；
- 能处理 Yosys 0.69 的 RTLIL，或以独立二进制/脚本形式接入；
- 目标是精确等价，而不是近似综合；
- 对 Cyclone IV LUT4/ABC9 面积有直接意义；
- 不会引入外部 `.so` 插件、外部 ABC 二进制或不可控的新依赖。

按这个标准，绝大多数搜索结果只是 Yosys 使用示例、数据集、物理布线工具、RTL 生成器
或与 MUX 无关的工程。

## 三、项目清单与复用判断

### 3.1 综合/逻辑网络项目

| 项目 | 许可证 | 最近维护迹象 | 能借鉴什么 | 为什么不能直接复用 |
|---|---|---|---|---|
| [YosysHQ/yosys](https://github.com/YosysHQ/yosys) | ISC | 官方主线持续维护 | RTLIL、`synth_intel`、`pmux2shiftx`、`opt_muxtree` 等正式实现 | 赛题固定 0.69；对照 0.69 到当前 main 没有可直接回移的 PMUX 面积补丁，升级本身也不合规 |
| [berkeley-abc/abc](https://github.com/berkeley-abc/abc) | 加州大学伯克利分校宽松许可 | 官方主线持续维护 | ABC9/AIG 优化、LUT4 映射和 `snMux.h` 路径共享思路 | 当前 0.69 已使用官方 ABC 逻辑；`snMux` 依赖 ABC 的 SN word-level 表示，不能直接作为 ABC9 script；整包替换会改变基线且无法证明效率/等价风险 |
| [zeroasiccorp/wildebeest](https://github.com/zeroasiccorp/wildebeest) | Apache-2.0 | 本地提交 2026-09-24；仓库近期仍有推送 | LUT4 ABC 脚本、面积模式、寄存器 MUX 路径共享思路 | 是独立 Yosys 插件/自研 `synth_fpga` 流程，主要面向 Platypus；官方测试版本不是 0.69；插件形式和架构绑定不符合本项目交付方式 |
| [cornell-zhang/eqmap](https://github.com/cornell-zhang/eqmap) | Apache-2.0 | 2026-08 左右 | e-graph 做 LUT 重映射，适合作为图级重写的研究参考 | 绑定 Yosys 0.33 和扁平单 bit 顶层；接入 0.69 + `synth_intel` 需要重做前端、层次和映射接口 |
| [lsils/mockturtle](https://github.com/lsils/mockturtle) | MIT | 2026-10；本地 master 有近期提交 | k-LUT 网络、逻辑重写、代价模型 | 是 C++ 逻辑网络库，不是 Yosys pass；需要先做 RTLIL ↔ mockturtle 的双向转换，转换和验证成本很高 |
| [nbulsi/also](https://github.com/nbulsi/also) | MIT | 2026-08 左右 | 研究型逻辑综合框架 | 不是面向 Cyclone IV 的精确 LUT4 映射器，接入成本和风险都高 |
| [panhomyoung/phyLS](https://github.com/panhomyoung/phyLS) | MIT | 2026-04/10 | 研究型逻辑综合 | 没有可直接调用的 Yosys 0.69 后端，也没有本项目所需的面积口径 |
| [Coloquinte/quaigh](https://github.com/Coloquinte/quaigh) | MIT/Apache-2.0 | 2026-02/10 | LUT 映射研究实现 | README 明确说明当前效果还不如 Yosys，映射流程也未完成；不适合作为生产候选 |
| [plex1/aig-opt](https://github.com/plex1/aig-opt) | LGPL-3.0 | 2026-04 左右 | AIG 优化算法参考 | 纯 Python，性能和集成方式都不适合隐藏效率用例；LGPL 还需单独评估交付边界 |
| [qmkkk/JARL](https://github.com/qmkkk/JARL) | MIT | 2026-10 左右 | 学习型/近似 LUT 映射研究 | 面向近似 LUT6/Vivado 方向，不能保证精确 Cyclone IV 面积 |
| [sharc-lab/Potluck](https://github.com/sharc-lab/Potluck) | Apache-2.0 | 2026-10 左右 | HLS 模块共享 | 与 PMUX 组合逻辑综合无关 |
| [marcelwa/aigverse](https://github.com/marcelwa/aigverse) | MIT | 2026-10 左右 | Python AIG 操作/分析 | 没有 Cyclone IV 映射后端；把 RTLIL 整体搬到 Python 会破坏效率目标 |
| [scale-lab/DRiLLS](https://github.com/scale-lab/DRiLLS) | BSD-3-Clause | 仍在维护 | 强化学习搜索 ABC 脚本参数 | 只提供研究型脚本搜索，不是精确 pass；训练/推理开销和可复现性都不适合本赛题 |
| [Nozidoali/lut-synth](https://github.com/Nozidoali/lut-synth) | MIT | 2026-09-15 提交 | XAG/AND 代价重综合、精确综合思路 | 面向量子 LUT/XAG，不是 Cyclone IV LUT4；精确模式只适合小规模，接入会引入新的表示转换 |

### 3.2 更适合作为测试集，而不是算法实现的项目

| 项目 | 许可证 | 用途 | 判断 |
|---|---|---|---|
| [zeroasiccorp/logikbench](https://github.com/zeroasiccorp/logikbench) | MIT | 公开 RTL/FPGA 基准，含 `mux`、`muxcase`、`muxhot`、`muxpri` | 可用于泛化测试和外部回归；不能把它的 LUT 结果直接当作本赛题官方成绩 |
| [lsils/benchmarks](https://github.com/lsils/benchmarks) | MIT | EPFL 等逻辑综合基准 | 可用于面积/等价回归，但大多是纯组合基准，覆盖不到本项目的 FSM、ROM、DFF 形态 |

本地对 LogikBench 的 `mux`、`muxcase`、`muxhot`、`muxpri` 做了 80 组参数扫描，
在 Cyclone IV 流程下给当前候选额外加入完整的 `pmux2shiftx`，结果是 80 组全部持平，
DFF 也没有变化。它更适合作为“没有收益也不能退化”的独立回归样本，而不是证明某个
开关有效。

## 四、上游 Yosys/ABC 与 Wildebeest 的关键差异

### 4.1 Yosys 0.69 到 0.71-dev

对照本地 `yosys-0.69` 与 `yosys-main` 源码，`muxpack`、`opt_muxtree`、
`pmux2shiftx`、`pmuxtree` 的核心逻辑没有可直接回移的面积优化补丁。
`muxcover` 的近期变化主要是逻辑环修复，`synth_intel` 的变化主要是流程顺序和 target
注册，不是 PMUX 面积算法。

因此“升级 Yosys”或“抄一个上游 commit”不能解决本项目的问题；而且本赛题要求固定
0.69，不能通过升级规避。

### 4.2 ABC master 的 word-level MUX 优化

ABC master 的 `src/base/sn/snMux.h` 有 word-level 的 mux-path sharing 和
`@opt_mux` 相关实现，主要针对 register-fed MUX 树、嵌套 PMUX 的路径共享。这和本项目的
“成对 PMUX/重复选择路径”问题在思想上最接近。

但它依赖 ABC 的 SN word-level 表示。Yosys 0.69 的 ABC9 流程在进入 ABC 前主要处理
门级/AIG 形态，不能把 `snMux.h` 直接作为 ABC9 script 调用；要利用它，必须在 Yosys
侧新增 pass，或者维护 SN 表示并在映射前/后建立等价接口。这不是“拿一个开源文件替换
当前脚本”的工作量。

### 4.3 Wildebeest 的 LUT4 脚本

Wildebeest 的 `src/abc_scripts/LUT4/BEST/` 包含
`tiny_area_lut4.scr`、`small_area_lut4.scr`、`area_lut4.scr` 等多组实验脚本，
确实比 Yosys 0.69 默认 ABC9 脚本复杂，使用了 `&synch2`、`&lf`、`mfs2`、
`&satlut`、`lutpack` 等操作。

但它们是 Wildebeest 自有架构和流程的一部分，不是可直接插入
`abc9 -script` 的单条命令；并且这些脚本的实验性质很强，运行时间和收敛性没有为本项目
的 1.05 效率线做过验证。把它们整套搬入会同时引入时间、内存和可复现性风险。

作为方向性验证，完整 Wildebeest LUT4 流程在官方四题上得到
`249 / 15 / 11 / 24` 个 `$lut`。这是 Wildebeest 自有架构下的单元，不能与赛题的
`cycloneiv_lcell_comb + $not` 直接同比；但它已经说明该流程在 `test1` 上明显不适合
直接迁移。进一步只摘取 `&synch2`、`&satlut`、`&lf` 等命令接回当前 ABC9，也没有得到
优于当前双映射的结果（见 5.5）。

## 五、本轮可复现实测

### 5.1 官方四题基线未改变

当前候选的双映射结果为：

| 用例 | 原版 `LCELL + $not` | 当前候选 | 缩减率 | DFF |
|---|---:|---:|---:|---:|
| test1 | 288 | 180 | 37.50% | 222 |
| test2 | 25 | 16 | 36.00% | 0 |
| test3 | 13 | 10 | 23.08% | 17 |
| test4 | 38 | 26 | 31.58% | 10 |
| **平均** |  |  | **32.04%** |  |

数据来源：`reports/024-official-area.json`。候选二进制为
`tools/build/official-v069-test4-20261023/build-clean/yosys`；旧的
`tools/build/official-v069-opt/build/yosys` 是早一版构建，不能用于复测本表。

### 5.2 在 103 个泛化 RTL 上插入 Yosys 自带 pass

样本为 `fuzz`、`mixed`、`scale`、`realistic`、`official` 和 `reference`，
共 103 个 RTL 文件；修正 `reference` 顶层名后全部有效。每个方案都在
`synth_intel -run begin:map_bram` 之后、`map_bram:check` 之前插入，
面积口径仍是 `cycloneiv_lcell_comb + $not`。

| 方案 | 总面积变化 | 更好/持平/更差 | 结论 |
|---|---:|---:|---|
| 当前双映射 | 0 | 0/103/0 | 生产基线 |
| `pmuxtree` | **+222** | 34/38/31 | 明显退化，拒绝 |
| `opt_muxtree` | **0** | 0/103/0 | 无收益，只增加开销，拒绝 |
| `pmux2shiftx` | **-3** | 34/46/23 | 总面积几乎不变但 23 个退化，拒绝 |
| `pmuxtree + opt_muxtree` | **+222** | 34/38/31 | 与 `pmuxtree` 相同，拒绝 |

`pmux2shiftx` 的退化不是小幅噪声，例如：

- `fuzz_060`：372 → 439
- `scale_multi6`：248 → 288
- `fuzz_034`：88 → 128
- `test1`：180 → 201

因此不能因为它在 34 个样本上有效就全局启用。

数据来源：`reports/024-github-pass-variants-103.json`；汇总值以
`reports/024-实验证据摘要.json` 中重算的 103 例结果为准。

### 5.3 更严格的 `pmux2shiftx` 参数没有解决问题

在官方四题上比较了 `-min_density 60/75/90`、`-onehot ignore/pmux` 和
`-norange`。`test1` 的结果始终为 **199 LCELL + 2 `$not` = 201**，
与默认 `pmux2shiftx` 相同；其他三题也都没有改善。

原因是 `test1` 中被转换的是一个 16 选 1、密度 100% 的 PMUX，密度参数无法排除它。
所以“调高 `min_density`”不是有效调优方向。

### 5.4 一个受控的局部候选：只转换 3 选择分支

从同一批 103 个样本的结构特征中发现：如果模块内所有 `$pmux` 的最大选择数不超过 3，
调用 `pmux2shiftx` 在这批样本上没有退化：

| 指标 | 结果 |
|---|---:|
| 应用模块数 | 17 |
| 更好 | 5 |
| 持平 | 98 |
| 更差 | 0 |
| 总面积变化 | **-42 LUT** |
| 官方四题变化 | 0 |

5 个有效案例是：

- `fuzz_077`：49 → 33
- `fuzz_010`：28 → 17
- `pmux_nested`：50 → 41
- `fuzz_037`：20 → 17
- `fuzz_066`：36 → 33

这个结果只说明“可能存在一个低风险的局部门槛”，还不能直接写进生产流程：

- 特征门槛是在观察这 103 个样本后选出的，存在选择偏差；
- 官方四题没有新增收益，不能提高当前成绩；
- 还没有在 LogikBench 的默认/参数化 mux 用例上得到独立验证；
- 还没有完成 0.69 干净重建、EQY 和长时间效率采样。

如果继续做，建议把它实现为**新的内部 pass 或上游 pass 的受控选项**，先只对
“选择分支数 ≤3”的组做转换，并保持默认关闭；在没有独立回归通过前，不进入生产候选。

### 5.5 从 Wildebeest 摘出的 ABC 后处理没有收益

为了排除“只是整套流程不兼容”的解释，又把 Wildebeest LUT4 脚本中的代表性命令拆出来，
直接接到当前 Yosys 0.69 + ABC9 的映射阶段。官方四题结果如下（数值为
`LCELL + $not`）：

| 变体 | test1 | test2 | test3 | test4 | 相对当前候选 |
|---|---:|---:|---:|---:|---:|
| 当前双映射 | 180 | 16 | 10 | 26 | 0 |
| 映射后 `&satlut` | 180 | 16 | 10 | 26 | 0 |
| `&satlut -r` + `&dch` + `&satlut` | 180 | 16 | 11 | 26 | +1 |
| `&synch2` + `&satlut` | 180 | 16 | 11 | 26 | +1 |
| `&lf -K 4 -e` | 202 | 16 | 10 | 26 | +22 |
| `&lf` + `&satlut` | 202 | 16 | 10 | 26 | +22 |
| `&sopb` + `&fraig` 后接双映射 | 180 | 16 | 11 | 26 | +1 |

所有变体都没有降低官方四题面积，部分还让 `test3` 或 `test1` 退化，DFF 均未变化。
因此 Wildebeest 中没有可以低成本摘用的“单条提升命令”；真正有价值的部分需要重做
word-level MUX 或 LUT 重综合接口，属于新的内部 pass，而不是流程拼接。

### 5.6 已经拒绝的更大流程改动

| 方案 | 泛化面积 | 时间风险 | 判断 |
|---|---:|---|---|
| `flow3_k4` | 总面积比基线多 24 | P95 约 1.36×，最大约 1.73× | 面积退化，拒绝 |
| 串行跑两套流程取较优 | 总面积少 338 | P95 约 1.59×，最大约 1.96× | 效率不满足，拒绝 |

这也说明：**不能用少量泛化样本的面积收益，换取隐藏效率用例的超时/内存风险。**

### 5.7 `muxcover` 的“55 LCELL”是口径假收益

在同一候选上，把 `muxcover -nodecode` 插到 `map_luts` 之前，`test1` 的
`cycloneiv_lcell_comb + $not` 会从 180 突然变成 57。但这并不是有效的面积缩减：
网表里同时留下了 24 个 `$_MUX8_`，而它们没有被 `cycloneiv_lcell_comb + $not`
这个口径计数。把宽 MUX 再用 `techmap -map +/simcells.v` 展开后，四题又回到
`180 / 16 / 10 / 26`，没有收益。

因此 `muxcover` 不能作为候选；如果后续有人只看 `stat` 的 LCELL 数，必须同时检查
`$_MUX4_`、`$_MUX8_`、`$_MUX16_` 是否为 0。证据见
`reports/024-muxcover-probe.json`。

## 六、为什么本轮不直接引入开源代码

### 6.1 接口不匹配

当前算法在 Yosys 内部、`synth_intel` 的 coarse/map 阶段运行，输入是 RTLIL，
输出必须继续进入 Intel 的 DFF/BRAM/LUT 映射。Wildebeest、eqmap、mockturtle
都要求另一种中间表示或另一套映射流程；转换层本身就可能抵消收益。

### 6.2 目标不一致

eqmap、mockturtle、JARL、lut-synth 更关注通用 LUT 网络、LUT6、近似综合或量子
网络。本赛题的目标是 Intel Cyclone IV 的 `cycloneiv_lcell_comb + $not`，
还要通过 EQY，不能拿“LUT 网络上的理论优化”直接换算成比赛成绩。

### 6.3 效率线很硬

当前官方四题的耗时和内存已经明显低于 1.05 倍门槛，但隐藏用例可能是千级节点。
串行双流程、Python 图算法、全设计 e-graph 重写、插件加载都可能把 P95 推向 1.5 倍以上。
因此“多跑一套再选优”不是稳妥的工程方案。

### 6.4 许可证和交付边界

Wildebeest（Apache-2.0）、eqmap（Apache-2.0）、mockturtle（MIT）本身允许复用，
但本项目还要求主程序内可复现、不能依赖外部 `.so` 插件或外部 ABC。即使许可证允许，
交付形态和构建可复现性仍然不满足要求。

## 七、建议的下一步

### 优先级 A：保持当前生产候选

先保留双映射 ABC9，不因官方四题的多 1～2 个 LUT 去换泛化集上的大面积退化。
当前阶段最重要的不是继续堆脚本，而是保证隐藏效率用例和 0.69 干净重建。

### 优先级 B：验证“3 选择分支”门槛

独立的 LogikBench 参数扫描已经完成，80 组全部持平，没有发现额外收益或退化。因此这条
规则目前只有“在 103 例中不退化”的事后证据，没有独立增益证据。若仍要继续，按以下顺序：

1. 将 103 个样本按文件/随机种子拆分，观察“调参集有效、留出集也有效”；
2. 对有效案例跑 EQY，并检查 DFF 不增加；
3. 串行交替采样时间/内存，P95 和最大值都必须 ≤1.05；
4. 只有在独立留出集也没有退化时，才考虑写进 `synth_intel`。

由于官方四题没有新增收益、独立 LogikBench 也没有增益，当前默认建议是**不集成**，
把精力放在图级 PMUX 重写和隐藏用例鲁棒性上。

### 优先级 C：图级 PMUX 重写而不是脚本拼接

比 `pmux2shiftx` 更有希望的方向是新增一个 Yosys 内部 pass：

- 识别嵌套 PMUX、重复数据端、选择信号共享和常量/无效分支；
- 用局部 LUT4 支持集和扇出代价估计重写收益；
- 只做可证明不劣化或严格减少 LCELL 的改写；
- 默认保留密集 16 选 1 PMUX，避免把本来适合 PMUX 的结构转成 shiftx；
- 所有改写都输出可审计的决策统计，便于 EQY 和泛化回归。

这比“把 Wildebeest 整套流程搬进来”更符合当前代码结构和赛题约束。

## 八、复现资料

本轮证据已经归档到 `reports/`：

- `reports/024-github-pass-variants-103.json`
  - 103 个文件 × 5 个方案的面积和耗时；
- `reports/024-pmux-pre-features-103.json`
  - 转换前每个用例的 PMUX 数量、最大数据宽度、最大选择宽度等结构特征；
- `reports/024-pmux2shiftx-small3-103.json`
  - 只对最大选择数 ≤3 的模块启用 `pmux2shiftx` 的 103 例结果。
- `reports/024-logikbench-mux-grid.json`
  - LogikBench 四个 MUX 基准的 80 组 `DW/N` 参数扫描；
- `reports/024-wildebeest-inspired-abc.json`
  - Wildebeest ABC 命令拆解后的官方四题结果；
- `reports/024-muxcover-probe.json`
  - `muxcover` 留下宽 MUX 造成的假收益，以及展开后的对照；
- `reports/024-official-area.json`、`reports/024-official-efficiency.json`
  - 当前生产候选的面积和效率证据；
- `reports/024-实验证据摘要.json`
  - 统一摘要和重算结果。

说明：`reports/024-github-pass-variants-103.json` 是原始实验输出，其顶层 `summary`
字段是在 4 个 reference 用例修正顶层名之前生成的，仍写着 99 例和 4 个错误；实际
103 行全部有效。以 `reports/024-实验证据摘要.json` 中重算的 103 例汇总为准。

## 九、一句话汇报口径

> 我检索了 GitHub 上仍在维护的 Yosys/ABC/LUT 映射项目。没有发现能直接嵌入
> Yosys 0.69 和 Cyclone IV `synth_intel` 的现成 PMUX 算法；Wildebeest 和 eqmap
> 只能提供算法思想，不能直接替换当前流程。实测把 `pmuxtree`、`opt_muxtree`、
> `pmux2shiftx` 接到当前流程，分别在泛化集上出现 +222、0、-3 个 LUT，但
> `pmux2shiftx` 有 23 个用例退化，不能采用；LogikBench 的 80 组独立参数扫描也全部
> 持平。当前仍保留双映射候选，下一步应做图级 PMUX 重写，而不是继续堆开源 ABC 脚本。
