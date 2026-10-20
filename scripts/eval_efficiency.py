#!/usr/bin/env python3
"""按官方 Windows 计时口径做效率采样：墙钟时间与峰值内存。

官方答疑明确 Windows 使用 PowerShell ``Measure-Command`` 计时，因此这里用
子进程墙钟时间作为代理，而不是 Yosys 日志里的 user+sys。每轮交替运行原版与
优化版，报告中位数与极值，并标出 1.05 / 1.15 两条评分线。

用法:
  python3 scripts/eval_efficiency.py --repeat 15
  python3 scripts/eval_efficiency.py --cases test2 --repeat 31 \
      --json reports/018-效率采样.json
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OFFICIAL_BASELINE = ROOT / "tools/build/official-v069/build/yosys"
OFFICIAL_CANDIDATE = ROOT / "tools/build/official-v069-opt/build/yosys"
CASES = ["test1", "test2", "test3", "test4"]


def run_once(binary: Path, case: str):
    vfile = ROOT / "rtl/official" / case / f"{case}.v"
    script = f"read_verilog {vfile}; synth_intel -family cycloneiv -top {case}; stat"
    timer = Path("/usr/bin/time")
    cmd = ([str(timer), "-l", str(binary), "-p", script]
           if timer.exists() else [str(binary), "-p", script])
    start = time.perf_counter()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.perf_counter() - start
    if proc.returncode != 0:
        raise RuntimeError(f"{case}: yosys failed\n{proc.stderr[-2000:]}")

    mem = None
    for line in proc.stderr.splitlines():
        match = re.match(r"\s*(\d+)\s+maximum resident set size", line)
        if match:
            mem = int(match.group(1))
            break
    # time(1) 的 real 更接近 Measure-Command；没有它时退回 Python 墙钟。
    for line in proc.stderr.splitlines():
        match = re.match(r"(\d+\.\d+) real\s+", line.strip())
        if match:
            wall = float(match.group(1))
            break
    return wall, mem


def summarize(samples):
    walls = [s[0] for s in samples]
    mems = [s[1] for s in samples if s[1] is not None]
    return {
        "wall_s": {
            "median": statistics.median(walls),
            "min": min(walls),
            "max": max(walls),
        },
        "mem_bytes": ({
            "median": statistics.median(mems),
            "min": min(mems),
            "max": max(mems),
        } if mems else None),
    }


def ratio(candidate, baseline):
    if baseline == 0:
        return None
    return candidate / baseline


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", default=str(OFFICIAL_BASELINE))
    parser.add_argument("--candidate", default=str(OFFICIAL_CANDIDATE))
    parser.add_argument("--cases", nargs="*", default=CASES)
    parser.add_argument("--repeat", type=int, default=15)
    parser.add_argument("--json", default=None)
    args = parser.parse_args()

    baseline = Path(args.baseline)
    candidate = Path(args.candidate)
    rows = []
    print(f"baseline : {baseline}")
    print(f"candidate: {candidate}")
    print(f"计时口径 : wall time（类比 PowerShell Measure-Command），{args.repeat} 次中位数\n")
    print(f"{'case':6} {'wall base':>10} {'wall cand':>10} {'ratio':>8} "
          f"{'mem base MB':>12} {'mem cand MB':>12} {'ratio':>8}")

    for case in args.cases:
        base_samples, cand_samples = [], []
        for i in range(args.repeat):
            # 交替顺序，降低温度/缓存漂移只偏向一侧的风险。
            if i % 2 == 0:
                base_samples.append(run_once(baseline, case))
                cand_samples.append(run_once(candidate, case))
            else:
                cand_samples.append(run_once(candidate, case))
                base_samples.append(run_once(baseline, case))
        base = summarize(base_samples)
        cand = summarize(cand_samples)
        wall_ratio = ratio(cand["wall_s"]["median"], base["wall_s"]["median"])
        mem_ratio = None
        if base["mem_bytes"] and cand["mem_bytes"]:
            mem_ratio = ratio(cand["mem_bytes"]["median"], base["mem_bytes"]["median"])
        rows.append({
            "case": case,
            "baseline": {**base, "samples": base_samples},
            "candidate": {**cand, "samples": cand_samples},
            "wall_ratio": wall_ratio,
            "mem_ratio": mem_ratio,
        })
        base_mb = base["mem_bytes"]["median"] / 1048576 if base["mem_bytes"] else None
        cand_mb = cand["mem_bytes"]["median"] / 1048576 if cand["mem_bytes"] else None
        print(f"{case:6} {base['wall_s']['median']:10.3f} {cand['wall_s']['median']:10.3f} "
              f"{wall_ratio:8.3f} {base_mb:12.2f} {cand_mb:12.2f} {mem_ratio:8.3f}")

    worst_wall = max(rows, key=lambda row: row["wall_ratio"] or 0)
    worst_mem = max(rows, key=lambda row: row["mem_ratio"] or 0)
    print(f"\n最大墙钟比: {worst_wall['case']} {worst_wall['wall_ratio']:.3f}")
    print(f"最大内存比: {worst_mem['case']} {worst_mem['mem_ratio']:.3f}")
    print("效率满分线: 所有用例均 <= 1.05；任一项 > 1.15 则效率项为 0。")

    if args.json:
        payload = {
            "method": "wall time via /usr/bin/time -l; alternating baseline/candidate",
            "repeat": args.repeat,
            "rows": rows,
        }
        Path(args.json).write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"JSON -> {args.json}")


if __name__ == "__main__":
    main()
