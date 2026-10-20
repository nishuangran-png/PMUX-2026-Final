# 013 · 合规骨架跑通：pmux_opt 已编入 yosys 主程序

日期：2026-10-18 中午
状态：✅ 里程碑 M1 完成（官方合规形态）

## 做了什么

1. 新建 `tools/build/yosys-069-src/passes/pmux/pmux_opt.cc`
   - v0 骨架：遍历设计、统计 `$pmux` 单元，暂不做任何改写
2. `passes/CMakeLists.txt` 注册新 pass 目录
3. `techlibs/intel/synth_intel.cc` coarse 阶段（`fsm; opt` 之后）加入 `run("pmux_opt")`
4. 重新编译 → `tools/build/yosys-069-src/build/yosys`

## 验证结果（官方 4 用例，官方流程 `synth_intel -family cycloneiv`）

| 用例 | 抓到的 $pmux | lcell（基线 → 现在） | DFF | $not |
|---|---|---|---|---|
| test1 | 5 | 286 → **286** | 222 | 2 |
| test2 | 0 | 25 → **25** | 0 | 0 |
| test3 | 3 | 11 → **11** | 17 | 1 |
| test4 | 0 | 30 → **30** | 10 | 1 |

- 日志出现 `pmux_opt: found 5 $pmux cell(s)` 等 → 证明 pass 确实在 `synth_intel` 内部被调用
- 结果与官方基线逐项一致 → 骨架无副作用
- 耗时（test1，CPU 时间）：原版 0.22s / 自建版 0.22s —— 无额外开销

## 怎么复现

```bash
tools/build/yosys-069-src/build/yosys -p \
  "read_verilog rtl/official/test1/test1.v; synth_intel -family cycloneiv -top test1; stat"
```

## 下一步

1. 官方口径评测脚本：lcell + $not、每用例百分比再平均、单次 CPU 时间、内存峰值
2. 在 v0 里实现第一个候选改写（test1 的选择网络共享，手工实验 −3.8%）
3. EQY 改造：验证 abc 之后的 `$lut` 网表
