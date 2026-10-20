#!/usr/bin/env python3
"""从 stdin 的 yosys 日志里读出**最终**单元数。

⚠️ 必须取最后一个匹配：`synth` 脚本内部自己会跑一次 `stat`，日志里会有多个
   "N cells" 行；取第一个会拿到中间状态（实测差 13 个单元）。

用法：yosys -p "...; stat" | python3 scripts/cellcount.py [--by-type]
--by-type 时打印分布（只统计 $ 开头的内部单元类型）。
"""
import sys
import re

text = sys.stdin.read()
matches = re.findall(r"^\s+(\d+)\s+cells\s*$", text, re.M)
total = matches[-1] if matches else "?"
if "--by-type" in sys.argv:
    # 只看最后一段统计：synth 内部也会跑一次 stat，整段解析会重复计数
    tail = text.rsplit("Printing statistics", 1)[-1]
    rows = re.findall(r"^\s+(\d+)\s+(\S+)\s*$", tail, re.M)
    rows = [(int(n), t) for n, t in rows if t.startswith("$")]
    rows.sort(reverse=True)
    print(" ".join(f"{t}={n}" for n, t in rows[:8]))
else:
    print(total)
