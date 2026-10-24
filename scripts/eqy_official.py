#!/usr/bin/env python3
"""官方口径 EQY：对 abc9 之后的 $lut 网表做形式等价验证（gold=原版, gate=优化版）。

流程（每个用例）：
  1. 两臂各跑到 synth_intel 的 map_ffs 之前，再手动跑 abc9 -lut 4（得到 $lut 网表）
  2. write_rtlil，过滤成只保留顶层模块（abc9 辅助模块会干扰 EQY 分区）
  3. eqy: read_rtlil + prep -top <case> + strategy sat

用法:
  python3 scripts/eqy_official.py            # 默认：官网 v0.69 原版 vs 官方源码优化版
  python3 scripts/eqy_official.py --keep     # 保留临时目录与日志
  python3 scripts/eqy_official.py --gold <原版yosys> --gate <优化版yosys>
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CASES = ["test1", "test2", "test3", "test4"]
SUITE = ROOT / "tools/oss-cad-suite"
GOLD_YOSYS = ROOT / "tools/build/yosys-069-baseline-src/build/yosys"
GATE_YOSYS = ROOT / "tools/build/official-v069-formal-src/build-formal/yosys"
EQY = SUITE / "bin/eqy"
ABC_LUT = 4
DFFEAS_MODEL = ROOT / "formal/intel_dffeas_formal.v"


def keep_module(src: Path, dst: Path, name: str):
    lines = src.read_text(errors="replace").splitlines()
    out, pending, i = [], [], 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("attribute "):
            pending.append(line)
            i += 1
            continue
        if line.startswith("module "):
            mod = line.split()[1].lstrip("\\")
            block = [line]
            i += 1
            while i < len(lines) and lines[i] != "end":
                block.append(lines[i])
                i += 1
            if i < len(lines):
                block.append("end")
            i += 1
            if mod == name:
                out.extend(pending)
                out.extend(block)
            pending = []
            continue
        if not pending:
            out.append(line)
        i += 1
    dst.write_text("\n".join(out) + "\n")


def synth_lut(binary: Path, case: str, out_il: Path, tmp: Path, arm: str):
    v = ROOT / "rtl/official" / case / f"{case}.v"
    raw_il = tmp / f"{case}_{arm}_raw.il"
    script = (
        f"read_verilog {v}; "
        f"synth_intel -family cycloneiv -top {case} -run begin:map_ffs; "
        f"read_verilog {DFFEAS_MODEL}; proc; flatten; opt -full; "
        f"abc9 -lut {ABC_LUT} -W 300; clean; opt -fast; clean; "
        f"write_rtlil {raw_il}"
    )
    p = subprocess.run([str(binary), "-q", "-p", script], capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{case}: synthesis failed\n{p.stderr[-1500:]}")
    keep_module(raw_il, out_il, case)
    if not out_il.exists() or out_il.stat().st_size == 0:
        raise RuntimeError(f"{case}: failed to filter top module from {raw_il}")


def count_luts(il: Path) -> int:
    return sum(1 for line in il.read_text(errors="replace").splitlines()
               if line.startswith("  cell $lut"))


def run_eqy(case: str, gold_il: Path, gate_il: Path, workdir: Path):
    cfg = workdir / f"{case}.eqy"
    cfg.write_text(f"""[gold]
read_rtlil {gold_il.name}
prep -top {case}

[gate]
read_rtlil {gate_il.name}
prep -top {case}

[strategy sat]
use sat
depth 10
""")
    p = subprocess.run([str(EQY), "-d", f"eqy_work_{case}", "-f", cfg.name], cwd=workdir,
                       capture_output=True, text=True)
    out = p.stdout + p.stderr
    if "DONE (PASS" in out:
        return "PASS", out
    if "DONE (FAIL" in out:
        return "FAIL", out
    return "ERROR", out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--cases", nargs="*", default=CASES)
    ap.add_argument("--gold", default=str(GOLD_YOSYS))
    ap.add_argument("--gate", default=str(GATE_YOSYS))
    args = ap.parse_args()

    tmp = Path(tempfile.mkdtemp(prefix="pmux_eqy_"))
    print(f"workdir: {tmp}")
    ok = True
    print(f"{'case':6} {'lut_gold':>8} {'lut_gate':>8} {'delta':>7}  eqy")
    for case in args.cases:
        gold = tmp / f"{case}_gold.il"
        gate = tmp / f"{case}_gate.il"
        synth_lut(Path(args.gold), case, gold, tmp, "gold")
        synth_lut(Path(args.gate), case, gate, tmp, "gate")
        g = count_luts(gold)
        a = count_luts(gate)
        status, out = run_eqy(case, gold, gate, tmp)
        if status != "PASS":
            ok = False
        print(f"{case:6} {g:8d} {a:8d} {g - a:7d}  {status}")
        if status != "PASS":
            log = out.strip().splitlines()[-8:]
            for line in log:
                print("   ", line)
    if not args.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    else:
        print(f"temporary files kept: {tmp}")
    print("\nEQY 全部通过" if ok else "\n存在未通过用例，见日志")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
