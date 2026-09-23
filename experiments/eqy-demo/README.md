# EQY 等价验证演示

用一条最小例子说明「形式等价验证」到底在做什么，以及它对本选题为什么是生死线。

## 这个演示在做什么

同一个 8 选 1 电路，两份**结构完全不同**的实现：

| 文件 | 实现方式 | 说明 |
| --- | --- | --- |
| `gold.v` | `case (sel)` | 基准实现，会生成 `$pmux` |
| `gate_ok.v` | 嵌套三目运算符 | 显式 mux 树，功能与 gold 完全一致 |
| `gate_bad.v` | 同 gate_ok | **但把 d3 和 d5 调换了**，功能已错 |

EQY 分别对比 `gold vs gate_ok` 和 `gold vs gate_bad`。

## 运行方式

```powershell
. H:\fpga2026\tools\oss-cad-suite\environment.ps1
$env:PYTHONUTF8 = "1"          # 中文 Windows 必须加，否则 .eqy 里的中文注释会报编码错
cd H:\fpga2026\experiments\eqy-demo
eqy eq_ok.eqy
eqy eq_bad.eqy
```

## 实测结果

**正确的实现——证明通过：**

```
** Trying induction with length 1 **
Base case for induction length 1 proven.
[induction step 1] Solving problem with 5904 variables and 15786 clauses..
Induction step proven: SUCCESS!
```

**写错的实现——被抓住，并给出反例：**

```
** Trying induction with length 1 **
[base case 1] Solving problem with 2953 variables and 7893 clauses..
SAT temporal induction proof finished - model found for base case: FAIL!
```

工具还能导出一条 `trace.vcd` 波形，就是那条能让两份电路输出不一致的具体输入。

## 原理：它在证明什么

EQY 不是跑仿真试几个输入，而是这样做的：

1. 把两份网表拼在一起，构造一个「**miter**」电路——它的输出是「两份电路结果不一致」这个信号
2. 用 **SAT 求解器 + 时序归纳法**（`sat -tempinduct`）证明：这个「不一致」信号**永远不可能**变成 1
3. 对**所有可能的输入组合**都成立，所以是数学证明，不是抽查

核心命令就一句（来自生成的 `run.ys`）：

```
sat -tempinduct -set-init-undef -set-def-formal -set-def-inputs \
    -maxsteps 5 -set-assumes -prove-asserts -show-public miter
```

## 对本选题的意义

官方评分细则：**存在任意一个测试用例不等价，功能正确性 0 分，其余各项一律不计分**。

所以这套流程必须**在写优化算法之前**就跑通，每次改代码都跑一遍。它是安全带，不是最后补的检查。

## Windows 上的两个坑（已踩）

1. **EQY 需要 GNU make**。`gold`/`gate`/`combine`/`partition` 四步都能过，最后一步
   `make -f strategies.mk` 会失败，因为本机没装 make（Git for Windows 自带 bash，但没有 make）。
   上面的结论是绕过 make 直接跑 `run.sh` 得到的。要全自动，需要装 make 或转到 WSL。
2. **`.eqy` 里的中文注释会触发 GBK 解码错误**。EQY 用系统默认编码读配置文件，
   中文 Windows 下必须设 `PYTHONUTF8=1`。
