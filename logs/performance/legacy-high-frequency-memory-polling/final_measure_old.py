#!/usr/bin/env python3
"""Official-style Windows wall/peak-memory benchmark for the PMUX flow."""

from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile
import threading
import time


CELL_RE = re.compile(r"^\s+(\d+)\s+(\S+)\s*$")


class ProcessMemoryCounters(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


class FileTime(ctypes.Structure):
    _fields_ = [
        ("dwLowDateTime", wintypes.DWORD),
        ("dwHighDateTime", wintypes.DWORD),
    ]


def cases(root: Path):
    official = root / "cases" / "official"
    five = root / "cases" / "five"
    rows = [
        {"suite": "official", "name": name, "top": name,
         "source": official / name / f"{name}.v"}
        for name in ("test1", "test2", "test3", "test4")
    ]
    rows += [
        {"suite": "five", "name": name, "top": name,
         "source": five / f"{name}.v"}
        for name in (
            "pmux_case01_balanced",
            "pmux_case02_pipeline",
            "pmux_case03_crossbar",
            "pmux_case04_feedback",
            "pmux_case05_nested",
        )
    ]
    return rows


def parse_stats(stdout: str, top: str):
    marker = f"=== {top} ==="
    at = stdout.rfind(marker)
    if at < 0:
        raise RuntimeError(f"{top}: stat section missing")
    cells = {}
    for line in stdout[at:].splitlines():
        match = CELL_RE.match(line)
        if match:
            cells[match.group(2)] = int(match.group(1))
    return {
        "lcell": cells.get("cycloneiv_lcell_comb", 0),
        "not": cells.get("$not", 0),
        "wide_lut": cells.get("$lut", 0),
        "logic": cells.get("cycloneiv_lcell_comb", 0) + cells.get("$not", 0),
        "dff": cells.get("dffeas", 0),
    }


def run_once(binary: Path, case: dict, temp_root: Path, env: dict, timeout: int):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.GetProcessTimes.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(FileTime),
        ctypes.POINTER(FileTime),
        ctypes.POINTER(FileTime),
        ctypes.POINTER(FileTime),
    ]
    psapi.GetProcessMemoryInfo.argtypes = [
        wintypes.HANDLE, ctypes.POINTER(ProcessMemoryCounters), wintypes.DWORD,
    ]

    work = Path(tempfile.mkdtemp(prefix="pmux-eval-", dir=temp_root))
    script_path = work / "run.ys"
    source = case["source"].resolve().as_posix()
    script_path.write_text(
        f"read_verilog \"{source}\"\n"
        f"synth_intel -family cycloneiv -top {case['top']}\n"
        "stat\n",
        encoding="utf-8",
    )

    started = time.perf_counter()
    proc = subprocess.Popen(
        [str(binary), "-Q", "-s", str(script_path)],
        cwd=work,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    handle = kernel32.OpenProcess(0x0400 | 0x0010, False, proc.pid)
    peak = 0

    def monitor():
        nonlocal peak
        if not handle:
            return
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        while proc.poll() is None:
            if psapi.GetProcessMemoryInfo(
                handle, ctypes.byref(counters), ctypes.sizeof(counters)
            ):
                peak = max(peak, int(counters.PeakWorkingSetSize))
            time.sleep(0.002)

    thread = threading.Thread(target=monitor, daemon=True)
    thread.start()
    def update_peak():
        nonlocal peak
        if handle:
            counters = ProcessMemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            if psapi.GetProcessMemoryInfo(
                handle, ctypes.byref(counters), ctypes.sizeof(counters)
            ):
                peak = max(peak, int(counters.PeakWorkingSetSize))

    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()
        raise RuntimeError(f"timeout: {case['name']}")
    wall = time.perf_counter() - started
    update_peak()
    thread.join(timeout=1.0)
    creation = exit_time = kernel_time = user_time = FileTime()
    cpu_s = 0.0
    if handle and kernel32.GetProcessTimes(
        handle, ctypes.byref(creation), ctypes.byref(exit_time),
        ctypes.byref(kernel_time), ctypes.byref(user_time)
    ):
        ticks = (
            (kernel_time.dwHighDateTime << 32) | kernel_time.dwLowDateTime
        ) + ((user_time.dwHighDateTime << 32) | user_time.dwLowDateTime)
        cpu_s = ticks / 10_000_000.0
    if handle:
        kernel32.CloseHandle(handle)
    if proc.returncode:
        raise RuntimeError(
            f"failed: {case['name']} ({proc.returncode})\n{(stdout + stderr)[-5000:]}"
        )
    stats = parse_stats(stdout, case["top"])
    if stats["wide_lut"]:
        raise RuntimeError(
            f"{case['name']}: {stats['wide_lut']} unmapped wide $lut cell(s)"
        )
    return {
        **stats,
        "wall_s": wall,
        "cpu_s": cpu_s,
        "peak_mem_bytes": peak,
    }


def median(values):
    return statistics.median(values)


def ratio(a, b):
    return a / b if b else 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument(
        "--case",
        action="append",
        default=[],
        help="limit the run to one or more case names",
    )
    args = parser.parse_args()

    if os.name != "nt":
        raise SystemExit("Windows required")
    root = Path(args.root).resolve()
    baseline = Path(args.baseline).resolve()
    candidate = Path(args.candidate).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    temp_root = out_dir / "synthesis-work"
    temp_root.mkdir(exist_ok=True)

    env = os.environ.copy()
    bin_override = os.environ.get("PMUX_BIN_DIR")
    if bin_override:
        bin_dir = Path(bin_override).resolve()
    else:
        bin_dir = root / "bin"
        if not bin_dir.is_dir():
            bin_dir = root / "runtime" / "bin"
    env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    env["ABC"] = str(bin_dir / "yosys-abc.exe")
    env["PYTHONUTF8"] = "1"

    selected_cases = [
        case for case in cases(root)
        if not args.case or case["name"] in args.case
    ]
    if not selected_cases:
        raise SystemExit("no cases selected")

    raw = []
    for case in selected_cases:
        print(f"{case['name']}: warmup", flush=True)
        run_once(baseline, case, temp_root, env, args.timeout)
        run_once(candidate, case, temp_root, env, args.timeout)
        for repetition in range(1, args.repeats + 1):
            order = (
                (("baseline", baseline), ("candidate", candidate))
                if repetition % 2
                else (("candidate", candidate), ("baseline", baseline))
            )
            for arm, binary in order:
                result = run_once(binary, case, temp_root, env, args.timeout)
                row = {
                    "suite": case["suite"],
                    "case": case["name"],
                    "arm": arm,
                    "repetition": repetition,
                    **result,
                }
                raw.append(row)
                print(
                    f"  {arm:9} rep={repetition:02d} "
                    f"wall={result['wall_s']:.6f} "
                    f"mem={result['peak_mem_bytes']} logic={result['logic']}",
                    flush=True,
                )

    fields = [
        "suite", "case", "arm", "repetition",
        "wall_s", "cpu_s", "peak_mem_bytes",
        "logic", "lcell", "not", "wide_lut", "dff",
    ]
    with (out_dir / "raw.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(raw)

    summary = []
    for case in selected_cases:
        base = [r for r in raw if r["case"] == case["name"] and r["arm"] == "baseline"]
        cand = [r for r in raw if r["case"] == case["name"] and r["arm"] == "candidate"]
        base_wall = median([r["wall_s"] for r in base])
        cand_wall = median([r["wall_s"] for r in cand])
        base_cpu = median([r["cpu_s"] for r in base])
        cand_cpu = median([r["cpu_s"] for r in cand])
        base_mem = median([r["peak_mem_bytes"] for r in base])
        cand_mem = median([r["peak_mem_bytes"] for r in cand])
        reduction = (base[0]["logic"] - cand[0]["logic"]) / base[0]["logic"] * 100
        summary.append({
            "suite": case["suite"],
            "case": case["name"],
            "baseline_logic": base[0]["logic"],
            "candidate_logic": cand[0]["logic"],
            "reduction_pct": reduction,
            "baseline_dff": base[0]["dff"],
            "candidate_dff": cand[0]["dff"],
            "baseline_wall_s": base_wall,
            "candidate_wall_s": cand_wall,
            "wall_ratio": ratio(cand_wall, base_wall),
            "baseline_cpu_s": base_cpu,
            "candidate_cpu_s": cand_cpu,
            "cpu_ratio": ratio(cand_cpu, base_cpu),
            "baseline_mem_bytes": base_mem,
            "candidate_mem_bytes": cand_mem,
            "memory_ratio": ratio(cand_mem, base_mem),
        })

    with (out_dir / "summary.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    payload = {
        "repeats": args.repeats,
        "order": "alternating by repetition (baseline first on odd runs)",
        "cases": summary,
        "worst_wall_ratio": max(r["wall_ratio"] for r in summary),
        "worst_cpu_ratio": max(r["cpu_ratio"] for r in summary),
        "worst_memory_ratio": max(r["memory_ratio"] for r in summary),
        "area_regressions": sum(
            r["candidate_logic"] > r["baseline_logic"] for r in summary
        ),
        "dff_increases": sum(
            r["candidate_dff"] > r["baseline_dff"] for r in summary
        ),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return 0 if (
        payload["worst_wall_ratio"] <= 1.05
        and payload["worst_memory_ratio"] <= 1.05
        and payload["area_regressions"] == 0
        and payload["dff_increases"] == 0
    ) else 1


if __name__ == "__main__":
    sys.exit(main())
