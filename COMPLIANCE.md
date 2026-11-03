# 合规说明

- 基线提交：`d0e71cfb7bcafe2b437f3edc1789b69e99ecd55a`。
- 候选与基线使用相同的 Yosys 源码版本、Release 参数和同一个 ABC。
- `kernel/io.cc`、`passes/techmap/abc9_exe.cc` 与官方源码完全一致。
- `abc9.verify` 保持默认开启。
- ABC 后、`map_cells` 前没有 `$lut` 结构重构。
- 没有 legacy ABC。
- 九题只使用同一条 `pmux_abc9.abc`。
- 不存在按用例名、节点数或结构切换 ABC 脚本的分支。
- `kernel/yosys.cc` 只增加可移动运行所需的同目录 ABC 查找，不改变综合结果。

旧 high-score 数据中的以下做法未进入本包：

- `pmux_opt -absorb_inverters`
- 关闭 `abc9.verify`
- 修改 `_spawnv`
- 修改 `io.cc` 临时目录删除
- ABC 后 `$lut` 重写
