# PMUX 测试用例

用于摸底实验的 Verilog 用例。分成两类，对应官方指南里的「单级 PMUX」和「混合组合逻辑 PMUX」。

## 目录

```
rtl/
├── single/   单级 PMUX：一层选择，控制信号干净
│   ├── pmux_basic.v       8 路并行选择，最典型的 $pmux 结构（基准用例）
│   ├── pmux_priority.v    case (1'b1) 写法：退化成带优先级的 $mux 树（对照）
│   ├── pmux_onehot.v      选择信号由前面的组合逻辑算出来
│   └── mux_tree_8to1.v    8 选 1 二叉树，产生 $mux 树——作为对照
└── mixed/    混合组合逻辑 PMUX：选择条件不是裸信号
    ├── pmux_in_alu.v      选择分支里是加法/减法/与/比较器的输出
    └── pmux_nested.v      内层 PMUX 的输出又接到外层 PMUX
```

## 每个用例的顶层模块名

模块名与文件名一致，便于脚本批量跑：`pmux_basic`、`pmux_priority`、`pmux_onehot`、
`mux_tree_8to1`、`pmux_in_alu`、`pmux_nested`。

## ⚠️ 写法决定一切（实测结论）

只有 **`case (sel)` 多分支对常量比较** 才会生成 `$pmux`。写成 `case (1'b1)` + `sel[i]`
会退化成带优先级的 `$mux` 树，单元数反而更高。详细数据和原因见
[reports/001-环境搭建与首个基线.md](../reports/001-环境搭建与首个基线.md)。

## 用途

1. 跑默认流程，记录 `stat` 的单元总数 → baseline
2. 逐个禁用上游 mux 相关 pass，看单元数各涨多少 → 现有 pass 吃掉了多少空间
3. 用 EQY 对比优化前后网表 → 等价性

注意：这些用例是**我们自己按官方描述造的**，官方用例集本体目前没拿到。等组委会下发后，直接替换输入即可，脚本不用改。
