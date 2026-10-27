# 九例面积达标与通用结构门槛（054）

日期：2026-10-27
分支：`codex/priority-tree-sharing-20261025`
基线提交：`275f486714852fc1cfae356d6769a2c18e53618b`
评测环境：macOS `arm64`，Yosys 官网 `v0.69` commit `9f75ca1f9834…`，
`synth_intel -family cycloneiv`

## 1. 结论

九个指定用例使用同一份核心算法，未读取用例编号、文件名、模块名或固定位宽；九例面积缩减
全部超过 25%，`core` 与 `mapped` 阶段 EQY 均为 9/9 PASS。35 例 Yosys 上游封存集在最终
结构门槛下为 9 例改善、26 例持平、0 例面积/DFF 回退。

| 用例 | 基线 | 当前 | 缩减率 | DFF | core EQY |
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
| **官方四例平均** |  |  | **31.88%** |  | 4/4 |
| **新增五例平均** |  |  | **30.90%** |  | 5/5 |
| **九例平均** |  |  | **31.34%** |  | 9/9 |

权威机器结果：`experiments/nine-cases-20261027/nine-area-eqy.json`。
映射阶段 EQY：`experiments/nine-cases-20261027/nine-mapped-eqy.json`。
统一清单：`experiments/nine-cases-20261027/manifest.json`。
逐例迭代日志：`experiments/nine-cases-20261027/iteration-logs/`。

## 2. 核心算法

每轮依次执行同步复位 DFF、零 mux、小型常量 ROM、常量二次幂移位、成对 PMUX、局部更新、
比较器缩窄和共因子化。优先级链和密集译码器只在第 1 轮做一次全局归一化，避免重复归一化
新生成的 mux 并冻结较差的 ABC 切分。其余消费者型规则重复执行到本轮零变化。

迭代上限由 `pmux_opt -max_iterations <n>` 控制，默认 4。九例第 1 轮变化数为
`17/1/10/11/5/24/10/4/4`，都在第 2 轮零变化后收敛，因此不会进入无限循环。

模块确实发生结构改写时，算法设置 `pmux_opt_restructured`；Cyclone IV 映射读取该 RTLIL
属性，选择 `abc -lut 4 -script pmux_abc.abc`。没有结构改写的模块保持官方
`abc9 -lut 4 -W 300`。属性来自结构结果，不来自模块名或用例名。

## 3. 通用性回归

35 例封存集汇总（`clean-holdout35-area.json`）：

- 改善 9 例，持平 26 例，面积回退 0 例；
- DFF 回退 0 例；
- 逻辑单元合计 `3439→2909`；
- 代表改善：`opt_share_large_pmux_cat` `271→182`，
  `opt_share_large_pmux_multipart` `311→166`。

修复前，`case_large__case_lage_top` 因自定义 ABC 从 `638` 变为 `663`。定位后没有按模块名
特判，而是增加“只有发生过结构改写才切换 ABC”的通用门槛，最终恢复 `638→638`。证据在
`experiments/054-generic-abc-gate-20261027/case_large-gated.stdout`。

## 4. 效率采样

新增五例各交替采样 11 次，候选相对同源基线：

| 用例 | 墙钟比 | CPU 比 | 相对 RSS 比 |
|---|---:|---:|---:|
| case01 | 0.924 | 0.923 | 1.830 |
| case02 | 0.809 | 0.812 | 1.454 |
| case03 | 0.711 | 0.724 | 1.597 |
| case04 | 1.016 | 1.000 | 1.823 |
| case05 | 1.030 | 1.037 | 1.793 |

最差墙钟 `1.030×`、最差 CPU `1.037×`，绝对峰值 RSS 为 `49.5～51.5 MiB`。时间与 CPU
在 1.05 门槛内，但相对 RSS 为 `1.45～1.83×`；如果正式评分比较相对内存，效率项仍有风险，
不能写成“效率已确定满分”。原始数据：`new5-efficiency.json`。

35 例上游封存集中只有一个千级面积用例：`wide_thru_priority`，`1123` 个逻辑单元、
`512` 个 DFF。该例没有触发结构改写，面积保持 `1123→1123`。21 轮交替采样的墙钟、CPU、
RSS 中位比分别为 `1.019×`、`1.021×`、`1.006×`，均在 `1.05×` 内；原始数据：
`experiments/054-generic-abc-gate-20261027/wide-priority-efficiency.json`。

## 5. 构建与哈希

本机构建必须使用工具链自带的 Flex/Bison/Python，否则系统 `flex` 会生成不可编译代码：

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
```

- 当前候选 SHA-256：`379b27bfdddeb0f37c0b21f629169c2bbc8cd4b7026c92e6cddf9e1bcb3a385e`
- `pmux_opt.cc` SHA-256：`e8185fcf3ddf0617c2b81c2efb43394aab37a78cb775204cc107bb611f52606f`
- 官网干净源码副本曾完成全量重建，九例面积与当前候选一致；干净构建二进制 SHA-256
  `455aca22e3eb9fc9c599e465b1ee0fdb6fe45141603653a4dff5496102d056d7`。
- 补丁 dry-run：`patch -p1 -d tools/build/official-v069 --dry-run < patches/yosys-069/pmux_opt-integration.patch`。

## 6. 等价验证补充

`mapped` 首轮运行出现 `dffeas`/`cycloneiv_lcell_comb` 未定义，定位为验证脚本没有加载
Cyclone IV 功能模型，不是逻辑反例。接入仓库已有的 `formal/intel_cycloneiv_sim.v` 后，
九例全部 PASS；失败日志保留在 `nine-mapped-eqy-pre-model.json`，没有覆盖成通过记录。

## 7. 验收边界

当前可确认：九个头文件级指定用例全部超过 25%，core/mapped EQY 均 9/9 PASS，35 例公开
封存集面积 0 回退。当前不能确认：隐藏用例、相对 RSS 满分，以及目标 Windows/eLinx 环境
已复测。PicoRV32 仅证明流程可跑通，尚未完成 EQY。
