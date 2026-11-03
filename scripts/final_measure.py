#!/usr/bin/env python3
"""Official-oriented wall-clock benchmark without in-process memory polling."""

from __future__ import annotations

import argparse
import csv
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


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
    result = [
        {
            "suite": "official",
            "name": name,
            "source": root / "cases" / "official" / name / f"{name}.v",
        }
        for name in ("test1", "test2", "test3", "test4")
    ]
    result.extend(
        {
            "suite": "five",
            "name": name,
            "source": root / "cases" / "five" / f"{name}.v",
        }
        for name in (
            "pmux_case01_balanced",
            "pmux_case02_pipeline",
            "pmux_case03_crossbar",
            "pmux_case04_feedback",
            "pmux_case05_nested",
        )
    )
    return result


def run_once(binary: Path, script: Path, work_dir: Path, timeout: int,
             env: dict[str, str]):
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
        wintypes.HANDLE,
        ctypes.POINTER(ProcessMemoryCounters),
        wintypes.DWORD,
    ]

    started = time.perf_counter()
    proc = subprocess.Popen(
        [str(binary), "-q", "-s", script.name],
        cwd=work_dir,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    handle = kernel32.OpenProcess(0x0400 | 0x0010, False, proc.pid)
    try:
        proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.communicate()
        raise RuntimeError("synthesis timeout")
    wall = time.perf_counter() - started

    peak = 0
    cpu_s = 0.0
    if handle:
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if psapi.GetProcessMemoryInfo(
            handle, ctypes.byref(counters), ctypes.sizeof(counters)
        ):
            peak = int(counters.PeakWorkingSetSize)
        creation = exit_time = kernel_time = user_time = FileTime()
        if kernel32.GetProcessTimes(
            handle,
            ctypes.byref(creation),
            ctypes.byref(exit_time),
            ctypes.byref(kernel_time),
            ctypes.byref(user_time),
        ):
            ticks = (
                (kernel_time.dwHighDateTime << 32)
                | kernel_time.dwLowDateTime
            ) + (
                (user_time.dwHighDateTime << 32)
                | user_time.dwLowDateTime
            )
            cpu_s = ticks / 10_000_000.0
        kernel32.CloseHandle(handle)
    if proc.returncode:
        raise RuntimeError(f"synthesis failed with exit code {proc.returncode}")
    return {"wall_s": wall, "cpu_s": cpu_s, "peak_mem_bytes": peak}


def med(values):
    return statistics.median(values)


def mean(values):
    return statistics.fmean(values)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--repeats", type=int, default=10)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument(
        "--case", action="append", default=[],
        help="limit the run to one or more case names",
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    baseline = root / "baseline" / "yosys.exe"
    candidate = root / "candidate" / "yosys.exe"
    abc = root / "candidate" / "yosys-abc.exe"
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    work_dir = out_dir / "shared-work"
    work_dir.mkdir(exist_ok=True)

    env = os.environ.copy()
    env["PATH"] = str(root / "candidate") + os.pathsep + env.get("PATH", "")
    env["ABC"] = str(abc)
    env["PYTHONUTF8"] = "1"
    env["TMP"] = str(work_dir)
    env["TEMP"] = str(work_dir)
    env.pop("PMUX_BIN_DIR", None)

    selected_cases = [
        case for case in cases(root)
        if not args.case or case["name"] in args.case
    ]
    if not selected_cases:
        raise SystemExit("no cases selected")

    rows = []
    for case in selected_cases:
        script = work_dir / f"{case['name']}.ys"
        source = case["source"].as_posix()
        script.write_text(
            f"read_verilog \"{source}\"\n"
            f"synth_intel -family cycloneiv -top {case['name']}\n"
            "stat\n",
            encoding="utf-8",
        )
        print(f"{case['name']}: warmup", flush=True)
        run_once(baseline, script, work_dir, args.timeout, env)
        run_once(candidate, script, work_dir, args.timeout, env)
        for repetition in range(1, args.repeats + 1):
            for arm, binary in (("baseline", baseline), ("candidate", candidate)):
                sample = run_once(binary, script, work_dir, args.timeout, env)
                rows.append({
                    **case,
                    "arm": arm,
                    "repetition": repetition,
                    **sample,
                })
                print(
                    f"  {arm:9} rep={repetition:02d} "
                    f"wall={sample['wall_s']:.6f} "
                    f"cpu={sample['cpu_s']:.6f} "
                    f"mem={sample['peak_mem_bytes']}",
                    flush=True,
                )

    fields = [
        "suite", "name", "source", "arm", "repetition",
        "wall_s", "cpu_s", "peak_mem_bytes",
    ]
    with (out_dir / "raw.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    summary = []
    for case in selected_cases:
        base = [r for r in rows if r["name"] == case["name"] and r["arm"] == "baseline"]
        cand = [r for r in rows if r["name"] == case["name"] and r["arm"] == "candidate"]
        base_walls = [r["wall_s"] for r in base]
        cand_walls = [r["wall_s"] for r in cand]
        base_mem = [r["peak_mem_bytes"] for r in base]
        cand_mem = [r["peak_mem_bytes"] for r in cand]
        base_cpu = [r["cpu_s"] for r in base]
        cand_cpu = [r["cpu_s"] for r in cand]
        summary.append({
            "suite": case["suite"],
            "case": case["name"],
            "baseline_wall_mean_s": mean(base_walls),
            "candidate_wall_mean_s": mean(cand_walls),
            "wall_ratio_mean": mean(cand_walls) / mean(base_walls),
            "baseline_wall_median_s": med(base_walls),
            "candidate_wall_median_s": med(cand_walls),
            "wall_ratio_median": med(cand_walls) / med(base_walls),
            "baseline_cpu_mean_s": mean(base_cpu),
            "candidate_cpu_mean_s": mean(cand_cpu),
            "cpu_ratio_mean": mean(cand_cpu) / mean(base_cpu) if mean(base_cpu) else 0.0,
            "baseline_mem_median_bytes": med(base_mem),
            "candidate_mem_median_bytes": med(cand_mem),
            "memory_ratio_median": med(cand_mem) / med(base_mem) if med(base_mem) else 0.0,
        })

    payload = {
        "protocol": {
            "repeats": args.repeats,
            "order_each_round": "baseline then candidate",
            "warmup": "baseline then candidate once per case",
            "wall_clock": "time.perf_counter around process launch and full completion",
            "memory": "GetProcessMemoryInfo PeakWorkingSetSize read after process exit; no polling",
            "same_workdir": str(work_dir),
            "same_script_per_case": True,
            "same_abc": str(abc),
        },
        "cases": summary,
        "worst_wall_ratio_mean": max(r["wall_ratio_mean"] for r in summary),
        "worst_wall_ratio_median": max(r["wall_ratio_median"] for r in summary),
        "worst_memory_ratio_median": max(r["memory_ratio_median"] for r in summary),
    }
    (out_dir / "summary.json").write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    with (out_dir / "summary.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    print(json.dumps(payload, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
