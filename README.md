# 嵌入式 FPGA · PMUX 选题

2026 FPGA 创新设计赛道 · 中科亿海微 · 选题二：多路选择器 PMUX 结构优化。

本仓库保存选题资料、Yosys 0.69 的 PMUX 重构实现、形式等价验证和实验记录。

本轮正式结果、版本纠错、复现步骤和已知限制见
`reports/036-官网0.69纠错与最终验证-20261025.md` 与 `RELEASE_NOTES.md`。

## 当前状态

v2 算法已编入官网 Yosys `v0.69`（commit `9f75ca1f9834…`）主程序的 `synth_intel`
coarse 阶段，不依赖插件加载。在官方 `synth_intel -family cycloneiv` 流程下：

| 用例 | 基线 `lcell + $not` | v2 | 缩减率 | DFF | EQY |
|---|---:|---:|---:|---:|:---:|
| test1 | 288 | 202 | 29.86% | 222 不变 | PASS |
| test2 | 25 | 16 | 36.00% | 0 不变 | PASS |
| test3 | 13 | 9 | 30.77% | 17 不变 | PASS |
| test4 | 38 | 32 | 15.79% | 10 不变 | PASS |
| **平均** |  |  | **28.10%** |  |  |

- 平均百分比按每个用例先算缩减率、再对四题等权平均。
- 单元数口径为 `cycloneiv_lcell_comb + $not`。
- 四题在 abc9 后的 `$lut` 网表上均通过 EQY；最终网表验证合计 24/24 通过。
- **test4 仍低于单题 20%；当前只能说平均超过 20%，不能说四题全部过线。**
- `test4` 原先的 0% 已定位为覆盖缺口：Yosys `proc` 先执行 `proc_rom`，把 32 路常量
  `case` 转成 32×10 ROM；`pmux_opt` 到达时实际看到 0 个 `$pmux`。当前实现增加
  “常量 ROM lowering + 延迟 techmap + LUT folding”路径，官网 9f75 复测由 38 降到 32。
  三条改动有顺序依赖，不能把边际收益直接相加，见 `reports/026-消融实验.md`。
- 旧 `ad7ff16f3` 树实际是 `0.69+117`。因此 `27.38%`、`34→27`、`13→10`、`323→316`
  等数字全部退出正式口径，不能再作为官网 v0.69 成绩。
- 31 次交替采样中，最大墙钟比 `1.008`、最大 CPU 比 `1.000`、最大 RSS 比 `0.988`，
  四题均低于官方 `1.05` 满分线，见 `reports/036-official069-final-efficiency.*`。
- `priority_encoder_128` 曾出现 `307→315` 回退。最终加入短索引直接输出保护后恢复
  `307→307`，同时保留 512 位编码器和 arbiter 的收益。
- 最新答疑明确会增加效率测试用例；当前四题结果不等于隐藏用例一定通过。

面积、EQY、最终网表、封存集和效率数据见
`reports/036-官网0.69纠错与最终验证-20261025.md`；算法说明见
`docs/算法设计文档-pmux_opt.md`。

## 这个项目在做什么

Verilog 的 `case`/条件选择会形成常量比较器和 `$pmux`/`$mux`。默认综合流程会把它们展开成
较大的与或网络。本项目在 Yosys 的 RTLIL 层识别可以证明安全的结构，在不改变功能的前提下
减少后续 Cyclone IV LCELL：

1. 两个共享选择网络的 `$pmux` 合并；
2. 标量优先级 `$mux` 链按长度折叠为平衡 mux 树或一个 `$pmux`；
3. 稀疏局部更新 PMUX 退化为连线/单 mux；
4. DFF 使能门控下安全缩窄常量比较器；
5. 相邻码比较与公共使能共因子化。

算法不读取 test 编号、文件名、模块名、固定位宽或业务常量。规则基于 RTLIL 拓扑和扇出关系；
匹配不完整时保留原电路。四个官方用例只是验证集，不是硬编码分支。

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

当前正式候选**不修改** ABC9 输入顺序。`patches/yosys-069/abc9-input-canonicalization.patch`
是独立实验补丁，不属于正式评测口径。

```bash
# 默认目标为官网 v0.69 优化源码树
bash scripts/install_pmux_069.sh tools/build/official-v069-opt

cmake -S tools/build/official-v069-opt \
  -B tools/build/official-v069-opt/build-current-20261025 \
  -DCMAKE_BUILD_TYPE=Release \
  -DGIT_EXECUTABLE=/usr/bin/false \
  -DYOSYS_ORIGIN_INFO=
cmake --build tools/build/official-v069-opt/build-current-20261025 --target yosys -j8
```

安装脚本是幂等的；已集成时会比较并更新源码副本，不会重复插入调用。

### 3. 官方四题评测

```bash
python3 scripts/eval_official.py --repeat 3 \
  --json reports/036-official069-final-area.json
python3 scripts/eqy_official.py
python3 scripts/eval_efficiency.py --repeat 31 \
  --json reports/036-official069-final-efficiency.json
```

`eval_official.py` 同时跑官网 v0.69 原版和官方源码优化版；`eqy_official.py`
把两臂都跑到 abc9 后的 `$lut` 网表，再用 SAT 策略做等价验证。两者都失败时不要继续调参。

## 目录

```text
fpga比赛/
├── PMUX选题资料/             官方文件与 Yosys 参考源码
├── src/yosys-069/            ★ 可提交的 Yosys 0.69 API 源码副本
│   └── passes/pmux/          pmux_opt v2
├── patches/yosys-069/        主程序集成补丁（两处接线）
├── scripts/
│   ├── install_pmux_069.sh   ★ 安装/更新 pass 到 0.69 源码树
│   ├── eval_official.py      官方四题面积对比
│   ├── eval_efficiency.py    墙钟时间与峰值内存采样
│   ├── eqy_official.py       官方四题 EQY 等价验证
│   ├── doctor.sh             本机环境体检
│   └── ...                   用例生成、历史插件实验等辅助脚本
├── rtl/official/             四个官方用例（只读参考）
├── rtl/{single,mixed,scale}/ 手写与规模化用例
├── reports/                  实验记录（每份都注明测量口径与版本）
└── docs/                     环境、算法、组委会问题清单和工作日志
```

## 关键约束

- 统一使用 Yosys 官网 **0.69 commit `9f75ca1f9834…`**；正式流程使用内置 ABC9，
  不通过 plugin `.so` 加载算法。
- 计数指标是 Cyclone IV LCELL 加 `$not`；不能用 LUT 数替代 LCELL 数。
- 任何结构改写都先过 EQY；不通过就回退，不能用仿真样例代替形式证明。
- DFF/IO 原则上不得增加；当前四题 DFF 均未增加。
- 小型常量 ROM 转 LUT、延迟 techmap 和 LUT folding 已实现；正式候选不使用
  `autoname; sort` 或外部 ABC。真实千级封存集已有面积数据，但 PicoRV32 尚未完成 EQY。
- 不应为 `test4` 关闭 `proc_rom`，也不应引入按用例、固定位宽或固定信号名写死的分支。

## 历史材料

早期 `plugin/`、`scripts/regress.sh` 和 `scripts/pmux_synth.sh` 是开发阶段的插件实验流程，
仅用于追溯 v1。旧报告中的 7.47% 是 v1 数据；报告 015 的 20.63% 来自 OSS 快照；
`27.38% / 34→27` 来自错误的 `0.69+117` 树，均已撤回。正式评测以报告 036、
`eval_official.py`、`eqy_official.py` 和 `install_pmux_069.sh` 为准。

## 已知限制

1. 现有四题的时间和峰值内存均进入公开口径满分线；隐藏效率用例尚未公布，不能外推。
2. `test4` 为 `15.79%`，仍低于单题 20%；ROM 路径的收益依赖 lowering、延迟 techmap 和
   LUT folding 的顺序组合，尚不能证明对隐藏用例稳定获得同等收益。
3. 当前只对 4 级优先级链启用平衡树，其他长度的链仍使用 `$pmux`，需继续检查临时网表膨胀。
4. PicoRV32 的面积改善只有 `0.52%`，且尚未完成 EQY；真实、混合结构的大规模等价证据仍不完整。
5. 封存集目前 9 例，仍需继续补充公开 RTL、长优先级链和宽解码组合，不能宣称泛化已经完成。
