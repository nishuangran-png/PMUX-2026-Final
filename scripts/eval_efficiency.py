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
import hashlib
import json
import os
import re
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OFFICIAL_BASELINE = ROOT / "tools/build/official-v069/build/yosys"
OFFICIAL_CANDIDATE = ROOT / "tools/build/official-v069-opt/build-current-20261025/yosys"
CASES = ["test1", "test2", "test3", "test4"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def version(binary: Path) -> str:
    proc = subprocess.run([str(binary), "-V"], capture_output=True, text=True)
    return (proc.stdout + proc.stderr).strip()


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

    cpu = mem = None
    for line in proc.stderr.splitlines():
        match = re.match(r"(\d+\.\d+) real\s+(\d+\.\d+) user\s+(\d+\.\d+) sys", line.strip())
        if match:
            cpu = float(match.group(2)) + float(match.group(3))
        match = re.match(r"\s*(\d+)\s+maximum resident set size", line)
        if match:
            mem = int(match.group(1))
    # time(1) 的 real 只有两位小数，不能覆盖 perf_counter 的高精度墙钟。
    return wall, cpu, mem


def summarize(samples):
    walls = [s[0] for s in samples]
    cpus = [s[1] for s in samples if s[1] is not None]
    mems = [s[2] for s in samples if s[2] is not None]
    return {
        "wall_s": {
            "median": statistics.median(walls),
            "min": min(walls),
            "max": max(walls),
        },
        "cpu_s": ({
            "median": statistics.median(cpus),
            "min": min(cpus),
            "max": max(cpus),
        } if cpus else None),
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
    print(f"计时口径 : perf_counter 墙钟 + time(1) CPU/RSS，{args.repeat} 次中位数\n")
    print(f"{'case':6} {'wall b':>8} {'wall c':>8} {'ratio':>7} "
          f"{'cpu b':>8} {'cpu c':>8} {'ratio':>7} "
          f"{'rss b MB':>10} {'rss c MB':>10} {'ratio':>7}")

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
        cpu_ratio = None
        if base["cpu_s"] and cand["cpu_s"]:
            cpu_ratio = ratio(cand["cpu_s"]["median"], base["cpu_s"]["median"])
        mem_ratio = None
        if base["mem_bytes"] and cand["mem_bytes"]:
            mem_ratio = ratio(cand["mem_bytes"]["median"], base["mem_bytes"]["median"])
        rows.append({
            "case": case,
            "baseline": {**base, "samples": base_samples},
            "candidate": {**cand, "samples": cand_samples},
            "wall_ratio": wall_ratio,
            "cpu_ratio": cpu_ratio,
            "mem_ratio": mem_ratio,
        })
        base_cpu = base["cpu_s"]["median"] if base["cpu_s"] else None
        cand_cpu = cand["cpu_s"]["median"] if cand["cpu_s"] else None
        base_mb = base["mem_bytes"]["median"] / 1048576 if base["mem_bytes"] else None
        cand_mb = cand["mem_bytes"]["median"] / 1048576 if cand["mem_bytes"] else None
        print(f"{case:6} {base['wall_s']['median']:8.3f} {cand['wall_s']['median']:8.3f} "
              f"{wall_ratio:7.3f} {base_cpu:8.3f} {cand_cpu:8.3f} {cpu_ratio:7.3f} "
              f"{base_mb:10.2f} {cand_mb:10.2f} {mem_ratio:7.3f}")

    worst_wall = max(rows, key=lambda row: row["wall_ratio"] or 0)
    worst_cpu = max(rows, key=lambda row: row["cpu_ratio"] or 0)
    worst_mem = max(rows, key=lambda row: row["mem_ratio"] or 0)
    print(f"\n最大墙钟比: {worst_wall['case']} {worst_wall['wall_ratio']:.3f}")
    print(f"最大 CPU 比 : {worst_cpu['case']} {worst_cpu['cpu_ratio']:.3f}")
    print(f"最大内存比: {worst_mem['case']} {worst_mem['mem_ratio']:.3f}")
    print("效率满分线: 所有用例均 <= 1.05；任一项 > 1.15 则效率项为 0。")

    if args.json:
        payload = {
            "method": "high-resolution wall time via perf_counter; CPU and max RSS via time(1); alternating baseline/candidate",
            "repeat": args.repeat,
            "baseline": {"path": str(baseline), "sha256": sha256(baseline), "version": version(baseline)},
            "candidate": {"path": str(candidate), "sha256": sha256(candidate), "version": version(candidate)},
            "rows": rows,
        }
        Path(args.json).write_text(json.dumps(payload, indent=2), encoding="utf-8")
        print(f"JSON -> {args.json}")


if __name__ == "__main__":
    main()
