# PMUX 优化方法

## 识别与重构

`pmux_opt` 在 `synth_intel` 中识别 `$pmux`、优先级 mux 链、稀疏
部分更新、常量 ROM、共享译码器和二维矩阵式选择器。选择端必须可
证明为常量编码或共享选择总线，否则保持不变。

主体重构包括：

1. 规则编码 PMUX 改写为 bus-controlled mux tree。
2. 合并互逆选择编码，并对共享选择总线矩阵进行转置。
3. 优先级 mux 链规范化。
4. 稀疏部分更新 PMUX 分支消除。
5. 相邻比较器和公共使能项合并。
6. 常量 ROM 和小选择器生成四输入 LUT。
7. 延迟 LUT 只在 ABC 前按内部名称精确展开。
8. 固定点迭代到没有结构变化。

## ABC

九题统一使用
`techlibs/intel/cycloneiv/pmux_abc9.abc`：

```text
&scorr
&sweep
&dc2
&dch -f -r
&if -W 300 -a -v
```

脚本 SHA256：
`79AFD96467E5EA234CC0C4D076E9D162661E6035AE17088083961BCCCFA629DD`。

不按用例名、节点数、DFF 数或结构切换脚本。

## 合规边界

- 未修改 `abc9.cc`、`abc.cc` 或 legacy ABC。
- `abc9_exe.cc` 和 `io.cc` 与官方源码完全一致。
- `abc9.verify` 保持默认开启。
- ABC 后、`map_cells` 前没有 `$lut` 重构。
- 仅 PMUX 算法、`synth_intel` 调用和同目录 ABC 查找属于最终改动。

## 形式验证

gold/gate 均在 ABC 后、`map_cells` 前输出真实 `$lut` RTLIL。官方
四题使用原生 EQY 分区 PASS。新增五题的 Windows EQY 分区策略因
相对路径下的 `.sv` 文件找不到而报 ERROR；失败日志保留后，改用整
设计等价流程：

```text
equiv_make
proc
async2sync
opt -fast
setundef -zero -init -undriven
equiv_simple
equiv_induct
equiv_status -assert
```

`pmux_opt` 生成的 `dffeas` 由
`source/final/dffeas_formal.v` 建模。九题全部证明通过。
