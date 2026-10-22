# 嵌入式 FPGA · PMUX 选题

2026 FPGA 创新设计赛道 · 中科亿海微 · 选题二：多路选择器 PMUX 结构优化。

本仓库保存选题资料、Yosys 0.69 的 PMUX 重构实现、形式等价验证和实验记录。

本轮可上传成果的基线、证据范围和已知限制见 `RELEASE_NOTES.md`。

## 当前状态

v2 算法已编入官网 Yosys `v0.69`（commit `9f75ca1f9`）主程序的 `synth_intel`
coarse 阶段，不依赖插件加载。在官方 `synth_intel -family cycloneiv` 流程下：

| 用例 | 基线 `lcell + $not` | v2 | 缩减率 | DFF | EQY |
|---|---:|---:|---:|---:|:---:|
| test1 | 288 | 202 | 29.86% | 222 不变 | PASS |
| test2 | 25 | 16 | 36.00% | 0 不变 | PASS |
| test3 | 13 | 10 | 23.08% | 17 不变 | PASS |
| test4 | 38 | 35 | 7.89% | 10 不变 | PASS |
| **平均** |  |  | **24.21%** |  |  |

- 平均百分比按每个用例先算缩减率、再对四题等权平均。
- 单元数口径为 `cycloneiv_lcell_comb + $not`。
- 四题在 abc9 后的 `$lut` 网表上均通过 EQY。
- `test4` 原先的 0% 已定位为覆盖缺口：Yosys `proc` 先执行 `proc_rom`，把 32 路常量
  `case` 转成 32×10 ROM；`pmux_opt` 到达时实际看到 0 个 `$pmux`。当前实验分支增加了
  小型常量 ROM 转 LUT 的通用规则，实测由 38 降到 35。完整定位和归因边界见
  `reports/020-test4零收益定位.md`。
- `test4` 的结果对 ABC9 输入顺序敏感。控制实验显示，官方基线仅改变输入排序也可能得到
  35～36，因此当前 7.89% 只能作为工程检查点，不能全部归因于 PMUX/ROM 规则。
- 同源构建的 7 次采样中，四题墙钟时间和峰值 RSS 均不超过原版；最大墙钟比 `1.000`，
  最大内存比 `1.018`，现有四题均处于官方 `1.05` 满分线内。
- `test2` 的 4 级优先级链改为两级优先对加组选择，进入 ABC 前由 52 个门降到 22 个门；
  31 次复测的内存比为 `0.974`，时间比为 `1.000`，见 `reports/019-test2平衡mux树.md`。
- 最新答疑明确会增加效率测试用例；当前四题结果不等于隐藏用例一定通过，见
  `reports/018-最新官方答疑与官网v069复测.md`。

详细数据见 `reports/019-test2平衡mux树.md`，算法说明见
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
tools/build/official-v069-opt/build/yosys -V  # 官网 v0.69 + pmux_opt
```

### 2. 从干净 Yosys 0.69 源码集成

核心源码是可跟踪的 `src/yosys-069/passes/pmux/`。安装脚本修改两处主程序接线，并应用一个
ABC9 输入规范化补丁：

- `passes/CMakeLists.txt`：加入 `add_subdirectory(pmux)`；
- `techlibs/intel/synth_intel.cc`：在 coarse 的 `opt` 之后加入 `run("pmux_opt");`。
- `techlibs/intel/synth_intel.cc`：在 `map_luts` 的 ABC9 调用前加入 `autoname; sort`。

```bash
# 默认目标为官网 v0.69 优化源码树
bash scripts/install_pmux_069.sh tools/build/official-v069-opt

cmake -S tools/build/official-v069-opt -B tools/build/official-v069-opt/build \
  -DCMAKE_BUILD_TYPE=Release
cmake --build tools/build/official-v069-opt/build --target yosys -j8
```

安装脚本是幂等的；已集成时会比较并更新源码副本，不会重复插入调用。

### 3. 官方四题评测

```bash
python3 scripts/eval_official.py --repeat 3 \
  --json reports/018-官网v069面积复测.json
python3 scripts/eqy_official.py
python3 scripts/eval_efficiency.py --repeat 15 \
  --json reports/019-官网v069修复后效率采样.json
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

- 统一使用 Yosys **0.69**；正式流程使用内置 ABC9，不通过 plugin `.so` 加载算法。
- 计数指标是 Cyclone IV LCELL 加 `$not`；不能用 LUT 数替代 LCELL 数。
- 任何结构改写都先过 EQY；不通过就回退，不能用仿真样例代替形式证明。
- DFF/IO 原则上不得增加；当前四题 DFF 均未增加。
- 小型常量 ROM 转 LUT 已实现，但 `autoname; sort` 对 ABC9 结果影响较大；正式交付前须确认
  赛题是否允许修改 `synth_intel.cc` 的映射顺序，并补做隐藏规模与随机结构回归。
- 不应为 `test4` 关闭 `proc_rom`，也不应引入按用例、固定位宽或固定信号名写死的分支。

## 历史材料

早期 `plugin/`、`scripts/regress.sh` 和 `scripts/pmux_synth.sh` 是开发阶段的插件实验流程，
仅用于追溯 v1；正式评测以 `eval_official.py`、`eqy_official.py` 和
`install_pmux_069.sh` 为准。旧报告中的 7.47% 是 v1 数据；报告 015 的 20.63%
来自 OSS 快照，也已被报告 018 的官网 v0.69 结果取代。

## 已知限制

1. 现有四题的时间和峰值内存均已进入满分线；官方新增的效率用例尚未公布，不能据此宣称隐藏用例一定通过。
2. `test4` 已从 38 降至 35，但收益同时受 ROM 规则和 ABC9 输入顺序影响，尚未完成稳定归因；
   这不等于 `test4` 已达到面积最优，也不能外推为隐藏用例稳定获得同等收益。
3. 当前只对 4 级优先级链启用平衡树，其他长度的链仍使用 `$pmux`，需继续检查临时网表膨胀。
4. 四题覆盖的结构已实现为通用拓扑规则，但 `test4` 新增路径仍缺 X 常量、多 ROM、多读写口
   和更大地址空间的专项回归；更大规模、混合逻辑和随机用例仍需持续做 EQY 验证。
