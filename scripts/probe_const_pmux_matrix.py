#!/usr/bin/env python3
"""纯组合常量 PMUX 的尺寸矩阵探针。

生成完整的 ``case`` 选择网络，每个分支只赋常量，分别用官方 Yosys 0.69
和带 ``pmux_opt`` 的候选综合。脚本保存 RTL、两侧原始日志和结构化结果，
用于判断常量选择网络是否还有类似 test4 的 ROM/LUT 优化空间。

这里只按结构生成用例，不读取官方 test 名称，也不包含业务常量特判。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tools/build/official-v069/build/yosys"
CANDIDATE = ROOT / "tools/build/official-v069-opt/build-current-20261025/yosys"
WORKDIR = ROOT / "experiments/const-pmux-20261025"
# Yosys stat separates count and cell name with at least two spaces.  Skip
# the single-space wire/port/cell summary rows.
CELL_RE = re.compile(r"^\s+(\d+)\s{2,}(\S+)\s*$")
UNMAPPED_PREFIXES = ("$lut", "$mux", "$pmux", "$_")
UNMAPPED_EXACT = {
    "$and",
    "$or",
    "$xor",
    "$xnor",
    "$logic_not",
    "$logic_and",
    "$logic_or",
    "$dff",
    "$dffe",
    "$adff",
    "$sdff",
    "$aldff",
    "$dffsr",
    "$dlatch",
    "$adlatch",
    "$sr",
    "$dlatchsr",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve(path: Path) -> Path:
    return path if path.is_absolute() else ROOT / path


def repo_relative(path: Path) -> str:
    path = path.resolve()
    return str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path)


def lcg_values(seed: int, count: int) -> list[int]:
    state = seed & 0xFFFFFFFF
    values = []
    for _ in range(count):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        values.append(state)
    return values


def make_case(address_width: int, output_width: int, seed: int) -> tuple[str, str]:
    top = f"const_pmux_a{address_width}_w{output_width}"
    values = lcg_values(seed ^ (address_width << 16) ^ output_width, 1 << address_width)
    mask = (1 << output_width) - 1
    digits = max(1, (output_width + 3) // 4)
    lines = [
        f"module {top}(input wire [{address_width - 1}:0] sel,",
        f"                output reg [{output_width - 1}:0] y);",
        "  always @* begin",
        "    case (sel)",
    ]
    for address, value in enumerate(values):
        value &= mask
        lines.append(
            f"      {address_width}'d{address}: y = {output_width}'h{value:0{digits}x};"
        )
    lines += [
        f"      default: y = {output_width}'h0;",
        "    endcase",
        "  end",
        "endmodule",
    ]
    return top, "\n".join(lines) + "\n"


def parse_final_cells(output: str, top: str) -> dict[str, int]:
    marker = f"=== {top} ==="
    start = output.rfind(marker)
    if start < 0:
        raise RuntimeError(f"{top}: 未找到最终 stat 段")
    cells: dict[str, int] = {}
    for line in output[start:].splitlines():
        match = CELL_RE.match(line)
        if match:
            cells[match.group(2)] = int(match.group(1))
    return cells


def invalid_cells(cells: dict[str, int]) -> dict[str, int]:
    return {
        name: count
        for name, count in cells.items()
        if name in UNMAPPED_EXACT or name.startswith(UNMAPPED_PREFIXES)
    }


def run_yosys(binary: Path, rtl: Path, top: str, log_path: Path) -> dict:
    script = f"read_verilog {rtl}; synth_intel -family cycloneiv -top {top}; stat"
    command = [str(binary), "-Q", "-p", script]
    started = time.monotonic()
    proc = subprocess.run(command, capture_output=True, text=True)
    elapsed = time.monotonic() - started
    output = proc.stdout + proc.stderr
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        "-- Running command: " + " ".join(command) + "\n\n" + output,
        encoding="utf-8",
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f"{top}: Yosys 失败，退出码 {proc.returncode}；日志见 {log_path}"
        )
    cells = parse_final_cells(output, top)
    invalid = invalid_cells(cells)
    if invalid:
        detail = ", ".join(f"{name}={count}" for name, count in sorted(invalid.items()))
        raise RuntimeError(f"{top}: 最终网表仍有未映射逻辑：{detail}")
    return {
        "lcell": cells.get("cycloneiv_lcell_comb", 0),
        "not": cells.get("$not", 0),
        "logic": cells.get("cycloneiv_lcell_comb", 0) + cells.get("$not", 0),
        "dff": cells.get("dffeas", 0),
        "cells": cells,
        "wall_s": round(elapsed, 6),
        "log": repo_relative(log_path),
    }


def binary_info(binary: Path) -> dict:
    proc = subprocess.run([str(binary), "-V"], capture_output=True, text=True)
    version_text = (proc.stdout + proc.stderr).strip()
    if proc.returncode != 0:
        raise RuntimeError(f"无法读取版本：{binary}")
    return {
        "path": repo_relative(binary),
        "sha256": sha256(binary),
        "version": version_text,
    }


def git_head() -> str | None:
    proc = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True
    )
    return proc.stdout.strip() if proc.returncode == 0 else None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--candidate", type=Path, default=CANDIDATE)
    parser.add_argument("--workdir", type=Path, default=WORKDIR)
    parser.add_argument("--address-widths", type=int, nargs="+", default=[2, 3, 4, 5, 6])
    parser.add_argument("--output-widths", type=int, nargs="+", default=[1, 4, 8, 16])
    parser.add_argument("--seed", type=int, default=20261025)
    parser.add_argument("--json", type=Path, default=None)
    args = parser.parse_args()

    args.baseline = resolve(args.baseline)
    args.candidate = resolve(args.candidate)
    args.workdir = resolve(args.workdir)
    if args.json is not None:
        args.json = resolve(args.json)

    for binary in (args.baseline, args.candidate):
        if not binary.exists():
            parser.error(f"缺少 Yosys：{binary}")

    rtl_dir = args.workdir / "rtl"
    log_dir = args.workdir / "logs"
    rtl_dir.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    baseline = binary_info(args.baseline)
    candidate = binary_info(args.candidate)
    if "Yosys 0.69" not in baseline["version"] or "Yosys 0.69" not in candidate["version"]:
        parser.error("基线和候选都必须是 Yosys 0.69")

    rows = []
    print(f"{'case':24} {'base':>6} {'cand':>6} {'delta':>7} {'pct':>8} {'dff':>5}")
    for address_width in args.address_widths:
        for output_width in args.output_widths:
            top, text = make_case(address_width, output_width, args.seed)
            rtl = rtl_dir / f"{top}.v"
            rtl.write_text(text, encoding="utf-8")
            base = run_yosys(
                args.baseline,
                rtl,
                top,
                log_dir / f"{top}.baseline.log",
            )
            cand = run_yosys(
                args.candidate,
                rtl,
                top,
                log_dir / f"{top}.candidate.log",
            )
            delta = base["logic"] - cand["logic"]
            pct = 0.0 if base["logic"] == 0 else delta / base["logic"] * 100.0
            row = {
                "id": top,
                "address_width": address_width,
                "output_width": output_width,
                "states": 1 << address_width,
                "rtl": repo_relative(rtl),
                "rtl_sha256": sha256(rtl),
                "baseline": base,
                "candidate": cand,
                "delta": delta,
                "reduction_pct": pct,
            }
            rows.append(row)
            print(
                f"{top:24} {base['logic']:6d} {cand['logic']:6d} "
                f"{delta:7d} {pct:7.2f}% {cand['dff']:5d}"
            )

    improved = sum(row["delta"] > 0 for row in rows)
    equal = sum(row["delta"] == 0 for row in rows)
    regressed = sum(row["delta"] < 0 for row in rows)
    total_base = sum(row["baseline"]["logic"] for row in rows)
    total_cand = sum(row["candidate"]["logic"] for row in rows)
    summary = {
        "cases": len(rows),
        "improved": improved,
        "equal": equal,
        "regressed": regressed,
        "total_baseline": total_base,
        "total_candidate": total_cand,
        "total_delta": total_base - total_cand,
        "total_reduction_pct": (
            0.0 if total_base == 0 else (total_base - total_cand) / total_base * 100.0
        ),
        "best_delta": max((row["delta"] for row in rows), default=0),
        "worst_delta": min((row["delta"] for row in rows), default=0),
    }
    result = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "git_head": git_head(),
        "seed": args.seed,
        "baseline": baseline,
        "candidate": candidate,
        "metric": "cycloneiv_lcell_comb + $not",
        "summary": summary,
        "cases": rows,
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    print(
        "\n"
        f"合计 {len(rows)} 例：改善 {improved}，持平 {equal}，回归 {regressed}；"
        f" {total_base} -> {total_cand}，"
        f"{summary['total_reduction_pct']:.2f}%"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
