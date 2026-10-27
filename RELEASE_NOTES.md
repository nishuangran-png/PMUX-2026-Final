# PMUX 项目成果快照（2026-10-27 · 054）

## 1. 结论

本快照固定 Yosys 官网 `v0.69` commit `9f75ca1f9834a39a863915b5dae0c7b1e33533bc`
上的 PMUX 优化实现。九个指定用例共用同一套核心算法，不读取用例编号、文件名、模块名
或固定位宽；面积缩减全部超过 25%，`core` 与 `mapped` 阶段 EQY 均为 9/9 PASS。

面积口径为官方 `synth_intel -family cycloneiv` 流程输出的
`cycloneiv_lcell_comb + $not`：

| 用例 | 基线 | 当前 | 缩减率 | DFF | core EQY | mapped EQY |
|---|---:|---:|---:|---:|:---:|:---:|
| test1 | 288 | 204 | 29.17% | 222 不变 | PASS | PASS |
| test2 | 25 | 16 | 36.00% | 0 不变 | PASS | PASS |
| test3 | 13 | 9 | 30.77% | 17 不变 | PASS | PASS |
| test4 | 38 | 26 | 31.58% | 10 不变 | PASS | PASS |
| case01 balanced | 406 | 276 | 32.02% | 12 不变 | PASS | PASS |
| case02 pipeline | 444 | 312 | 29.73% | 24 不变 | PASS | PASS |
| case03 crossbar | 409 | 250 | 38.88% | 10 不变 | PASS | PASS |
| case04 feedback | 365 | 266 | 27.12% | 16 不变 | PASS | PASS |
| case05 nested | 415 | 304 | 26.75% | 12 不变 | PASS | PASS |
| **官方四例平均** |  |  | **31.88%** |  | **4/4** | **4/4** |
| **新增五例平均** |  |  | **30.90%** |  | **5/5** | **5/5** |
| **九例平均** |  |  | **31.34%** |  | **9/9** | **9/9** |

单例最低为 `26.75%`，九例 DFF 均未增加，没有通过增加寄存器换取面积缩减。

权威结果：

- 九例面积与 core EQY：`experiments/nine-cases-20261027/nine-area-eqy.json`
- 九例 mapped EQY：`experiments/nine-cases-20261027/nine-mapped-eqy.json`
- 统一清单：`experiments/nine-cases-20261027/manifest.json`
- 逐例迭代日志：`experiments/nine-cases-20261027/iteration-logs/`

## 2. 迭代算法

`pmux_opt` 位于 `synth_intel` coarse 阶段，不依赖插件 `.so` 或按用例特判。算法按轮执行
同步复位 DFF、零 mux、小型常量 ROM、常量二次幂移位、成对 PMUX、局部更新、比较器缩窄
和共因子化。优先级链与密集译码器只在第 1 轮做一次全局归一化，避免反复改写新生成的
mux；其余消费型局部规则重复执行到本轮零变化。

迭代上限由 `pmux_opt -max_iterations <n>` 控制，默认 4。九例第 1 轮变化数为
`17/1/10/11/5/24/10/4/4`，均在**第 2 轮零变化后收敛**，不会进入无限循环。

模块确实发生结构改写时，算法设置 `pmux_opt_restructured` RTLIL 属性，Cyclone IV 映射
据此选择仓库内 ABC 脚本；没有结构改写的模块继续使用官方 `abc9`。该门槛来自结构结果，
不来自模块名或用例名。

`test4` 原先为 0%，原因是 Yosys `proc` 中的 `proc_rom` 先把 32 路常量 `case` 转为
32×10 ROM，`pmux_opt` 到达时已看不到 `$pmux`。当前实现增加“常量 ROM lowering +
延迟 techmap + LUT folding”，再加入有界拆分规划后，官网 9f75 由 `38` 降至 `26`。

## 3. 通用性回归

Yosys `v0.69` 上游公开 RTL 封存集共 12 个文件、35 个顶层模块。最终结构门槛版实测：

- 9 例改善、26 例持平、0 例面积回退；
- DFF 回退 0 例；
- 逻辑单元合计 `3439→2909`。

代表改善为 `opt_share_large_pmux_cat` `271→182` 和
`opt_share_large_pmux_multipart` `311→166`。`case_large__case_lage_top` 曾因自定义
ABC 从 `638` 变为 `663`，最终由“仅结构改写模块切换 ABC”的通用门槛恢复为 `638→638`。

证据：`experiments/054-generic-abc-gate-20261027/clean-holdout35-area.json`。

该 35 例是泛化证据，不是官方成绩。此前版本完成过 35/35 EQY PASS，但本轮锁定版只重跑
面积，不能把历史 35/35 EQY 记为当前版本已重验结论。

## 4. 效率

新增五例各交替采样 11 次，候选相对同源基线：

| 用例 | 墙钟比 | CPU 比 | 相对 RSS 比 |
|---|---:|---:|---:|
| case01 | 0.924 | 0.923 | 1.830 |
| case02 | 0.809 | 0.812 | 1.454 |
| case03 | 0.711 | 0.724 | 1.597 |
| case04 | 1.016 | 1.000 | 1.823 |
| case05 | 1.030 | 1.037 | 1.793 |

最差墙钟 `1.030×`、最差 CPU `1.037×`，绝对峰值 RSS 为 `49.5～51.5 MiB`。时间与 CPU
低于 1.05 门槛，但相对 RSS 为 `1.45～1.83×`；若正式评分比较相对内存，效率项不能写成
“已确定满分”。原始数据：`reports/054-final-20261027/new5-efficiency.json`。

## 5. 固定构件

| 构件 | SHA-256 |
|---|---|
| 当前候选 `tools/build/official-v069-opt/build-current-20261025/yosys` | `379b27bfdddeb0f37c0b21f629169c2bbc8cd4b7026c92e6cddf9e1bcb3a385e` |
| `src/yosys-069/passes/pmux/pmux_opt.cc` | `e8185fcf3ddf0617c2b81c2efb43394aab37a78cb775204cc107bb611f52606f` |
| 官网基线 `tools/build/official-v069/build/yosys` | `143bd6015239d40c4bfa156928a48efe2ea1efd5712d5d232eab8929f31eb91d` |
| `tools/oss-cad-suite/bin/yosys-abc`（ABC 1.01） | `91bc02357beb61ace477cf8d8c0a84541ccb44f224c36dab4a8a6f4db689100f` |

官网干净源码副本曾完成全量重建，九例面积与当前候选一致；干净构建二进制 SHA-256 为
`455aca22e3eb9fc9c599e465b1ee0fdb6fe45141603653a4dff5496102d056d7`。

构建时必须使用工具链自带 Flex/Bison/Python；系统 `/usr/bin/flex` 会生成不可编译代码。

## 6. 快速复现

```bash
export PATH="$PWD/tools/oss-cad-suite/bin:$PATH"
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

python3 scripts/eval_holdout.py \
  --manifest experiments/nine-cases-20261027/manifest.json \
  --json experiments/nine-cases-20261027/nine-area-eqy.json
python3 scripts/eval_holdout.py \
  --manifest experiments/nine-cases-20261027/manifest.json \
  --eqy-stage mapped \
  --json experiments/nine-cases-20261027/nine-mapped-eqy.json
python3 scripts/eval_holdout.py \
  --manifest experiments/holdout-yosys-v069-20261025/manifest.json \
  --json experiments/054-generic-abc-gate-20261027/clean-holdout35-area.json
```

## 7. 验收边界

当前可确认：九个指定用例全部超过 25%，core/mapped EQY 均 9/9 PASS，35 例公开封存集
面积与 DFF 0 回退，算法主体有界迭代且九例均在第 2 轮收敛。

当前不能确认：隐藏用例一定达到 25%，相对 RSS 效率项一定满分，目标 Windows/eLinx
工程与板级环境已复测，PicoRV32 已完成 EQY。详细证据与限制见
`reports/054-final-20261027/README.md` 和 `docs/算法设计文档-pmux_opt.md`。
