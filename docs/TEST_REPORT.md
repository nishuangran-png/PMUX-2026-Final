# 最终测试报告

## 环境和目标

- 基线：Yosys `0.69+post`，提交
  `d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`
- 基线 SHA256：
  `4A69FB580FC223D395CAF319B63B7C03C61D88AF5540DFE9ECCBBB70014A515F`
- 候选 SHA256：
  `0B68124A65638A13067D72716C337D5A6A0166DA423D7D153225CFF82471FF24`
- ABC SHA256：
  `E7161F996329320AF6DFACF178813DF0EE3C1A6FA82E7CD2216D37A4B7A828A7`
- 目标流程：`synth_intel -family cycloneiv`
- 面积口径：`cycloneiv_lcell_comb + $not`
- 同时检查 DFF 和未映射宽 `$lut`

## 面积

| 用例 | 基线逻辑 | 候选逻辑 | 缩减 | DFF |
|---|---:|---:|---:|---:|
| test1 | 288 | 180 | 37.50% | 222 -> 222 |
| test2 | 25 | 16 | 36.00% | 0 -> 0 |
| test3 | 13 | 10 | 23.08% | 17 -> 17 |
| test4 | 36 | 26 | 27.78% | 10 -> 10 |
| case01 | 426 | 263 | 38.26% | 12 -> 12 |
| case02 | 450 | 375 | 16.67% | 24 -> 24 |
| case03 | 408 | 310 | 24.02% | 10 -> 10 |
| case04 | 368 | 302 | 17.93% | 16 -> 16 |
| case05 | 413 | 353 | 14.53% | 12 -> 12 |

- 官方四题平均缩减：`31.09%`
- 新增五题平均缩减：`22.28%`
- 九题平均缩减：`26.20%`
- 面积回退、DFF 增加、未映射宽 `$lut` 均为 0

## 性能

正式测量不使用高频内存轮询。墙钟从进程启动前开始，到完整退出结束；
峰值内存只在进程退出后读取。每例先预热基线和候选，每轮原版先、
候选后，10 轮使用相同工作目录、同一脚本和同一个 ABC。

队友独立 Windows 审计的 10 轮结果为：

- 九题最大墙钟比：`1.023`
- 九题最大内存比：`1.031`
- 相对 `1.05` 门槛：通过

本包无轮询复测的 10 轮中位数：

- 最大墙钟比：`0.990`
- 最大内存比：`1.033`
- test2 因首轮之后出现机器调度尖峰，按规则补做 5 轮，平均墙钟比
  `0.909`

最终成绩仍以官方评测环境复测为准。旧七轮高频轮询结果已移动到
`logs/performance/legacy-high-frequency-memory-polling/`，不作为
正式结论。

原始数据：

- `logs/performance/raw.csv`
- `logs/performance/summary.csv`
- `logs/performance/summary.json`
- `logs/performance/official-summary.json`
- `logs/performance/recheck-test2/`

## 形式等价

| 用例 | EQY 分区 | 整设计等价 | 最终 |
|---|---|---|---|
| test1 | PASS | 未触发 | PASS |
| test2 | PASS | 未触发 | PASS |
| test3 | PASS | 未触发 | PASS |
| test4 | PASS | 未触发 | PASS |
| case01 | 生成文件路径 ERROR | PASS | PASS* |
| case02 | 生成文件路径 ERROR | PASS | PASS* |
| case03 | 生成文件路径 ERROR | PASS | PASS* |
| case04 | 生成文件路径 ERROR | PASS | PASS* |
| case05 | 生成文件路径 ERROR | PASS | PASS* |

新增五题的 EQY 策略阶段实际报错是生成的分区 `.sv` 文件在相对路径
下找不到。原始失败日志保留在
`logs/eqy/eqy-all.log`；整设计等价使用
`equiv_simple/equiv_induct/equiv_status -assert`，九题全部通过。
