#!/usr/bin/env python3
"""Verify the final Cyclone IV netlist, not just the intermediate LUT graph.

Combinational cases are proved twice with a SAT miter:
  * original RTL versus the candidate's fully mapped netlist;
  * baseline's fully mapped netlist versus the candidate's.

Sequential cases are compared after full synthesis with cycle-accurate,
same-input simulation.  A reset-like port, when present, is held active for
two clocks before comparison.  This removes the power-up mismatch that can
arise when equivalent reset logic is represented by opposite polarities; it
is a differential gate-level check, not a proof over every initial state.

The lcell_comb model is attached after synthesis, so a bad mapping in
Yosys 0.69's cells_map.v cannot be hidden by an earlier $lut proof.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BASELINE = ROOT / "tools/build/official-v069/build/yosys"
DEFAULT_CANDIDATE = ROOT / "tools/build/official-v069-opt/build-current-20261025/yosys"
DEFAULT_IVERILOG = ROOT / "tools/oss-cad-suite/bin/iverilog"
DEFAULT_VVP = ROOT / "tools/oss-cad-suite/bin/vvp"
TECHMAP = ROOT / "formal/intel_cycloneiv_techmap.v"
SIM_MODEL = ROOT / "formal/intel_cycloneiv_sim.v"

# Yosys stat separates count and cell name with at least two spaces.  The
# single-space wire/port/cell totals are summaries, not cell instances.
CELL_RE = re.compile(r"^\s+(\d+)\s{2,}(\S+)\s*$")
PORT_RE = re.compile(
    r"^\s*(input|output)\s*(?:\[([^\]]+)\])?\s*([^;]+);", re.MULTILINE
)
MODULE_RE = re.compile(r"^module\s+(\\?\S+)\s*\(", re.MULTILINE)

UNMAPPED_PREFIXES = ("$lut", "$mux", "$pmux", "$_")
UNMAPPED_EXACT = {
    "$and", "$or", "$xor", "$xnor", "$logic_not", "$logic_and",
    "$logic_or", "$dff", "$dffe", "$adff", "$sdff", "$aldff",
    "$dffsr", "$dlatch", "$adlatch", "$sr", "$dlatchsr", "$mem",
    "$mem_v2", "$memrd", "$memrd_v2", "$memwr", "$memwr_v2",
    "$meminit", "$meminit_v2",
}
SUPPORTED_FINAL_CELLS = {
    "cycloneiv_lcell_comb", "$not", "dffeas", "VCC", "GND",
    "cycloneiv_io_ibuf", "cycloneiv_io_obuf",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve(path_text: str) -> Path:
    path = Path(path_text)
    return path if path.is_absolute() else ROOT / path


def source_command(source: dict) -> str:
    path = resolve(source["path"])
    language = source.get("language", "verilog")
    if language in {"rtlil", "il"} or path.suffix == ".il":
        return f"read_rtlil {path}"
    prefix = "read_verilog -sv" if language == "systemverilog" else "read_verilog"
    return f"{prefix} {path}"


def final_cells(output: str, top: str) -> dict[str, int]:
    marker = f"=== {top} ==="
    start = output.rfind(marker)
    if start < 0:
        raise RuntimeError(f"{top}: final stat section not found")
    cells = {}
    for line in output[start:].splitlines():
        match = CELL_RE.match(line)
        if match:
            cells[match.group(2)] = int(match.group(1))
    return cells


def validate_cells(top: str, cells: dict[str, int]) -> None:
    invalid = {
        name: count for name, count in cells.items()
        if name in UNMAPPED_EXACT or name.startswith(UNMAPPED_PREFIXES)
    }
    if invalid:
        detail = ", ".join(f"{name}={count}" for name, count in sorted(invalid.items()))
        raise RuntimeError(f"{top}: unmapped logic in final netlist: {detail}")
    unsupported = {
        name: count for name, count in cells.items()
        if name not in SUPPORTED_FINAL_CELLS and not name.startswith("$")
    }
    if unsupported:
        detail = ", ".join(
            f"{name}={count}" for name, count in sorted(unsupported.items())
        )
        raise RuntimeError(f"{top}: unsupported final cell model: {detail}")


def run_yosys(
    binary: Path,
    script: str,
    log_path: Path,
    timeout: int,
) -> subprocess.CompletedProcess[str]:
    command = [str(binary), "-Q", "-p", script]
    started = time.monotonic()
    proc = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    elapsed = time.monotonic() - started
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        "-- Running command: " + " ".join(command) + "\n"
        f"-- Elapsed: {elapsed:.3f}s\n\n"
        + proc.stdout + proc.stderr,
        encoding="utf-8",
    )
    return proc


def synth_final(binary: Path, case: dict, workdir: Path, arm: str,
                timeout: int) -> dict:
    top = case["top"]
    raw_il = workdir / f"{case['id']}.{arm}.il"
    raw_v = workdir / f"{case['id']}.{arm}.v"
    log = workdir / f"{case['id']}.{arm}.synth.log"
    commands = [source_command(source) for source in case["sources"]]
    commands += [
        f"synth_intel -family cycloneiv -top {top}",
        "stat",
        f"write_rtlil {raw_il}",
        f"rename {top} {arm}_{top}",
        f"write_verilog -noattr {raw_v}",
    ]
    proc = run_yosys(binary, "; ".join(commands), log, timeout)
    output = proc.stdout + proc.stderr
    if proc.returncode != 0:
        tail = "\n".join(output.strip().splitlines()[-20:])
        raise RuntimeError(f"{case['id']}/{arm}: synthesis failed\n{tail}")
    cells = final_cells(output, top)
    validate_cells(top, cells)
    dff = cells.get("dffeas", 0)
    return {
        "binary_sha256": sha256(binary),
        "cells": cells,
        "lcell": cells.get("cycloneiv_lcell_comb", 0),
        "not": cells.get("$not", 0),
        "logic": cells.get("cycloneiv_lcell_comb", 0) + cells.get("$not", 0),
        "dff": dff,
        "il": str(raw_il.relative_to(ROOT)) if raw_il.is_relative_to(ROOT) else str(raw_il),
        "verilog": str(raw_v.relative_to(ROOT)) if raw_v.is_relative_to(ROOT) else str(raw_v),
        "log": str(log.relative_to(ROOT)) if log.is_relative_to(ROOT) else str(log),
    }


def prove_miter(binary: Path, first_commands: list[str], first_top: str,
                first_name: str, gate_il: Path, gate_top: str,
                log_path: Path, timeout: int) -> dict:
    commands = list(first_commands)
    commands += [
        f"rename {first_top} {first_name}",
        f"read_rtlil {gate_il}",
        f"rename {gate_top} gate",
        f"techmap -map {TECHMAP}",
        f"miter -equiv -flatten {first_name} gate miter",
        "hierarchy -top miter",
        "sat -verify -prove trigger 0 -show-ports miter",
    ]
    proc = run_yosys(binary, "; ".join(commands), log_path, timeout)
    output = proc.stdout + proc.stderr
    passed = (
        proc.returncode == 0
        and "SAT proof finished - no model found: SUCCESS!" in output
    )
    return {
        "status": "PASS" if passed else "FAIL",
        "returncode": proc.returncode,
        "log": str(log_path.relative_to(ROOT)) if log_path.is_relative_to(ROOT)
        else str(log_path),
        "log_tail": "" if passed else "\n".join(output.strip().splitlines()[-30:]),
    }


def prove_combinational(binary: Path, case: dict, gold: dict, gate: dict,
                        workdir: Path, timeout: int) -> dict:
    top = case["top"]
    source_commands = [source_command(source) for source in case["sources"]]
    rtl_commands = source_commands + [
        f"hierarchy -top {top}",
        "proc",
        "memory",
        "opt -full",
        "flatten",
    ]
    rtl_status = prove_miter(
        binary, rtl_commands, top, "rtl", resolve(gate["il"]), top,
        workdir / f"{case['id']}.rtl-vs-final.log", timeout,
    )
    gold_commands = [f"read_rtlil {resolve(gold['il'])}"]
    gold_status = prove_miter(
        binary, gold_commands, top, "gold", resolve(gate["il"]), top,
        workdir / f"{case['id']}.baseline-vs-final.log", timeout,
    )
    status = "PASS" if (
        rtl_status["status"] == "PASS" and gold_status["status"] == "PASS"
    ) else "FAIL"
    return {
        "status": status,
        "rtl_vs_final": rtl_status,
        "baseline_vs_final": gold_status,
    }


def parse_ports(verilog: Path) -> tuple[list[dict], list[dict]]:
    match = MODULE_RE.search(verilog.read_text(errors="replace"))
    if not match:
        raise RuntimeError(f"{verilog}: module header not found")
    ports = []
    for direction, width_text, name_text in PORT_RE.findall(
        verilog.read_text(errors="replace")
    ):
        name = name_text.strip().lstrip("\\")
        width = None
        if width_text:
            width = int(width_text.split(":")[0].strip()) + 1
        ports.append({"direction": direction, "name": name, "width": width})
    inputs = [port for port in ports if port["direction"] == "input"]
    outputs = [port for port in ports if port["direction"] == "output"]
    if not inputs or not outputs:
        raise RuntimeError(f"{verilog}: missing input/output ports")
    return inputs, outputs


def declared(name: str, width: int | None) -> str:
    return f"[{width - 1}:0] {name}" if width else name


def make_testbench(gold_top: str, gate_top: str, inputs: list[dict],
                   outputs: list[dict], cycles: int) -> str:
    clock_candidates = [
        port["name"] for port in inputs
        if re.search(r"(^|_)(clk|clock)($|_)", port["name"].lower())
    ]
    if not clock_candidates:
        raise RuntimeError("sequential case has no clock-like input")
    clock = clock_candidates[0]
    reset_candidates = [
        port["name"] for port in inputs
        if re.search(r"(^|_)(reset|rst)(n|_n)?($|_)", port["name"].lower())
    ]
    reset = reset_candidates[0] if reset_candidates else None
    reset_active = None
    reset_inactive = None
    if reset:
        lower_reset = reset.lower()
        active_low = lower_reset.endswith("_n") or lower_reset.endswith("n")
        reset_active = "1'b0" if active_low else "1'b1"
        reset_inactive = "1'b1" if active_low else "1'b0"
    data_inputs = [port for port in inputs if port["name"] != clock]

    lines = [
        "`timescale 1ns/1ps",
        "module tb;",
        f"  reg {clock} = 1'b0;",
    ]
    for port in data_inputs:
        lines.append(f"  reg {declared(port['name'], port['width'])} = 0;")
    for port in outputs:
        lines.append(f"  wire {declared('g_' + port['name'], port['width'])};")
        lines.append(f"  wire {declared('t_' + port['name'], port['width'])};")
    lines += [
        "  integer cycle;",
        "  integer errors = 0;",
        f"  {gold_top} gold (" + ", ".join(
            f".{port['name']}({port['name']})" for port in inputs
        ) + ", " + ", ".join(
            f".{port['name']}(g_{port['name']})" for port in outputs
        ) + ");",
        f"  {gate_top} gate (" + ", ".join(
            f".{port['name']}({port['name']})" for port in inputs
        ) + ", " + ", ".join(
            f".{port['name']}(t_{port['name']})" for port in outputs
        ) + ");",
        "  always #5 " + clock + " = ~" + clock + ";",
        "  task check; begin",
        "    #1;",
        "    if ({" + ", ".join(f"g_{p['name']}" for p in outputs) + "} !== {"
        + ", ".join(f"t_{p['name']}" for p in outputs) + "}) begin",
        "      errors = errors + 1;",
        "      if (errors <= 10)",
        "        $display(\"MISMATCH cycle=%0d gold=%b gate=%b\", cycle, {"
        + ", ".join(f"g_{p['name']}" for p in outputs) + "}, {"
        + ", ".join(f"t_{p['name']}" for p in outputs) + "});",
        "    end",
        "  end endtask",
        "  initial begin",
        "    cycle = 0;",
    ]
    if reset:
        lines.append(f"    {reset} = {reset_active};")
    lines += [
        f"    repeat (2) @(posedge {clock});",
    ]
    if reset:
        lines.append(f"    @(negedge {clock}); {reset} = {reset_inactive};")
    lines += [
        f"    @(posedge {clock});",
        "    check;",
        f"    for (cycle = 0; cycle < {cycles}; cycle = cycle + 1) begin",
        f"      @(negedge {clock});",
    ]
    for port in data_inputs:
        lines.append(f"      {port['name']} = $random;")
    lines += [
        "      check;",
        "    end",
        "    $display(\"FINAL_MISMATCHES=%0d\", errors);",
        "    if (errors == 0) $display(\"FINAL_STATUS=PASS\");",
        "    else $display(\"FINAL_STATUS=FAIL\");",
        "    $finish;",
        "  end",
        "endmodule",
    ]
    return "\n".join(lines) + "\n"


def simulate_sequential(case: dict, gold: dict, gate: dict, workdir: Path,
                        iv: Path, vvp: Path, cycles: int, timeout: int) -> dict:
    top = case["top"]
    inputs, outputs = parse_ports(resolve(gate["verilog"]))
    tb = workdir / f"{case['id']}.tb.v"
    tb.write_text(
        make_testbench(f"gold_{top}", f"gate_{top}", inputs, outputs, cycles),
        encoding="utf-8",
    )
    executable = workdir / f"{case['id']}.sim.out"
    compile_log = workdir / f"{case['id']}.compile.log"
    compile_cmd = [
        str(iv), "-g2012", "-o", str(executable), "-s", "tb",
        str(SIM_MODEL), str(resolve(gold["verilog"])),
        str(resolve(gate["verilog"])), str(tb),
    ]
    compile_proc = subprocess.run(
        compile_cmd, capture_output=True, text=True, timeout=timeout
    )
    compile_output = compile_proc.stdout + compile_proc.stderr
    compile_log.write_text(
        "-- Running command: " + " ".join(compile_cmd) + "\n\n" + compile_output,
        encoding="utf-8",
    )
    if compile_proc.returncode != 0:
        return {
            "status": "FAIL",
            "stage": "compile",
            "errors": None,
            "cycles": cycles,
            "log": str(compile_log.relative_to(ROOT)) if compile_log.is_relative_to(ROOT)
            else str(compile_log),
            "log_tail": "\n".join(compile_output.strip().splitlines()[-30:]),
        }

    sim_log = workdir / f"{case['id']}.sim.log"
    sim_cmd = [str(vvp), str(executable)]
    sim_proc = subprocess.run(sim_cmd, capture_output=True, text=True, timeout=timeout)
    sim_output = sim_proc.stdout + sim_proc.stderr
    sim_log.write_text(
        "-- Running command: " + " ".join(sim_cmd) + "\n\n" + sim_output,
        encoding="utf-8",
    )
    match = re.search(r"FINAL_MISMATCHES=(\d+)", sim_output)
    errors = int(match.group(1)) if match else None
    passed = (
        sim_proc.returncode == 0
        and errors == 0
        and "FINAL_STATUS=PASS" in sim_output
    )
    return {
        "status": "PASS" if passed else "FAIL",
        "stage": "simulation",
        "errors": errors,
        "cycles": cycles,
        "log": str(sim_log.relative_to(ROOT)) if sim_log.is_relative_to(ROOT)
        else str(sim_log),
        "log_tail": "" if passed else "\n".join(sim_output.strip().splitlines()[-30:]),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--candidate", type=Path, default=DEFAULT_CANDIDATE)
    parser.add_argument("--iverilog", type=Path, default=DEFAULT_IVERILOG)
    parser.add_argument("--vvp", type=Path, default=DEFAULT_VVP)
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--workdir", type=Path, default=None)
    parser.add_argument("--case", action="append", dest="cases")
    parser.add_argument("--cycles", type=int, default=None)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    for path in (args.baseline, args.candidate, args.manifest, args.iverilog,
                 args.vvp, TECHMAP, SIM_MODEL):
        if not path.exists():
            parser.error(f"missing input: {path}")

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    cases = manifest["cases"]
    if args.cases:
        by_id = {case["id"]: case for case in cases}
        missing = [case_id for case_id in args.cases if case_id not in by_id]
        if missing:
            parser.error("unknown case(s): " + ", ".join(missing))
        cases = [by_id[case_id] for case_id in args.cases]

    workdir = args.workdir or Path(tempfile.mkdtemp(prefix="pmux_final_mapping_"))
    workdir.mkdir(parents=True, exist_ok=True)
    rows = []
    all_pass = True
    print(
        f"{'case':38} {'base':>6} {'cand':>6} {'dff':>5} "
        f"{'method':>10}  result"
    )
    try:
        for case in cases:
            started = time.monotonic()
            errors = []
            status = "FAIL"
            method = "-"
            gold = gate = None
            check = None
            try:
                gold = synth_final(
                    args.baseline, case, workdir, "gold", args.timeout
                )
                gate = synth_final(
                    args.candidate, case, workdir, "gate", args.timeout
                )
                if gold["dff"] != gate["dff"]:
                    raise RuntimeError(
                        f"DFF count changed: {gold['dff']} -> {gate['dff']}"
                    )
                if gold["dff"] == 0:
                    method = "sat-miter"
                    check = prove_combinational(
                        args.candidate, case, gold, gate, workdir, args.timeout
                    )
                else:
                    method = "gate-sim"
                    cycles = args.cycles or case.get(
                        "cycles", manifest.get("cycles", 2000)
                    )
                    check = simulate_sequential(
                        case, gold, gate, workdir, args.iverilog, args.vvp,
                        cycles, args.timeout,
                    )
                status = check["status"]
            except (RuntimeError, subprocess.TimeoutExpired) as exc:
                errors.append(str(exc))
            all_pass &= status == "PASS"
            elapsed = time.monotonic() - started
            base_area = gold["logic"] if gold else -1
            gate_area = gate["logic"] if gate else -1
            dff = gate["dff"] if gate else -1
            print(
                f"{case['id']:38} {base_area:6d} {gate_area:6d} {dff:5d} "
                f"{method:>10}  {status} ({elapsed:.2f}s)"
            )
            for error in errors:
                for line in error.splitlines()[:20]:
                    print(f"    {line}")
            if check and check["status"] != "PASS":
                for line in check.get("log_tail", "").splitlines()[:20]:
                    print(f"    {line}")
            rows.append({
                "id": case["id"],
                "top": case["top"],
                "sources": [
                    {"path": source["path"],
                     "sha256": sha256(resolve(source["path"]))}
                    for source in case["sources"]
                ],
                "baseline": gold,
                "candidate": gate,
                "method": method,
                "check": check,
                "status": status,
                "elapsed_s": round(elapsed, 3),
                "errors": errors,
            })
    finally:
        if args.keep:
            print(f"workdir: {workdir}")
        elif args.workdir is None:
            shutil.rmtree(workdir, ignore_errors=True)

    summary = {
        "cases": len(rows),
        "passed": sum(row["status"] == "PASS" for row in rows),
        "failed": sum(row["status"] != "PASS" for row in rows),
    }
    result = {
        "schema": 1,
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "baseline": {"path": str(args.baseline), "sha256": sha256(args.baseline)},
        "candidate": {"path": str(args.candidate), "sha256": sha256(args.candidate)},
        "metric": "cycloneiv_lcell_comb + $not",
        "summary": summary,
        "rows": rows,
    }
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(result, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"JSON -> {args.json}")
    print(
        f"\n最终网表验证：{summary['passed']}/{summary['cases']} 通过"
        + ("" if all_pass else "，存在失败项")
    )
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
