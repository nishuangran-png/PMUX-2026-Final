#!/usr/bin/env python3
"""评测封存集：任意纯 RTL 在官方 0.69 流程下的面积、DFF 与 EQY。

封存集清单由 JSON 提供。脚本遵守一条规则：先跑完基线，再跑候选，
结果按清单顺序全部输出，不因为收益差而删用例。

用法：
  python3 scripts/eval_holdout.py \
    --baseline /tmp/fpga-yosys-baseline-069.IdEpLa/build-repro/yosys \
    --candidate /tmp/fpga-yosys-repro-069.KzR8np/build-repro2/yosys \
    --manifest experiments/holdout-20261024/manifest.json \
    --json reports/028-封存集评测.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BASELINE = Path("/tmp/fpga-yosys-baseline-069.IdEpLa/build-repro/yosys")
DEFAULT_CANDIDATE = Path("/tmp/fpga-yosys-repro-069.KzR8np/build-repro2/yosys")
DEFAULT_EQY = ROOT / "tools/oss-cad-suite/bin/eqy"
HOLDOUT_ROOT = Path(os.environ.get(
    "FPGA_HOLDOUT_ROOT", ROOT / "tools/holdout-20261024/sources"
))
CELL_RE = re.compile(r"^\s+(\d+)\s+(\S+)\s*$")
TIME_RE = re.compile(r"([\d.]+) real\s+([\d.]+) user\s+([\d.]+) sys")
RSS_RE = re.compile(r"^\s*(\d+)\s+maximum resident set size", re.M)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def source_commands(case: dict) -> list[str]:
    commands = []
    for source in case["sources"]:
        path_text = source["path"]
        if path_text.startswith("@HOLDOUT@/"):
            path = HOLDOUT_ROOT / path_text.removeprefix("@HOLDOUT@/")
        else:
            path = Path(path_text)
        if not path.is_absolute():
            path = ROOT / path
        language = source.get("language", "verilog")
        prefix = "read_verilog -sv" if language == "systemverilog" else "read_verilog"
        commands.append(f"{prefix} {path}")
    for name, value in case.get("params", {}).items():
        commands.append(f"chparam -set {name} {value} {case['top']}")
    return commands


def resolve_sources(case: dict) -> list[dict]:
    sources = []
    for source in case["sources"]:
        path_text = source["path"]
        if path_text.startswith("@HOLDOUT@/"):
            path = HOLDOUT_ROOT / path_text.removeprefix("@HOLDOUT@/")
        else:
            path = Path(path_text)
        if not path.is_absolute():
            path = ROOT / path
        sources.append({"path": path_text, "sha256": sha256(path)})
    return sources


def parse_stat(output: str, top: str) -> dict[str, int]:
    marker = f"=== {top} ==="
    start = output.rfind(marker)
    if start < 0:
        raise RuntimeError(f"{top}: 未找到最终 stat")
    cells = {}
    for line in output[start:].splitlines():
        match = CELL_RE.match(line)
        if match:
            cells[match.group(2)] = int(match.group(1))
    if "$lut" in cells:
        raise RuntimeError(f"{top}: 仍有宽 $lut，面积结果无效")
    lcell = cells.get("cycloneiv_lcell_comb", 0)
    not_cells = cells.get("$not", 0)
    dff = cells.get("dffeas", 0)
    return {"lcell": lcell, "not": not_cells, "logic": lcell + not_cells, "dff": dff}


def synth_stats(yosys: Path, case: dict, timeout: int) -> dict:
    script = "; ".join(source_commands(case) + [
        f"synth_intel -family cycloneiv -top {case['top']}",
        "stat",
    ])
    timer = "/usr/bin/time" if Path("/usr/bin/time").exists() else None
    command = [str(yosys), "-Q", "-p", script]
    if timer:
        command = [timer, "-l", *command]
    proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"{case['id']}: 综合失败\n{proc.stderr[-2000:]}")
    stats = parse_stat(proc.stdout, case["top"])
    time_match = TIME_RE.search(proc.stderr)
    rss_match = RSS_RE.search(proc.stderr)
    stats["wall_s"] = float(time_match.group(1)) if time_match else None
    stats["cpu_s"] = (float(time_match.group(2)) + float(time_match.group(3))) if time_match else None
    stats["peak_rss_bytes"] = int(rss_match.group(1)) if rss_match else None
    return stats


def keep_top_module(src: Path, dst: Path, name: str) -> None:
    lines = src.read_text(errors="replace").splitlines()
    out, pending, index = [], [], 0
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


def count_luts(path: Path) -> int:
    return sum(
        line.startswith("  cell $lut")
        for line in path.read_text(errors="replace").splitlines()
    )


def synth_lut(yosys: Path, case: dict, output: Path, workdir: Path,
              arm: str, timeout: int) -> int:
    raw = workdir / f"{case['id']}_{arm}_raw.il"
    script = "; ".join(source_commands(case) + [
        f"synth_intel -family cycloneiv -top {case['top']} -run begin:map_ffs",
        "abc9 -lut 4 -W 300",
        "clean; opt -fast; clean",
        f"write_rtlil {raw}",
    ])
    proc = subprocess.run([str(yosys), "-q", "-p", script],
                          capture_output=True, text=True, timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError(f"{case['id']}: LUT 综合失败\n{proc.stderr[-2000:]}")
    keep_top_module(raw, output, case["top"])
    if not output.exists() or output.stat().st_size == 0:
        raise RuntimeError(f"{case['id']}: 未能提取顶层模块")
    return count_luts(output)


def run_eqy(baseline: Path, candidate: Path, eqy: Path, case: dict,
            workdir: Path, timeout: int) -> dict:
    gold = workdir / f"{case['id']}_gold.il"
    gate = workdir / f"{case['id']}_gate.il"
    gold_luts = synth_lut(baseline, case, gold, workdir, "gold", timeout)
    gate_luts = synth_lut(candidate, case, gate, workdir, "gate", timeout)
    config = workdir / f"{case['id']}.eqy"
    config.write_text(f"""[gold]
read_rtlil {gold.name}
prep -top {case['top']}

[gate]
read_rtlil {gate.name}
prep -top {case['top']}

[strategy sat]
use sat
depth 10
""", encoding="utf-8")
    proc = subprocess.run(
        [str(eqy), "-d", f"eqy_{case['id']}", "-f", config.name],
        cwd=workdir, capture_output=True, text=True, timeout=timeout,
    )
    output = proc.stdout + proc.stderr
    if "DONE (PASS" in output:
        status = "PASS"
    elif "DONE (FAIL" in output:
        status = "FAIL"
    else:
        status = "ERROR"
    return {
        "status": status,
        "gold_luts": gold_luts,
        "gate_luts": gate_luts,
        "log_tail": output[-4000:],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--candidate", type=Path, default=DEFAULT_CANDIDATE)
    parser.add_argument("--eqy", type=Path, default=DEFAULT_EQY)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--reps", type=int, default=1)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    for binary in (args.baseline, args.candidate):
        if not binary.exists():
            parser.error(f"缺少 Yosys：{binary}")

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    workdir = Path(tempfile.mkdtemp(prefix="pmux_holdout_"))
    print(f"workdir: {workdir}")
    cases = manifest["cases"]
    for case in cases:
        case["_resolved_sources"] = resolve_sources(case)

    baseline_runs = {}
    print("phase 1/2: baseline")
    for case in cases:
        baseline_runs[case["id"]] = [
            synth_stats(args.baseline, case, args.timeout)
            for _ in range(args.reps)
        ]
        print(f"{case['id']:28} baseline done")

    candidate_runs = {}
    print("phase 2/2: candidate")
    for case in cases:
        candidate_runs[case["id"]] = [
            synth_stats(args.candidate, case, args.timeout)
            for _ in range(args.reps)
        ]
        print(f"{case['id']:28} candidate done")

    rows = []
    for case in cases:
        base = min(baseline_runs[case["id"]], key=lambda item: item["logic"])
        cand = min(candidate_runs[case["id"]], key=lambda item: item["logic"])
        reduction = 0.0 if base["logic"] == 0 else (
            (base["logic"] - cand["logic"]) / base["logic"] * 100.0
        )
        row = {
            "id": case["id"],
            "role": case["role"],
            "top": case["top"],
            "params": case.get("params", {}),
            "sources": case["_resolved_sources"],
            "baseline": base,
            "candidate": cand,
            "reduction_pct": reduction,
            "eqy": None,
        }
        if case.get("eqy", True):
            if not args.eqy.exists():
                raise RuntimeError(f"缺少 EQY：{args.eqy}")
            try:
                row["eqy"] = run_eqy(args.baseline, args.candidate, args.eqy,
                                     case, workdir, args.timeout)
            except Exception as exc:
                row["eqy"] = {"status": "ERROR", "error": str(exc)}
        rows.append(row)
        eqy_text = row["eqy"]["status"] if row["eqy"] else "SKIP"
        print(f"{case['id']:28} {case['role']:11} "
              f"{base['logic']:5d} -> {cand['logic']:5d} "
              f"{reduction:+7.2f}% dff {cand['dff']:4d} EQY {eqy_text}")

    payload = {
        "manifest": str(args.manifest),
        "baseline": {"path": str(args.baseline), "sha256": sha256(args.baseline)},
        "candidate": {"path": str(args.candidate), "sha256": sha256(args.candidate)},
        "environment": {
            "platform": os.uname().sysname,
            "machine": os.uname().machine,
            "python": os.sys.version.split()[0],
            "reps": args.reps,
        },
        "rows": rows,
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")
        print(f"JSON -> {args.json}")

    if not args.keep:
        shutil.rmtree(workdir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
