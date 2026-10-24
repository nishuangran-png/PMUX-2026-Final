#!/usr/bin/env python3
"""Verify constant power-of-two shift decoder lowering with direct LUT equivalence."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GOLD = ROOT / "tools/build/official-v069/build/yosys"
DEFAULT_GATE = ROOT / "tools/build/official-v069-opt/build-current-20261025/yosys"
DEFAULT_RTL = ROOT / "tests/pmux/rtl/shift_decode_cases.v"
DFFEAS_MODEL = ROOT / "formal/intel_dffeas_formal.v"

CASES = [
    {"module": "shift_decode_b5_y32", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_b5_y64_oob", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_b7_y128", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_b5_internal_xor", "trigger": 1,
     "source": "shift_decode_cases.v"},
    {"module": "shift_decode_b8_y256", "trigger": 1, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_a64_b8_y512_offset", "trigger": 1,
     "source": "shift_decode_cases.v"},
    {"module": "shift_decode_signed_a", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_b4_skip", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_y16_skip", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_a2", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_a3_skip", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_a4_signed_safe", "trigger": 0,
     "source": "shift_decode_a4_signed_safe.il"},
    {"module": "shift_decode_signed_msb_skip", "trigger": 0,
     "source": "shift_decode_signed_msb_skip.il"},
    {"module": "shift_decode_signed_b_normalized", "trigger": 0,
     "source": "shift_decode_cases.v"},
    {"module": "shift_decode_signed_width1_skip", "trigger": 0,
     "source": "shift_decode_signed_width1_skip.il"},
    {"module": "shift_decode_variable_a_skip", "trigger": 0, "source": "shift_decode_cases.v"},
    {"module": "shift_decode_b17_skip", "trigger": 0, "source": "shift_decode_cases.v"},
]


def keep_module(src: Path, dst: Path, name: str) -> None:
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


def run_command(command: list[str], timeout: int) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
    if result.returncode != 0:
        tail = (result.stdout + result.stderr).strip().splitlines()[-20:]
        raise RuntimeError("command failed: " + " ".join(command) + "\n" + "\n".join(tail))
    return result


def read_source(source: Path) -> str:
    if source.suffix == ".il":
        return f"read_rtlil {source}"
    return f"read_verilog {source}"


def probe_trigger(binary: Path, source: Path, module: str, timeout: int) -> int:
    script = (
        f"{read_source(source)}; hierarchy -top {module}; proc; opt; "
        "pmux_opt -v"
    )
    result = run_command([str(binary), "-p", script], timeout)
    output = result.stdout + result.stderr
    matches = re.findall(
        r"pmux_opt: lowered (\d+) constant power-of-two shift decoder", output
    )
    return int(matches[-1]) if matches else 0


def synth_lut(binary: Path, source: Path, module: str, out_il: Path,
              workdir: Path, arm: str, timeout: int) -> int:
    raw_il = workdir / f"{module}_{arm}_raw.il"
    script = (
        f"{read_source(source)}; "
        f"synth_intel -family cycloneiv -top {module} -run begin:map_ffs; "
        f"read_verilog {DFFEAS_MODEL}; proc; flatten; opt -full; "
        f"abc9 -lut 4 -W 300; clean; opt -fast; clean; "
        f"write_rtlil {raw_il}"
    )
    run_command([str(binary), "-q", "-p", script], timeout)
    keep_module(raw_il, out_il, module)
    return sum(
        line.startswith("  cell $lut")
        for line in out_il.read_text(errors="replace").splitlines()
    )


def prove_lut_equivalence(binary: Path, gold: Path, gate: Path, module: str,
                          timeout: int) -> None:
    script = (
        f"read_rtlil {gold}; rename {module} gold; "
        f"read_rtlil {gate}; rename {module} gate; "
        "equiv_make gold gate equiv; hierarchy -top equiv; "
        "equiv_simple -nogroup; equiv_status -assert"
    )
    result = run_command([str(binary), "-p", script], timeout)
    output = result.stdout + result.stderr
    if "Equivalence successfully proven" not in output:
        raise RuntimeError(f"{module}: equivalence command did not report success")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gold", type=Path, default=DEFAULT_GOLD)
    parser.add_argument("--gate", type=Path, default=DEFAULT_GATE)
    parser.add_argument("--rtl", type=Path, default=DEFAULT_RTL)
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    for path in (args.gold, args.gate, args.rtl, DFFEAS_MODEL):
        if not path.exists():
            parser.error(f"missing input: {path}")

    workdir = Path(tempfile.mkdtemp(prefix="pmux_shift_verify_"))
    rows = []
    all_pass = True
    print(f"{'module':32} {'hit':>3} {'gold':>6} {'gate':>6} {'delta':>7}  result")
    try:
        for case in CASES:
            module = case["module"]
            source = args.rtl.parent / case["source"]
            if not source.exists():
                parser.error(f"missing input: {source}")
            errors = []
            try:
                triggers = probe_trigger(args.gate, source, module, args.timeout)
                if triggers != case["trigger"]:
                    errors.append(
                        f"trigger count {triggers}, expected {case['trigger']}"
                    )
                gold = workdir / f"{module}_gold.il"
                gate = workdir / f"{module}_gate.il"
                gold_luts = synth_lut(
                    args.gold, source, module, gold, workdir, "gold", args.timeout
                )
                gate_luts = synth_lut(
                    args.gate, source, module, gate, workdir, "gate", args.timeout
                )
                prove_lut_equivalence(args.gate, gold, gate, module, args.timeout)
            except (RuntimeError, subprocess.TimeoutExpired) as exc:
                triggers = -1
                gold_luts = -1
                gate_luts = -1
                errors.append(str(exc))

            status = "PASS" if not errors else "FAIL"
            all_pass &= not errors
            delta = gold_luts - gate_luts if gold_luts >= 0 else None
            print(
                f"{module:32} {triggers:3d} {gold_luts:6d} {gate_luts:6d} "
                f"{delta if delta is not None else 0:7d}  {status}"
            )
            for error in errors:
                for line in error.splitlines()[:8]:
                    print(f"    {line}")
            rows.append({
                "module": module,
                "expected_trigger": case["trigger"],
                "trigger": triggers,
                "gold_luts": gold_luts,
                "gate_luts": gate_luts,
                "delta_luts": delta,
                "status": status,
            })
    finally:
        if args.keep:
            print(f"workdir: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    if args.json:
        args.json.write_text(
            json.dumps({"cases": rows, "all_pass": all_pass}, indent=2),
            encoding="utf-8",
        )
        print(f"JSON -> {args.json}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
