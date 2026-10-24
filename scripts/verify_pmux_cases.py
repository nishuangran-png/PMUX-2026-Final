#!/usr/bin/env python3
"""Verify arbitrary RTL cases with the official Yosys 0.69 LUT/EQY flow."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SUITE = ROOT / "tools/oss-cad-suite"
EQY = SUITE / "bin/eqy"
DFFEAS_MODEL = ROOT / "formal/intel_dffeas_formal.v"
DEFAULT_GOLD = ROOT / "tools/build/official-v069/build/yosys"
DEFAULT_GATE = ROOT / "tools/build/official-v069-opt/build-current-20261025/yosys"
DEFAULT_RTL = ROOT / "tests/pmux/rtl/dff_clear_cases.v"
DEFAULT_MODULES = [
    "dff_zero_on_a",
    "dff_zero_on_b",
    "dffe_zero_on_a",
    "dffe_zero_on_b_active_low",
    "adffe_zero_on_a",
    "dff_init_skip",
    "dffe_no_clear",
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


def synth_lut(binary: Path, rtl: Path, module: str, out_il: Path,
              workdir: Path, arm: str) -> int:
    raw_il = workdir / f"{module}_{arm}_raw.il"
    script = (
        f"read_verilog {rtl}; "
        f"synth_intel -family cycloneiv -top {module} -run begin:map_ffs; "
        f"read_verilog {DFFEAS_MODEL}; proc; flatten; opt -full; "
        f"abc9 -lut 4 -W 300; clean; opt -fast; clean; "
        f"write_rtlil {raw_il}"
    )
    result = subprocess.run(
        [str(binary), "-q", "-p", script], capture_output=True, text=True
    )
    if result.returncode != 0:
        raise RuntimeError(f"{module}/{arm}: synthesis failed\n{result.stderr[-2000:]}")
    keep_module(raw_il, out_il, module)
    return sum(
        1 for line in out_il.read_text(errors="replace").splitlines()
        if line.startswith("  cell $lut")
    )


def run_eqy(module: str, gold: Path, gate: Path, workdir: Path) -> tuple[str, str]:
    config = workdir / f"{module}.eqy"
    config.write_text(
        f"""[gold]
read_rtlil {gold.name}
prep -top {module}

[gate]
read_rtlil {gate.name}
prep -top {module}

[strategy sat]
use sat
depth 10
""",
        encoding="utf-8",
    )
    result = subprocess.run(
        [str(EQY), "-d", f"eqy_{module}", "-f", config.name],
        cwd=workdir,
        capture_output=True,
        text=True,
    )
    output = result.stdout + result.stderr
    if "DONE (PASS" in output:
        return "PASS", output
    if "DONE (FAIL" in output:
        return "FAIL", output
    return "ERROR", output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gold", type=Path, default=DEFAULT_GOLD)
    parser.add_argument("--gate", type=Path, default=DEFAULT_GATE)
    parser.add_argument("--rtl", type=Path, default=DEFAULT_RTL)
    parser.add_argument("--module", action="append", dest="modules")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()

    modules = args.modules or DEFAULT_MODULES
    for path in (args.gold, args.gate, args.rtl):
        if not path.exists():
            parser.error(f"missing input: {path}")
    if not EQY.exists():
        parser.error(f"missing EQY: {EQY}")

    workdir = Path(tempfile.mkdtemp(prefix="pmux_case_verify_"))
    all_pass = True
    print(f"{'module':30} {'gold_lut':>8} {'gate_lut':>8}  result")
    try:
        for module in modules:
            gold = workdir / f"{module}_gold.il"
            gate = workdir / f"{module}_gate.il"
            gold_luts = synth_lut(args.gold, args.rtl, module, gold, workdir, "gold")
            gate_luts = synth_lut(args.gate, args.rtl, module, gate, workdir, "gate")
            status, output = run_eqy(module, gold, gate, workdir)
            all_pass &= status == "PASS"
            print(f"{module:30} {gold_luts:8d} {gate_luts:8d}  {status}")
            if status != "PASS":
                for line in output.strip().splitlines()[-10:]:
                    print(f"    {line}")
    finally:
        if args.keep:
            print(f"workdir: {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
