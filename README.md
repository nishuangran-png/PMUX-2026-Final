# 嵌入式 FPGA · PMUX 选题

2026 FPGA 创新设计赛道 · 中科亿海微 · 选题二：多路选择器 PMUX 结构优化。

本仓库保存选题资料、Yosys 0.69 的 PMUX 重构实现、形式等价验证和实验记录。

本轮九例正式结果、上游封存集、复现步骤和已知限制见
`reports/054-final-20261027/README.md` 与 `RELEASE_NOTES.md`。

## 当前状态

当前算法已编入官网 Yosys `v0.69`（commit `9f75ca1f9834…`）主程序的 `synth_intel`
coarse 阶段，不依赖插件加载。九个指定用例共用同一套结构规则和清单驱动评测流程，
不读取用例编号、文件名、模块名或固定位宽。在官方 `synth_intel -family cycloneiv`
流程下：

| 用例 | 基线 `lcell + $not` | 当前 | 缩减率 | DFF | core EQY |
|---|---:|---:|---:|---:|:---:|
| test1 | 288 | 204 | 29.17% | 222 不变 | PASS |
| test2 | 25 | 16 | 36.00% | 0 不变 | PASS |
| test3 | 13 | 9 | 30.77% | 17 不变 | PASS |
| test4 | 38 | 26 | 31.58% | 10 不变 | PASS |
| case01 balanced | 406 | 276 | 32.02% | 12 不变 | PASS |
| case02 pipeline | 444 | 312 | 29.73% | 24 不变 | PASS |
| case03 crossbar | 409 | 250 | 38.88% | 10 不变 | PASS |
| case04 feedback | 365 | 266 | 27.12% | 16 不变 | PASS |
| case05 nested | 415 | 304 | 26.75% | 12 不变 | PASS |
| **官方四例平均** |  |  | **31.88%** |  | **4/4** |
| **新增五例平均** |  |  | **30.90%** |  | **5/5** |
| **九例平均** |  |  | **31.34%** |  | **9/9** |

- 平均百分比按每个用例先算缩减率、再等权平均；单例最低为 `26.75%`，九例全部超过 25%。
- 单元数口径为 `cycloneiv_lcell_comb + $not`；九例 DFF 均未增加。
- EQY 在 `core` 和 `mapped` 两个阶段均为九例 9/9 PASS；mapped 结果见
  `experiments/nine-cases-20261027/nine-mapped-eqy.json`。
- 统一清单为 `experiments/nine-cases-20261027/manifest.json`，完整结果与迭代日志见
  `experiments/nine-cases-20261027/`。
- 主体算法有界迭代最多 4 轮：首轮做一次优先级链/密集译码器全局归一化，之后重复执行
  可由消费者消除的局部规则，直到零变化；九例都在第 2 轮零变化后收敛。
- 只有模块确实发生结构改写时，Cyclone IV 才选择自定义 ABC 脚本；未改写的模块继续使用
  官方 `abc9`。该门槛来自 RTLIL 结构属性，不按用例名特判。
- `test4` 原先的 0% 已定位为覆盖缺口：Yosys `proc` 先执行 `proc_rom`，把 32 路常量
  `case` 转成 32×10 ROM；`pmux_opt` 到达时实际看到 0 个 `$pmux`。当前实现增加
  “常量 ROM lowering + 延迟 techmap + LUT folding”路径，随后加入有界拆分规划，官网
  9f75 复测由 38 降到 26。
  三条改动有顺序依赖，不能把边际收益直接相加，见 `reports/026-消融实验.md`。
- 旧 `ad7ff16f3` 树实际是 `0.69+117`。因此 `27.38%`、`34→27`、`13→10`、`323→316`
  等数字全部退出正式口径，不能再作为官网 v0.69 成绩。
- 既有四题 31 次交替采样的最大墙钟比为 `1.018`、最大 CPU 比为 `1.000`、最大 RSS 比为
  `1.004`，见 `reports/039-efficiency.*`。新增五例 11 次交替采样的最大墙钟比为
  `1.030`、最大 CPU 比为 `1.037`；但相对基线 RSS 达 `1.45～1.83` 倍，若正式评分比较
  相对内存，效率项仍有风险，见 `reports/054-final-20261027/new5-efficiency.json`。
- `priority_encoder_128` 曾出现 `307→315` 回退。最终加入短索引直接输出保护后恢复
  `307→307`，同时保留 512 位编码器和 arbiter 的收益。
- Yosys `v0.69` 官方测试 RTL 封存集共 12 个文件、35 个顶层模块。最终结构门槛版实测为
  9 例改善、26 例持平、0 例面积/DFF 回退，合计 `3439→2909`；此前的 039 版本曾完成
  35/35 EQY PASS，但本轮锁定版只重跑了面积，不能把 35/35 EQY 当作本轮已重验结论。
  面积数据见 `experiments/054-generic-abc-gate-20261027/clean-holdout35-area.json`。
  该集合不计入官方成绩。
- 最新答疑明确会增加效率测试用例；当前九题结果不等于隐藏用例一定通过。

面积、EQY、封存集、效率、构建哈希和限制见
`reports/054-final-20261027/README.md`；算法说明见 `docs/算法设计文档-pmux_opt.md`。

## 这个项目在做什么

Verilog 的 `case`/条件选择会形成常量比较器和 `$pmux`/`$mux`。默认综合流程会把它们展开成
较大的与或网络。本项目在 Yosys 的 RTLIL 层识别可以证明安全的结构，在不改变功能的前提下
减少后续 Cyclone IV LCELL：

1. 两个共享选择网络的 `$pmux` 合并；
2. 标量优先级 `$mux` 链按长度折叠为平衡 mux 树或一个 `$pmux`；
3. 稀疏局部更新 PMUX 退化为连线/单 mux；
4. DFF 使能门控下安全缩窄常量比较器；
5. 相邻码比较与公共使能共因子化；
6. 小型常量 ROM、常量二次幂移位和同步复位路径做结构转换；
7. 上述消费者型规则在有界轮次内反复执行到固定点，再做 LUT 反相器吸收。

算法不读取 test 编号、文件名、模块名、固定位宽或业务常量。规则基于 RTLIL 拓扑和扇出关系；
匹配不完整时保留原电路。九个指定用例只是验证集，不是硬编码分支。

## 复现流程

### 1. 安装或确认工具链

```bash
source tools/oss-cad-suite/environment
tools/build/official-v069/build/yosys -V      # 官网 v0.69 原版
tools/build/official-v069-opt/build-current-20261025/yosys -V  # 官网 v0.69 + pmux_opt
```

### 2. 从干净 Yosys 0.69 源码集成

核心源码是可跟踪的 `src/yosys-069/passes/pmux/`。安装脚本会检查源码 `.gitcommit`
必须是 `9f75ca1f9834…`，再应用主程序集成补丁：

- `passes/CMakeLists.txt`：加入 `add_subdirectory(pmux)`；
- `techlibs/intel/synth_intel.cc`：在 coarse 的 `opt` 之后加入 `run("pmux_opt");`。
- `techlibs/intel/synth_intel.cc`：在 `map_ffram` 中延迟 techmap，保护 ROM lowering 生成的 LUT；
- `techlibs/intel/synth_intel.cc`：在 `map_ffs` 后加入 `run("pmux_opt -fold_luts");`。
- `techlibs/intel/common/ff_map_pn1.v` 与 Intel CMake 资源清单：补齐同步置位 FF 映射。

当前正式候选**不修改** ABC9 输入顺序。结构确实发生改写时，Cyclone IV 映射使用
`pmux_abc.abc` 自定义 ABC 脚本；结构未改写的模块仍使用官方 `abc9`。
`patches/yosys-069/abc9-input-canonicalization.patch` 是独立实验补丁，不属于正式评测口径。

```bash
export PATH="$PWD/tools/oss-cad-suite/bin:$PATH"

# 默认目标为官网 v0.69 优化源码树
bash scripts/install_pmux_069.sh tools/build/official-v069-opt

cmake -S tools/build/official-v069-opt \
  -B tools/build/official-v069-opt/build-current-20261025 \
  -DCMAKE_BUILD_TYPE=Release \
  -DGIT_EXECUTABLE=/usr/bin/false \
  -DYOSYS_ORIGIN_INFO= \
  -DBISON_EXECUTABLE="$PWD/tools/build-tools/bison/bin/bison" \
  -DFLEX_EXECUTABLE="$PWD/tools/build-tools/flex/bin/flex" \
  -DPython3_EXECUTABLE="$PWD/tools/oss-cad-suite/py3bin/python3.11" \
  -DYOSYS_WITHOUT_SLANG=ON
cmake --build tools/build/official-v069-opt/build-current-20261025 --target yosys -j8
```

安装脚本是幂等的；已集成时会比较并更新源码副本，不会重复插入调用。

### 3. 九例与封存集评测

```bash
python3 scripts/eval_official.py --repeat 3 \
  --json reports/039-official-area.json
python3 scripts/eqy_official.py
python3 scripts/eval_efficiency.py --repeat 31 \
  --json reports/039-efficiency.json
python3 scripts/eval_holdout.py \
  --manifest experiments/nine-cases-20261027/manifest.json \
  --json experiments/nine-cases-20261027/nine-area-eqy.json
python3 scripts/eval_holdout.py \
  --manifest experiments/nine-cases-20261027/manifest.json \
  --eqy-stage mapped \
  --json experiments/nine-cases-20261027/nine-mapped-eqy.json
python3 scripts/eval_holdout.py \
  --manifest experiments/holdout-yosys-v069-20261025/manifest.json \
  --json reports/039-holdout-yosys-v069.json
```

`eval_official.py` 同时跑官网 v0.69 原版和官方源码优化版；`eqy_official.py`
对四道原始题把两臂都跑到 `$lut` 网表，再用 SAT 策略做等价验证；`eval_holdout.py`
可对任意清单做相同的 core 阶段 EQY。两者都失败时不要继续调参。

## 目录

```text
fpga比赛/
├── PMUX选题资料/             官方文件与 Yosys 参考源码
├── src/yosys-069/            ★ 可提交的 Yosys 0.69 API 源码副本
│   └── passes/pmux/          pmux_opt 当前算法实现
├── patches/yosys-069/        主程序集成与 Cyclone IV 资源补丁
├── scripts/
│   ├── install_pmux_069.sh   ★ 安装/更新 pass 到 0.69 源码树
│   ├── eval_official.py      官方四题面积对比
│   ├── eval_efficiency.py    墙钟时间与峰值内存采样
│   ├── eqy_official.py       官方四题 EQY 等价验证
│   ├── doctor.sh             本机环境体检
│   └── ...                   用例生成、历史插件实验等辅助脚本
├── rtl/official/             四个官方用例（只读参考）
├── rtl/upstream/yosys-v069/  Yosys 上游公开 RTL 封存输入
├── rtl/{single,mixed,scale}/ 手写与规模化用例
├── reports/                  实验记录（每份都注明测量口径与版本）
└── docs/                     环境、算法、组委会问题清单和工作日志
```

## 关键约束

- 统一使用 Yosys 官网 **0.69 commit `9f75ca1f9834…`** 和工具链自带 ABC；
  发生过结构改写的模块使用仓库内 ABC 脚本，未改写模块使用官方 `abc9`，
  不通过 plugin `.so` 加载算法。
- 计数指标是 Cyclone IV LCELL 加 `$not`；不能用 LUT 数替代 LCELL 数。
- 任何结构改写都先过 EQY；不通过就回退，不能用仿真样例代替形式证明。
- DFF/IO 原则上不得增加；当前九题 DFF 均未增加。
- 小型常量 ROM 转 LUT、延迟 techmap 和 LUT folding 已实现；正式候选不使用
  `autoname; sort` 或第三方综合器。真实千级封存集已有面积数据，但 PicoRV32 尚未完成 EQY。
- 规范、密集、降序的常量比较优先链保留给 ABC9；乱序、非互斥或非完整链才进入分组
  改写。该门槛来自 RTLIL 结构，不读取用例名称。
- 不应为 `test4` 关闭 `proc_rom`，也不应引入按用例、固定位宽或固定信号名写死的分支。

## 历史材料

早期 `plugin/`、`scripts/regress.sh` 和 `scripts/pmux_synth.sh` 是开发阶段的插件实验流程，
仅用于追溯 v1。旧报告中的 7.47% 是 v1 数据；报告 015 的 20.63% 来自 OSS 快照；
`27.38% / 34→27` 来自错误的 `0.69+117` 树，均已撤回。正式评测以报告 054、
`eval_official.py`、`eqy_official.py` 和 `install_pmux_069.sh` 为准。

## 已知限制

1. 新增五例墙钟/CPU 最坏比分别为 `1.030×/1.037×`；绝对 RSS 约 `49.5～51.5 MiB`，
   但相对基线为 `1.45～1.83×`。若正式评测比较相对内存，效率项不能宣称稳过。
2. `test4` 已达到 `31.58%`，但 ROM 路径的收益依赖 lowering、延迟 techmap、LUT folding
   和有界拆分规划的顺序组合，尚不能证明对隐藏 ROM 稳定获得同等收益。
3. 九例 `core` 与 `mapped` EQY 均为 9/9 PASS；目标 eLinx 工程导入和板级环境仍未验证。
4. PicoRV32 的面积改善只有 `0.52%`，且尚未完成 EQY；真实、混合结构的大规模等价证据仍不完整。
5. 35 例上游封存集的最终门槛版为 9 改善、26 持平、0 回退，但只重跑了面积，
   仍需补充公开 RTL、最终映射 EQY 和目标 Windows/eLinx 环境复测，不能宣称泛化已经完成。
