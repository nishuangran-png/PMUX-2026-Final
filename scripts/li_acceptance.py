#!/usr/bin/env python3
"""Windows-first acceptance runner for the PMUX/ROM test deliverables.

This script is intentionally independent of the macOS `/usr/bin/time` and
`tools/` layout used by the early project scripts.  It records, for every case:

* Cyclone IV `cycloneiv_lcell_comb + $not`;
* DFF count;
* wall-clock time and peak process-tree memory (Windows Job Object);
* EQY result on ABC9-produced `$lut` netlists.

Example:

    python scripts/li_acceptance.py \
      --baseline C:/eda/baseline/yosys.exe \
      --candidate C:/eda/candidate/yosys.exe \
      --eqy C:/eda/oss-cad-suite/bin/eqy.exe \
      --suite all --repeats 3 \
      --out-dir reports/028-windows-acceptance

The six boundary cases in `rtl/test4_family/manifest.json` are sealed by
default.  Pass `--include-sealed` only when a release candidate is frozen.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import csv
import hashlib
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OFFICIAL_DIR = ROOT / "rtl" / "official"
FAMILY_DIR = ROOT / "rtl" / "test4_family"
OFFICIAL_FAMILY_DIR = ROOT / "rtl" / "official_family"
CELL_RE = re.compile(r"^\s+(\d+)\s+(\S+)\s*$")
WINDOWS_JOB_EXTENDED = 9
JOB_LIMIT_KILL_ON_CLOSE = 0x00002000


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_longlong),
        ("PerJobUserTimeLimit", ctypes.c_longlong),
        ("LimitFlags", wintypes.DWORD),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", wintypes.DWORD),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", wintypes.DWORD),
        ("SchedulingClass", wintypes.DWORD),
    ]


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("ReadOperationCount", ctypes.c_ulonglong),
        ("WriteOperationCount", ctypes.c_ulonglong),
        ("OtherOperationCount", ctypes.c_ulonglong),
        ("ReadTransferCount", ctypes.c_ulonglong),
        ("WriteTransferCount", ctypes.c_ulonglong),
        ("OtherTransferCount", ctypes.c_ulonglong),
    ]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION),
        ("IoInfo", IO_COUNTERS),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def child_env(eqy: Path | None = None) -> dict[str, str]:
    env = os.environ.copy()
    build_shim = ROOT / "work" / "gccshim"
    posix_candidates = [
        Path(env["PMUX_POSIX_BIN"]) if env.get("PMUX_POSIX_BIN") else None,
        Path.cwd() / "work" / "posixshim",
        ROOT.parents[1] / "posixshim" if len(ROOT.parents) > 1 else None,
    ]
    posix_shim = next(
        (path for path in posix_candidates if path is not None and path.is_dir()),
        None,
    )
    parts = []
    if eqy is not None:
        suite_root = eqy.parent.parent
        parts.extend([str(eqy.parent), str(suite_root / "lib")])
        env["YOSYSHQ_ROOT"] = str(suite_root)
        python_exe = suite_root / "lib" / "python3.exe"
        if python_exe.is_file():
            env["PYTHON_EXECUTABLE"] = str(python_exe)
        if posix_shim is not None:
            # Prefer the tested BusyBox shim. The larger EDA BusyBox build
            # mishandles make's current directory on this Windows host.
            parts.append(str(posix_shim))
        eda_root = suite_root.parent
        for candidate in (
            eda_root / "bin" / "busybox",
            eda_root / "tools" / "mingw64" / "bin",
        ):
            if candidate.exists():
                parts.append(str(candidate))
    if build_shim.exists():
        parts.append(str(build_shim))
    parts.extend(env.get("PATH", "").split(os.pathsep))
    env["PATH"] = os.pathsep.join(part for part in parts if part)
    env["PYTHONUTF8"] = "1"
    return env


def run_captured(cmd: list[str], cwd: Path | None, env: dict[str, str],
                 timeout: int, measure: bool = False) -> dict:
    """Run a process and optionally measure its Windows Job Object peak RSS."""
    job = kernel32 = process_handle = None
    if measure and os.name == "nt":
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateJobObjectW.restype = wintypes.HANDLE
        kernel32.CreateJobObjectW.argtypes = [wintypes.LPVOID, wintypes.LPCWSTR]
        kernel32.SetInformationJobObject.argtypes = [
            wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID, wintypes.DWORD,
        ]
        kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel32.OpenProcess.restype = wintypes.HANDLE
        kernel32.QueryInformationJobObject.argtypes = [
            wintypes.HANDLE, ctypes.c_int, wintypes.LPVOID,
            wintypes.DWORD, ctypes.POINTER(wintypes.DWORD),
        ]

        job = kernel32.CreateJobObjectW(None, None)
        if job:
            limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
            limits.BasicLimitInformation.LimitFlags = JOB_LIMIT_KILL_ON_CLOSE
            kernel32.SetInformationJobObject(
                job, WINDOWS_JOB_EXTENDED, ctypes.byref(limits), ctypes.sizeof(limits)
            )

    started = time.perf_counter()
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    proc = subprocess.Popen(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        creationflags=creationflags,
    )
    if job and kernel32:
        process_handle = kernel32.OpenProcess(0x0001 | 0x0100, False, proc.pid)
        if process_handle:
            kernel32.AssignProcessToJobObject(job, process_handle)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        stdout, stderr = proc.communicate()
        raise RuntimeError(f"timeout after {timeout}s: {' '.join(cmd)}")
    wall = time.perf_counter() - started

    peak = None
    if job and kernel32:
        info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        returned = wintypes.DWORD()
        if kernel32.QueryInformationJobObject(
            job, WINDOWS_JOB_EXTENDED, ctypes.byref(info),
            ctypes.sizeof(info), ctypes.byref(returned)
        ):
            peak = int(info.PeakJobMemoryUsed)
        if process_handle:
            kernel32.CloseHandle(process_handle)
        kernel32.CloseHandle(job)

    if proc.returncode:
        tail = (stdout + "\n" + stderr)[-4000:]
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{tail}")
    return {
        "stdout": stdout,
        "stderr": stderr,
        "wall_s": wall,
        "peak_mem_bytes": peak,
    }


def parse_stats(stdout: str, top: str) -> dict:
    marker = f"=== {top} ==="
    stat_at = stdout.rfind(marker)
    if stat_at < 0:
        raise RuntimeError(f"{top}: final stat section not found")
    cells: dict[str, int] = {}
    for line in stdout[stat_at:].splitlines():
        match = CELL_RE.match(line)
        if match:
            cells[match.group(2)] = int(match.group(1))
    if "$lut" in cells:
        raise RuntimeError(f"{top}: wide $lut left unmapped; area is invalid")
    lcell = cells.get("cycloneiv_lcell_comb", 0)
    nots = cells.get("$not", 0)
    return {
        "lcell": lcell,
        "not": nots,
        "logic": lcell + nots,
        "dff": cells.get("dffeas", 0),
        "cells": cells,
    }


def synth_once(binary: Path, source: Path, top: str, timeout: int,
               env: dict[str, str], measure: bool) -> tuple[dict, dict]:
    # Run beside the source and pass a relative name. Some Yosys 0.69 Windows
    # builds cannot open non-ASCII absolute paths even when Python can.
    script = f"read_verilog {source.name}; synth_intel -family cycloneiv -top {top}; stat"
    run = run_captured(
        [str(binary), "-Q", "-p", script], source.parent, env, timeout, measure
    )
    return parse_stats(run["stdout"], top), run


def keep_top_module(src: Path, dst: Path, name: str) -> None:
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines()
    out: list[str] = []
    pending: list[str] = []
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("attribute "):
            pending.append(line)
            index += 1
            continue
        if line.startswith("module "):
            module = line.split()[1].lstrip("\\")
            block = [line]
            index += 1
            while index < len(lines) and lines[index] != "end":
                block.append(lines[index])
                index += 1
            if index < len(lines):
                block.append("end")
            index += 1
            if module == name:
                out.extend(pending)
                out.extend(block)
            pending = []
            continue
        if not pending:
            out.append(line)
        index += 1
    dst.write_text("\n".join(out) + "\n", encoding="utf-8")


def make_lut_netlist(binary: Path, source: Path, top: str, raw: Path,
                     final: Path, timeout: int, env: dict[str, str]) -> int:
    script = (
        f"read_verilog {source.name}; "
        f"synth_intel -family cycloneiv -top {top} -run begin:map_ffs; "
        "abc9 -lut 4 -W 300; clean; opt -fast; clean; "
        f"write_rtlil {raw.as_posix()}"
    )
    run_captured([str(binary), "-q", "-p", script], source.parent, env, timeout)
    keep_top_module(raw, final, top)
    if not final.exists() or not final.stat().st_size:
        raise RuntimeError(f"{top}: failed to extract top module for EQY")
    return sum(
        line.startswith("  cell $lut")
        for line in final.read_text(encoding="utf-8", errors="replace").splitlines()
    )


def run_whole_miter(top: str, gold: Path, gate: Path, work: Path,
                    baseline: Path, env: dict[str, str], timeout: int) -> tuple[str, str]:
    script = (
        f"read_rtlil {gold.name}; rename {top} gold; "
        f"read_rtlil {gate.name}; rename {top} gate; "
        "miter -equiv -flatten -make_outputs gold gate miter; "
        "hierarchy -top miter; sat -verify -prove trigger 0 miter"
    )
    proc = subprocess.run(
        [str(baseline), "-Q", "-p", script], cwd=work, env=env,
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=timeout,
    )
    log = proc.stdout + proc.stderr
    if proc.returncode == 0 and "SAT proof finished - no model found: SUCCESS!" in log:
        return "PASS", log
    if "SAT proof finished" in log and "model found" in log:
        return "FAIL", log
    return "ERROR", log


def run_eqy(case: dict, baseline: Path, candidate: Path, eqy: Path,
            work_root: Path, timeout: int, env: dict[str, str],
            formal_model: Path | None = None) -> dict:
    top = case["top"]
    source = case["source"]
    work = work_root / top
    work.mkdir(parents=True, exist_ok=True)
    gold = work / f"{top}_gold.il"
    gate = work / f"{top}_gate.il"
    gold_luts = make_lut_netlist(
        baseline, source, top, work / f"{top}_gold_raw.il", gold, timeout, env
    )
    gate_luts = make_lut_netlist(
        candidate, source, top, work / f"{top}_gate_raw.il", gate, timeout, env
    )
    config = work / f"{top}.eqy"
    gate_library = ""
    if formal_model is not None:
        gate_library = f"read_verilog {formal_model.as_posix()}\n"
    config.write_text(
        f"[gold]\nread_rtlil {gold.name}\nprep -top {top}\n\n"
        f"[gate]\n{gate_library}read_rtlil {gate.name}\nprep -top {top}\n\n"
        "[strategy sat]\nuse sat\ndepth 10\n",
        encoding="utf-8",
    )
    eqy_dir = f"eqy_{top}"
    setup = subprocess.run(
        [str(eqy), "-m", "-d", eqy_dir, "-f", config.name],
        cwd=work, env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=timeout,
    )
    log = "===== EQY setup =====\n" + setup.stdout + setup.stderr
    if setup.returncode:
        partition = "ERROR"
    else:
        make_exe = shutil.which("make", path=env.get("PATH"))
        if make_exe is None:
            partition = "ERROR"
            log += "\nERROR: make executable not found in PATH\n"
        else:
            # EQY's normal `make -C <dir>` path does not preserve the child
            # working directory with MinGW Make + BusyBox bash on Windows.
            # Running the same generated Makefile inside that directory keeps
            # EQY's official partitions/strategies unchanged.
            makefile = work / eqy_dir / "strategies.mk"
            makefile.write_text(
                makefile.read_text(encoding="utf-8").replace(
                    "source run.sh", "source ./run.sh"
                ),
                encoding="utf-8",
            )
            make_run = subprocess.run(
                [make_exe, "-f", "strategies.mk"],
                cwd=work / eqy_dir, env=env, capture_output=True, text=True,
                encoding="utf-8", errors="replace", timeout=timeout,
            )
            log += "\n===== EQY strategies.mk =====\n" + make_run.stdout + make_run.stderr
            status_files = sorted((work / eqy_dir / "strategies").glob("*/*/status"))
            statuses = {
                path.read_text(encoding="utf-8", errors="replace").strip()
                for path in status_files
            }
            log += "\n===== partition statuses =====\n" + "\n".join(
                f"{path.parent.parent.name}: {path.read_text(encoding='utf-8', errors='replace').strip()}"
                for path in status_files
            ) + "\n"
            if make_run.returncode == 0 and status_files and statuses == {"PASS"}:
                partition = "PASS"
            elif "FAIL" in statuses:
                partition = "FAIL"
            else:
                partition = "ERROR"
    whole = None
    whole_log = ""
    status = partition
    if partition == "FAIL":
        whole, whole_log = run_whole_miter(top, gold, gate, work, baseline, env, timeout)
        if whole == "PASS":
            status = "PASS*"
    return {
        "status": status,
        "eqy_partition": partition,
        "whole_miter": whole,
        "gold_luts": gold_luts,
        "gate_luts": gate_luts,
        "log": log,
        "whole_log": whole_log,
    }


def load_cases(suite: str, include_sealed: bool, flat_index: Path | None) -> list[dict]:
    cases: list[dict] = []
    if suite in {"official", "all"}:
        for name in ("test1", "test2", "test3", "test4"):
            cases.append({
                "suite": "official",
                "name": name,
                "top": name,
                "source": OFFICIAL_DIR / name / f"{name}.v",
            })
    if suite in {"family", "all"}:
        manifest = json.loads((FAMILY_DIR / "manifest.json").read_text(encoding="utf-8"))
        for entry in manifest["cases"]:
            if entry["suite"] == "sealed" and not include_sealed:
                continue
            cases.append({
                "suite": entry["suite"],
                "name": entry["name"],
                "top": entry["name"],
                "source": FAMILY_DIR / entry["file"],
                "entries": entry["entries"],
                "width": entry["width"],
                "style": entry["style"],
                "ena": entry["ena"],
            })
    if suite in {"official-family", "all"}:
        manifest = json.loads(
            (OFFICIAL_FAMILY_DIR / "manifest.json").read_text(encoding="utf-8")
        )
        for entry in manifest["cases"]:
            cases.append({
                "suite": f"official-family-{entry['family']}",
                "name": entry["name"],
                "top": entry["top"],
                "source": ROOT / entry["source"],
                "profile": entry.get("profile"),
                "parameters": entry.get("parameters", {}),
            })
    if suite == "flat":
        if flat_index is None or not flat_index.is_file():
            raise RuntimeError("--flat-index is required for --suite flat")
        with flat_index.open(encoding="utf-8-sig", newline="") as stream:
            for entry in csv.DictReader(stream):
                cases.append({
                    "suite": f"flat-{entry['corresponds_to']}",
                    "name": entry["file"].removesuffix(".v"),
                    "top": entry["top"],
                    "source": flat_index.parent / entry["file"],
                    "profile": entry.get("what"),
                    "origin": entry.get("source"),
                })
    return cases


def ratio(candidate: float | int | None, baseline: float | int | None) -> float | None:
    if candidate is None or baseline in (None, 0):
        return None
    return float(candidate) / float(baseline)


def median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def write_markdown(path: Path, payload: dict) -> None:
    lines = [
        "# PMUX / test4 同族 Windows 验收报告",
        "",
        f"- 生成时间：{payload['generated_at']}",
        f"- 基线：`{payload['environment']['baseline']}`",
        f"- 候选：`{payload['environment']['candidate']}`",
        f"- EQY：`{payload['environment']['eqy']}`",
        f"- 面积口径：`cycloneiv_lcell_comb + $not`",
        f"- 采样次数：每臂 {payload['repeats']} 次，交替运行取中位数",
        "",
        "| 套件 | 用例 | 基线 | 候选 | 缩减 | DFF | 墙钟比 | 内存比 | EQY |",
        "|---|---|---:|---:|---:|---:|---:|---:|:---:|",
    ]
    for row in payload["cases"]:
        lines.append(
            f"| {row['suite']} | {row['name']} | {row['baseline']['logic']} | "
            f"{row['candidate']['logic']} | {row['reduction_pct']:.2f}% | "
            f"{row['baseline']['dff']} -> {row['candidate']['dff']} | "
            f"{row['wall_ratio']:.3f} | "
            f"{(row['mem_ratio'] if row['mem_ratio'] is not None else float('nan')):.3f} | "
            f"{row.get('eqy', {}).get('status', '-')} |"
        )
    summary = payload["summary"]
    lines.extend([
        "",
        "## 汇总结论",
        "",
        f"- 用例数：{summary['cases']}（其中封存集 {summary['sealed_cases_run']} 个）",
        f"- 面积改善：{summary['improved']}；持平：{summary['equal']}；回退：{summary['regressed']}",
        f"- 官方四题平均缩减：{summary['official_avg_reduction_pct']:.2f}%",
        f"- EQY：{summary['eqy_pass']}/{summary['eqy_total']} PASS",
        f"- 最大墙钟比：{summary['worst_wall_ratio']:.3f}",
        f"- 最大内存比：{summary['worst_mem_ratio']:.3f}",
        f"- 候选最大中位耗时：{summary['worst_candidate_wall_s']:.3f} s",
        f"- 候选最大中位峰值内存："
        f"{summary['worst_candidate_peak_mem_bytes'] / 1048576:.2f} MiB",
        f"- 相对 1.05 满分线："
        f"{'满足' if summary['meets_1_05_relative'] else '未满足'}",
        f"- 赛题绝对线（60 s / 2 GiB）："
        f"{'满足' if summary['meets_absolute_limits'] else '未满足'}",
        f"- 验收状态：**{summary['status']}**",
        "",
        "说明：`PASS*` 表示 EQY 分区模式给出 FAIL，但整模块 miter SAT 证明通过；"
        "该结果保留双记录，不能伪装成普通 EQY PASS。封存集默认不运行，只有版本冻结后"
        "使用 `--include-sealed` 显式开启。",
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--eqy", default=None)
    parser.add_argument(
        "--formal-model",
        default=None,
        help="optional Verilog model for target cells present only in the gate netlist",
    )
    parser.add_argument(
        "--suite",
        choices=["official", "family", "official-family", "flat", "all"],
        default="all",
    )
    parser.add_argument("--flat-index", default=None)
    parser.add_argument("--include-sealed", action="store_true")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--out-dir", default=str(ROOT / "reports" / "028-windows-acceptance"))
    parser.add_argument(
        "--work-dir",
        default=str(Path.cwd() / ".pmux-eqy-work"),
        help="short-path work directory for EQY (Windows MAX_PATH safety)",
    )
    parser.add_argument("--skip-eqy", action="store_true")
    args = parser.parse_args()

    baseline = Path(args.baseline).resolve()
    candidate = Path(args.candidate).resolve()
    eqy = Path(args.eqy).resolve() if args.eqy else None
    formal_model = Path(args.formal_model).resolve() if args.formal_model else None
    for label, path in (("baseline", baseline), ("candidate", candidate)):
        if not path.is_file():
            parser.error(f"{label} Yosys not found: {path}")
    if not args.skip_eqy and (eqy is None or not eqy.is_file()):
        parser.error("EQY executable not found; pass --eqy or use --skip-eqy")
    if formal_model is not None and not formal_model.is_file():
        parser.error(f"formal model not found: {formal_model}")

    env = child_env(eqy)
    flat_index = Path(args.flat_index).resolve() if args.flat_index else None
    cases = load_cases(args.suite, args.include_sealed, flat_index)
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    eqy_work = Path(args.work_dir).resolve()
    eqy_work.mkdir(parents=True, exist_ok=True)
    netlist_dir = out_dir / "netlists"
    netlist_dir.mkdir(parents=True, exist_ok=True)
    eqy_logs: list[str] = []
    rows: list[dict] = []
    failures: list[str] = []
    eqy_pass = eqy_total = 0

    print(f"{'suite':10} {'case':38} {'base':>6} {'cand':>6} {'reduct':>8} "
          f"{'wall':>7} {'mem':>7}  EQY")
    for case in cases:
            base_runs, cand_runs = [], []
            for index in range(args.repeats):
                order = (
                    (("baseline", baseline), ("candidate", candidate))
                    if index % 2 == 0 else
                    (("candidate", candidate), ("baseline", baseline))
                )
                for arm, binary in order:
                    stats, run = synth_once(
                        binary, case["source"], case["top"], args.timeout, env, True
                    )
                    sample = {
                        "logic": stats["logic"],
                        "lcell": stats["lcell"],
                        "not": stats["not"],
                        "dff": stats["dff"],
                        "wall_s": run["wall_s"],
                        "peak_mem_bytes": run["peak_mem_bytes"],
                    }
                    (base_runs if arm == "baseline" else cand_runs).append(sample)

            base_best = min(base_runs, key=lambda item: item["logic"])
            cand_best = min(cand_runs, key=lambda item: item["logic"])
            reduction = 0.0 if base_best["logic"] == 0 else (
                base_best["logic"] - cand_best["logic"]
            ) / base_best["logic"] * 100.0
            wall_ratio = ratio(
                median([item["wall_s"] for item in cand_runs]),
                median([item["wall_s"] for item in base_runs]),
            ) or 0.0
            base_mem = median([
                item["peak_mem_bytes"] for item in base_runs
                if item["peak_mem_bytes"] is not None
            ])
            cand_mem = median([
                item["peak_mem_bytes"] for item in cand_runs
                if item["peak_mem_bytes"] is not None
            ])
            mem_ratio = ratio(cand_mem, base_mem)

            eqy_result = {"status": "-"}
            if not args.skip_eqy and eqy is not None:
                eqy_result = run_eqy(
                    case, baseline, candidate, eqy, eqy_work, args.timeout, env,
                    formal_model,
                )
                for arm in ("gold", "gate"):
                    source_il = eqy_work / case["top"] / f"{case['top']}_{arm}.il"
                    if source_il.is_file():
                        shutil.copy2(
                            source_il,
                            netlist_dir / f"{case['name']}_{arm}.il",
                        )
                eqy_total += 1
                if eqy_result["status"] in {"PASS", "PASS*"}:
                    eqy_pass += 1
                else:
                    failures.append(f"{case['name']}: EQY {eqy_result['status']}")
                eqy_logs.append(
                    f"===== {case['name']} =====\n"
                    f"status={eqy_result['status']} partition={eqy_result['eqy_partition']} "
                    f"whole_miter={eqy_result['whole_miter']} "
                    f"luts={eqy_result['gold_luts']}->{eqy_result['gate_luts']}\n"
                    f"{eqy_result['log']}\n{eqy_result['whole_log']}\n"
                )

            if cand_best["logic"] > base_best["logic"]:
                failures.append(
                    f"{case['name']}: area regression "
                    f"{base_best['logic']}->{cand_best['logic']}"
                )
            if cand_best["dff"] > base_best["dff"]:
                failures.append(
                    f"{case['name']}: DFF regression "
                    f"{base_best['dff']}->{cand_best['dff']}"
                )
            if wall_ratio > 1.15:
                failures.append(f"{case['name']}: wall ratio {wall_ratio:.3f} > 1.15")
            if mem_ratio is not None and mem_ratio > 1.15:
                failures.append(f"{case['name']}: memory ratio {mem_ratio:.3f} > 1.15")

            row = {
                **case,
                "source": str(case["source"]),
                "baseline": {**base_best, "samples": base_runs},
                "candidate": {**cand_best, "samples": cand_runs},
                "reduction_pct": reduction,
                "wall_ratio": wall_ratio,
                "mem_ratio": mem_ratio,
                "eqy": {
                    key: value for key, value in eqy_result.items()
                    if key not in {"log", "whole_log"}
                },
            }
            rows.append(row)
            mem_display = "n/a" if mem_ratio is None else f"{mem_ratio:.3f}"
            print(
                f"{case['suite']:10} {case['name']:38} "
                f"{base_best['logic']:6d} {cand_best['logic']:6d} "
                f"{reduction:7.2f}% {wall_ratio:7.3f} {mem_display:>7}  "
                f"{eqy_result['status']}"
            )

    official_rows = [row for row in rows if row["suite"] == "official"]
    official_avg = (
        sum(row["reduction_pct"] for row in official_rows) / len(official_rows)
        if official_rows else 0.0
    )
    wall_values = [row["wall_ratio"] for row in rows]
    mem_values = [row["mem_ratio"] for row in rows if row["mem_ratio"] is not None]
    absolute_wall = max(
        (
            median([sample["wall_s"] for sample in row["candidate"]["samples"]])
            for row in rows
        ),
        default=0.0,
    )
    absolute_mem = max(
        (
            median([
                sample["peak_mem_bytes"] for sample in row["candidate"]["samples"]
                if sample["peak_mem_bytes"] is not None
            ])
            for row in rows
        ),
        default=0,
    )
    summary = {
        "cases": len(rows),
        "sealed_cases_run": sum(row["suite"] == "sealed" for row in rows),
        "improved": sum(row["candidate"]["logic"] < row["baseline"]["logic"] for row in rows),
        "equal": sum(row["candidate"]["logic"] == row["baseline"]["logic"] for row in rows),
        "regressed": sum(row["candidate"]["logic"] > row["baseline"]["logic"] for row in rows),
        "official_avg_reduction_pct": official_avg,
        "eqy_pass": eqy_pass,
        "eqy_total": eqy_total,
        "worst_wall_ratio": max(wall_values, default=1.0),
        "worst_mem_ratio": max(mem_values, default=1.0),
        "worst_candidate_wall_s": absolute_wall,
        "worst_candidate_peak_mem_bytes": absolute_mem,
        "meets_1_05_relative": (
            max(wall_values, default=1.0) <= 1.05
            and max(mem_values, default=1.0) <= 1.05
        ),
        "meets_absolute_limits": absolute_wall <= 60.0 and (
            absolute_mem == 0 or absolute_mem <= 2 * 1024**3
        ),
        "failures": failures,
    }
    summary["status"] = "PASS" if not failures else "FAIL"
    payload = {
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "environment": {
            "baseline": str(baseline),
            "baseline_sha256": sha256(baseline),
            "candidate": str(candidate),
            "candidate_sha256": sha256(candidate),
            "eqy": str(eqy) if eqy else None,
            "eqy_sha256": sha256(eqy) if eqy else None,
        },
        "repeats": args.repeats,
        "suite": args.suite,
        "include_sealed": args.include_sealed,
        "cases": rows,
        "summary": summary,
    }
    (out_dir / "result.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (out_dir / "eqy-all.log").write_text("\n".join(eqy_logs), encoding="utf-8")
    write_markdown(out_dir / "REPORT.md", payload)

    print("\nsummary")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    print(f"result: {out_dir / 'result.json'}")
    print(f"report: {out_dir / 'REPORT.md'}")
    return 0 if not failures else 1


if __name__ == "__main__":
    return_code = main()
    sys.exit(return_code)
