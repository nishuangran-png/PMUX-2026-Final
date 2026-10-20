#!/usr/bin/env python3
"""官方口径评测：4 个官方用例，lcell+$not，每用例百分比再平均，CPU 时间/内存峰值。

用法:
  python3 scripts/eval_official.py \
      --baseline tools/build/official-v069/build/yosys \
      --candidate tools/build/official-v069-opt/build/yosys
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ["test1", "test2", "test3", "test4"]
OFFICIAL_BASELINE = ROOT / "tools/build/official-v069/build/yosys"
OFFICIAL_CANDIDATE = ROOT / "tools/build/official-v069-opt/build/yosys"
CELL_RE = re.compile(r"^\s+(\d+)\s+(\S+)\s*$")


def run_case(yosys: str, case: str, repeat: int = 1):
    vfile = ROOT / "rtl" / "official" / case / f"{case}.v"
    script = f"read_verilog {vfile}; synth_intel -family cycloneiv -top {case}; stat"
    best = None
    for _ in range(repeat):
        timer = "/usr/bin/time"
        cmd = [timer, "-l", yosys, "-q", "-p", script] if os.path.exists(timer) else [yosys, "-q", "-p", script]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(f"{case}: yosys failed\n{proc.stderr[-2000:]}")
        # yosys -q 不输出 log；stat 结果需要非 -q 运行，改用不带 -q（时间测量仍用 time -l）
        cmd = [timer, "-l", yosys, "-p", script] if os.path.exists(timer) else [yosys, "-p", script]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(f"{case}: yosys failed\n{proc.stderr[-2000:]}")
        out = proc.stdout
        cells = {}
        for line in out.splitlines():
            m = CELL_RE.match(line)
            if m:
                cells[m.group(2)] = int(m.group(1))
        lcell = cells.get("cycloneiv_lcell_comb", 0)
        nots = cells.get("$not", 0)
        dff = cells.get("dffeas", 0)
        logic = lcell + nots
        cpu_m, mem_m = None, None
        for line in proc.stderr.splitlines():
            line = line.strip()
            m = re.match(r"(\d+\.\d+) real\s+(\d+\.\d+) user\s+(\d+\.\d+) sys", line)
            if m:
                cpu_m = float(m.group(2)) + float(m.group(3))
            m = re.match(r"(\d+)\s+maximum resident set size", line)
            if m:
                mem_m = int(m.group(1))
        rec = {"case": case, "lcell": lcell, "not": nots, "logic": logic,
               "dff": dff, "cpu_s": cpu_m, "mem_bytes": mem_m, "cells": cells}
        if best is None or rec["logic"] < best["logic"]:
            best = rec
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default=str(OFFICIAL_BASELINE))
    ap.add_argument("--candidate", default=str(OFFICIAL_CANDIDATE))
    ap.add_argument("--cases", nargs="*", default=CASES)
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--json", default=None)
    args = ap.parse_args()

    rows, reductions = [], []
    print(f"baseline : {args.baseline}")
    print(f"candidate: {args.candidate}\n")
    print(f"{'case':6} {'base':>6} {'cand':>6} {'reduct':>8} {'dff':>5} {'cpu_base':>9} {'cpu_cand':>9}")
    for case in args.cases:
        b = run_case(args.baseline, case, args.repeat)
        c = run_case(args.candidate, case, args.repeat)
        red = 0.0 if b["logic"] == 0 else (b["logic"] - c["logic"]) / b["logic"] * 100.0
        reductions.append(red)
        rows.append({"case": case, "baseline": b, "candidate": c, "reduction_pct": red})
        print(f"{case:6} {b['logic']:6d} {c['logic']:6d} {red:7.2f}% {c['dff']:5d} "
              f"{(b['cpu_s'] or 0):9.3f} {(c['cpu_s'] or 0):9.3f}")
    avg = sum(reductions) / len(reductions) if reductions else 0.0
    print(f"\n平均缩减率（每用例先算再平均）: {avg:.2f}%")
    print("官方满分线: ≥20% ；基础分线: 有缩减")
    if args.json:
        Path(args.json).write_text(json.dumps({"rows": rows, "avg_reduction_pct": avg}, indent=2), encoding="utf-8")
        print(f"JSON -> {args.json}")


if __name__ == "__main__":
    main()
