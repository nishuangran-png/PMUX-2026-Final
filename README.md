# PMUX-2026-Final

> **状态**：冻结交付版 `v1.0.0-final`（2026-11-02 冻结）<br>
> **目标流程**：Yosys `synth_intel -family cycloneiv`<br>
> **结论**：9/9 用例面积下降，平均缩减 `26.20%`；面积回退 `0`，DFF 增加 `0`，形式等价 9/9 通过。

本项目面向 2026 年 FPGA 竞赛选题二，在 Yosys 的 Intel Cyclone IV
综合流程中加入 PMUX 结构识别与重写，在不增加 DFF、不使用 legacy ABC
的前提下降低 `cycloneiv_lcell_comb + $not` 面积。

本 README 同时用于**可直接运行的提交 ZIP**和**GitHub 源码审阅仓库**。
两者的唯一权威运行产物、外层 ZIP 大小与 SHA-256、允许的差异边界，见
[`SUBMISSION_ARTIFACTS.md`](https://github.com/nishuangran-png/PMUX-2026-Final/blob/main/SUBMISSION_ARTIFACTS.md)。

## 一分钟结论

- 官方四题平均缩减 **31.09%**，新增五题平均缩减 **22.28%**，九题平均 **26.20%**。
- 九题面积全部低于基线，DFF 全部未增加，最终网表没有未映射宽 `$lut`。
- `test1`—`test4` 为 EQY 原生分区 `PASS`；新增五题为整设计等价 `PASS*`。
- Windows 独立十轮审计最大墙钟比 **1.023**、最大内存比 **1.031**，低于 `1.05` 门槛。
- 规则按 RTLIL 结构触发，不读取用例名、文件名、固定位宽或节点数，也不按样例切换 ABC 脚本。

## 结果

面积口径为 `cycloneiv_lcell_comb + $not`，DFF 为 `dffeas`。数据来自冻结
Windows 包中的 `logs/synthesis/stats.csv` 和对应原始日志。

| 用例 | 基线 LCELL | 基线 `$not` | 基线总计 | 候选 LCELL | 候选 `$not` | 候选总计 | 缩减 | DFF |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| test1 | 286 | 2 | 288 | 178 | 2 | 180 | 37.50% | 222 -> 222 |
| test2 | 25 | 0 | 25 | 16 | 0 | 16 | 36.00% | 0 -> 0 |
| test3 | 12 | 1 | 13 | 10 | 0 | 10 | 23.08% | 17 -> 17 |
| test4 | 35 | 1 | 36 | 25 | 1 | 26 | 27.78% | 10 -> 10 |
| balanced | 426 | 0 | 426 | 263 | 0 | 263 | 38.26% | 12 -> 12 |
| pipeline | 450 | 0 | 450 | 375 | 0 | 375 | 16.67% | 24 -> 24 |
| crossbar | 408 | 0 | 408 | 310 | 0 | 310 | 24.02% | 10 -> 10 |
| feedback | 367 | 1 | 368 | 301 | 1 | 302 | 17.93% | 16 -> 16 |
| nested | 413 | 0 | 413 | 353 | 0 | 353 | 14.53% | 12 -> 12 |

- 官方四题平均缩减：**31.09%**。
- 新增五题平均缩减：**22.28%**。
- 九题平均缩减：**26.20%**。
- 官方赛题按九个用例缩减率的平均值计算面积成绩，不要求每个用例单独达到 20%；最终评分仍以组委会评测环境为准。

## 技术路线

1. **识别**：在 `synth_intel` 中扫描 `$pmux`、优先级 MUX 链和译码选择结构，恢复“选择信号—数据输入—消费者”的关系。
2. **结构重写**：按结构规则做 MUX 树重构、共享译码、稀疏部分更新合并和因子化；二维矩阵转置只作用于满足完整行列覆盖与互斥条件的规则矩阵。
3. **打通 test4**：处理 `proc_rom` 提前吞掉常量 PMUX 的顺序问题，加入常量 ROM 降 LUT、延迟 techmap 和 LUT folding。
4. **统一下游映射**：九题使用同一个 `techlibs/intel/cycloneiv/pmux_abc9.abc`；不使用 legacy ABC，不在 ABC 后重构 `$lut`。
5. **安全边界**：形式等价、DFF 不增加、面积不回退三条同时成立才接受改写，宁可持平也不为单题引入回归。

完整算法、识别条件与设计取舍见
[`docs/ALGORITHM_AND_METHOD.md`](docs/ALGORITHM_AND_METHOD.md) 和
[`docs/DEVELOPMENT_TIMELINE.md`](https://github.com/nishuangran-png/PMUX-2026-Final/blob/main/docs/DEVELOPMENT_TIMELINE.md)。

## 性能

正式性能数据在 Windows 上采集，使用同一目录、同一综合脚本和同一个 ABC；
墙钟区间内不轮询内存，峰值工作内存在进程退出后读取。

- 队友独立审计的 10 轮复测：九题最大墙钟比 **1.023**，最大内存比 **1.031**。
- 本包无轮询复测：最差墙钟比中位数 **0.990**，最差内存比中位数 **1.033**。
- `test2` 为短用例，波动较大：10 轮均值比 `1.199`、中位数比 `0.990`；按允许的短用例口径补做 5 轮后，均值比 `0.909`。

最终效率成绩以组委会评测环境复测为准。原始数据位于
`logs/performance/`；旧的高频内存轮询数据单独保留，不作为最终结论。

## 形式等价

- `test1`—`test4`：EQY 原生分区验证 **PASS**。
- `balanced`—`nested`：Windows 分区策略因生成 `.sv` 的相对路径错误返回 ERROR，原始失败日志保留；随后用整设计等价流程验证，五题均为 **PASS\***。
- 不能把新增五题表述为“原生 EQY PASS”。

详细结果见 `logs/eqy/SUMMARY.md` 和 `logs/eqy/result.json`。

## 合规边界

- 官方基线提交：`d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`。
- 基线与候选使用同一个 Yosys 源码版本、Release 参数和同一个 ABC。
- ABC SHA-256：`E7161F996329320AF6DFACF178813DF0EE3C1A6FA82E7CD2216D37A4B7A828A7`。
- 九题统一使用 `techlibs/intel/cycloneiv/pmux_abc9.abc`。
- 不使用 legacy ABC；`abc9.verify` 保持默认开启。
- ABC 后、`map_cells` 前没有 `$lut` 结构重构；`kernel/io.cc`、`passes/techmap/abc9_exe.cc` 与官方源码一致。

完整说明见 `COMPLIANCE.md`。最终二进制、ABC 和构建身份以
`FINAL_VERSION.json` 为准。

## 包、仓库与 Release

| 入口 | 内容 | 使用方式 |
|---|---|---|
| 提交 ZIP | 基线、候选、可执行文件、运行库、源码、九题、全部日志和证据 | 组委会提交与离线复现 |
| GitHub 仓库 | 源码、补丁、用例、脱敏日志、性能采样、EQY 结果和说明文档 | 在线审阅与逐文件追溯 |
| GitHub Release | 与提交 ZIP 字节一致的唯一运行包 | 下载完整可执行产物 |

为避免与 Git 历史争用空间，仓库不跟踪 `baseline/`、`candidate/` 中的大体积
Windows 二进制；它们完整存在于提交 ZIP 和 Release 附件中。仓库日志仅把
Windows 本机目录替换为 `%LOCAL_HOME%`，不改变任何数值或结论。

外层 ZIP 的文件大小、SHA-256、Release 附件身份和差异边界统一记录在
[`SUBMISSION_ARTIFACTS.md`](https://github.com/nishuangran-png/PMUX-2026-Final/blob/main/SUBMISSION_ARTIFACTS.md)；
包内各文件以 `MANIFEST.sha256` 为准。

## 目录结构

```text
baseline/                 官方基线可执行文件、ABC、运行库与 share/
candidate/                最终候选可执行文件、ABC、运行库与 share/
cases/                    官方四题与新增五题
docs/                     测试报告、算法说明和开发时间线
environment/              构建环境、工具链版本与哈希
hashes/                   用例与冻结二进制 SHA-256
logs/synthesis/           18 份基线与候选综合日志及 stats.csv
logs/eqy/                 EQY、整设计等价日志和结果
logs/performance/         最终性能原始采样与汇总
scripts/                  冒烟、综合、性能、EQY 与验收脚本
source/final/             最终源码覆盖层与 EQY 形式模型
source/patches/           相对官方提交的统一补丁
source/upstream/          官方源码及依赖子模块归档（若有）
```

GitHub 仓库不包含 `baseline/`、`candidate/` 大文件，其余审阅入口相同。

## 复现

从提交 ZIP 解压后，在包根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\smoke_runtime.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run_synthesis.ps1 -PythonExe python
```

统一综合命令：

```text
read_verilog <case>.v
synth_intel -family cycloneiv -top <top>
stat
```

从源码重新构建时，先取得同一个 ABC 可执行文件，再运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\source\build_pair_clean.ps1 `
  -AbcExe .\candidate\yosys-abc.exe `
  -ToolchainBin C:\msys64\mingw64\bin
```

## 开发过程与证据

项目不是在交付前一次性生成，而是从可测基线开始，经历结构重写、`test4`
根因定位、五题泛化、统一 ABC9 和 Windows 冻结验证逐步收敛：

| 阶段 | 时间 | 关键节点 |
|---|---|---|
| 01 建立基线 | 2026-09-22 — 09-24 | Yosys 0.68 初测，确认 `$pmux` 成因，统一面积口径 |
| 02 结构重写 | 2026-10-13 — 10-21 | `pmux_opt` 接入 `synth_intel`，官方四题形成等价改写 |
| 03 打通 test4 | 2026-10-23 — 10-25 | 定位 `proc_rom` 顺序依赖，加入常量 ROM lowering 与 LUT folding |
| 04 泛化审计 | 2026-10-26 — 10-28 | 新增五题上验证共享译码、稀疏更新、因子化和二维矩阵转置 |
| 05 统一 ABC9 | 2026-10-28 — 10-30 | A/B/C 同源消融，九题统一一个 ABC 脚本，清除 `nested` 回退 |
| 06 冻结交付 | 2026-10-30 — 11-03 | Windows 十轮复测、9/9 形式等价、源码与发布物哈希冻结 |

完整阶段说明、关键纠错和证据索引见
[`docs/DEVELOPMENT_TIMELINE.md`](https://github.com/nishuangran-png/PMUX-2026-Final/blob/main/docs/DEVELOPMENT_TIMELINE.md)。

项目从 **2026-09-22** 启动，冻结前形成 **31 次真实非合并提交**（9 月 12 次、
10 月 19 次）。完整日期、SHA 与提交说明见
[`DEVELOPMENT_LOG.md`](https://github.com/nishuangran-png/PMUX-2026-Final/blob/main/DEVELOPMENT_LOG.md)。

冻结包的公开整理只做路径脱敏和文档补充，不改变 RTL、源码补丁、综合日志、
性能采样、EQY 结论或 `FINAL_VERSION.json`。评分复现请以 `README.md`、
`FINAL_VERSION.json` 和 GitHub Release `v1.0.0-final` 为准。

## 第三方组件

Yosys 及其上游组件按 ISC 许可证发布。许可证文本和第三方声明见
[`LICENSES/`](https://github.com/nishuangran-png/PMUX-2026-Final/tree/main/LICENSES) 与
[`THIRD_PARTY_NOTICES.md`](https://github.com/nishuangran-png/PMUX-2026-Final/blob/main/THIRD_PARTY_NOTICES.md)。
